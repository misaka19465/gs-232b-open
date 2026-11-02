# UART1 传输与 USB 日志隔离

## 板级适配器

`gs232b_esp_uart_open()` 独占 UART1：TX=GPIO6、RX=GPIO7，8N1、无 CTS/RTS。
启动时加载独立的波特率记录，并在 GPIO7 上捕获 CR 波形，识别
1200/2400/4800/9600 bit/s。15 秒内未确认时，使用上次有效记录；记录不存在或
无法恢复时使用工厂回退 9600。`monitor_speed=115200` 仍只用于 USB 日志。

## 锁定 SDK 与 RX 捕获

验证环境为 `espressif32@8.0.0` / `framework-espidf 4.60100.0` / ESP-IDF **6.1.0**，
目标 ESP32-C3。实现见 `src/platform/esp_uart_capture.c`，使用公开的
`driver/rmt_rx.h`：`rmt_new_rx_channel` → `rmt_rx_register_event_callbacks` →
`rmt_enable` → `rmt_receive`，最终 `rmt_disable` / `rmt_del_channel`。
不使用 UART0 ROM 波特率检测，也不轮询四种 UART 配置。

核对的锁定 SDK 源码包括：

- `components/esp_driver_rmt/include/driver/rmt_rx.h`、`driver/rmt_types.h`：
  RX 配置、ISR 回调、`flags.is_last` 及 `en_partial_rx` 契约。
- `components/soc/esp32c3/include/soc/soc_caps.h`：RMT RX ping-pong，单通道
  48 个 symbol；实现有相应编译期断言。
- `components/esp_driver_rmt/src/rmt_rx.c`：GPIO 输入矩阵连接、partial 块交付、
  `rmt_receive` 的非阻塞与缓冲区复用、停用时关闭中断及删除时释放中断。
- `components/esp_hal_rmt/esp32c3/include/hal/rmt_ll.h`：idle 最大 32767 tick，
  filter 使用外设时钟。另核对 `esp_driver_uart` 的 `uart_set_pin` GPIO 输入矩阵
  路由和 `uart_set_baudrate`；GPIO7 同时接入 UART1 RX 与 RMT RX，TX 仍为 GPIO6。

RMT 使用 APB 时钟、1MHz 分辨率（1 tick=1µs）、48-symbol 硬件块与
48-symbol 用户缓冲区；启用 partial RX 支持连续 CR。小于 1µs 的毛刺由硬件过滤；
某电平超过 12ms 时结束接收，12ms 大于 1200bps CR 最长 4bit 低电平的约 3.33ms，
也低于 idle 上限。ISR 只复制至一个有界队列，SDK 可立即复用原缓冲区；控制任务
一次最多处理一块。队列被覆盖时序号缺口作废旧候选，末块事件仍被保留以便重启。
不使用 C3 工具链不保证无锁的 `atomic_bool` 在新增 ISR 路径中发布缓冲区。
最终块若用零时长 EOF 省略末尾 idle 区间，回调同时读取 `gpio_get_level(7)`；
仅当 CR 的全部数据段已通过且 idle 尾部为高，才用 RMT 的 12ms idle 结束事实确认
停止位。低电平尾部或残缺数据不能凭 EOF 补齐。显式停止区间仍按捕获宽度验证。

`src/core/uart_baud.c` 验证 start + 八个 LSB-first 数据位 + stop 的完整 0x0D：
电平运行长度为 **L1/H1/L1/H2/L4/H≥1**。各数据段容差为 ±8%；停止高电平至少
0.92 个标称位宽，可包含空闲。连续 **三个同速完整 CR** 才锁定；不接受单个位宽、
仅两个 CR 或 UART 在错误速率下解码出来的 `\r`。噪声、错电平、位宽不符、捕获
缺口、EOF 后非零数据会清候选；最终块中的半字符不能接到下一次捕获。partial
块属于同一次连续捕获，允许字符跨块。完成时间必须严格早于 15 秒截止，截止前
已发布的结果可由稍晚到达的控制任务消费；截止时仍未完成的捕获按超时处理。

确认/回退后先停止和释放 RMT，只调用一个最终 `uart_set_baudrate(UART_NUM_1, ...)`，
清掉训练字节与错误事件，再允许 `attach`。匹配窗口中不解析/执行命令，不发欢迎语。
捕获重新启动失败或硬件切换失败返回错误，控制任务可重试；不会伪装成匹配成功。

## 波特率持久化

`gs232b_esp_uart_open(adapter, store)` 代替旧的固定 `baud` 参数接口，当前仓库调用方
已同步更新。新增 `GS232B_STORE_UART_BAUD` 追加在原四个域后，不改变既有域编号及
旧记录。UART schema=1，payload 为 4 字节 little-endian 波特率；使用已有 CRC-32、
revision 比较、提交后读回与 NVS 双槽机制，不写入原生 C 结构体。

只有三个完整 CR 确认后才保存；同速已有有效记录则不重复写，超时也不写。
当前记录损坏或语义无效时尝试上一完整槽。`boot_status.load_error/save_error`
保留诊断；保存失败仍按当前已识别速率接收，`saved_baud` 保留旧值，下次启动按旧值
回退。不自动擦除损坏记录，不把 Flash 错误转为协议文本，也不重复每轮写 Flash。

`boot_service` 的持久化由独立存储任务执行；它只在启动已知全停且尚未开放其他控制入口时
调用，不能在运动循环、ISR 或 RX 任务中调用。它完成后重复调用只返回 OK。

## 接收与发送缓冲

专用 `gs_uart_rx` 任务每轮最多搬运 256 字节并让出一个调度 tick，不解析命令、不
调用运动核心、不发送日志或协议响应。IDF RX ring 为 **32768 字节**，另有八个
256 字节的数据包；每个数据包在入队前带上完整会话键和 command epoch。
软件包队列暂满时停止取 driver ring，保留其缓冲能力；不会读出后静默扔掉。
初始动态预算还包括约 2.3KiB 包队列、32 项事件队列、3072 字节 RX 任务栈、约
220 字节捕获队列条目及 RMT/IDF/RTOS 元数据；4KiB TX FIFO 与 192 字节捕获用户
缓冲区位于调用方实例中。捕获资源在匹配/回退后释放，不能只按 32KiB 估计内存。

`session_io.send` 原子复制完整字节范围到 **4096 字节** TX FIFO，队满时不接纳
任何部分；NUL、CR/LF 和二进制字节原样保留。控制任务的 `service` 每轮最多调用
一次 `uart_tx_chars`，提交不超过 128 字节，保留未发送后缀。driver TX ring 为 0，
不调用会等待线上发送完毕的 `uart_write_bytes`。UART 驱动只允许该适配器访问，
避免其他调用者持有 IDF 内部互斥锁。FIFO 持续拥塞导致响应无法入队时，dispatcher
关闭来源会话；来源若为控制者，现有关闭路径负责全停、清序列及撤权。

## 与控制任务连接

[组合入口](app-lifecycle.md)已完成整机启动编排；UART 波特率匹配和协议会话在网络
启动失败时独立运行。真实捕获和机械参数仍需实板验证。

1. 存储就绪且运动已知全停时，使用清零、生命周期足够长的实例调用 `open(adapter, store)`。
2. 启动控制任务持续调用 `boot_service`；WOULD_BLOCK 表示等待，下轮再试。建议每
   1–5ms 服务一次；未匹配的最终块需由控制任务重新启动捕获。硬件/存储错误查看
   返回码与 `boot_status`，不要在 UART1 写日志。
3. `boot_service` 返回 OK 且 `boot_status.ready` 后，以 `gs232b_esp_uart_io()`
   初始化 dispatcher，并调用 `attach`，仅为该新会话排队一次 `Connect OK\r\n`。
   提前 attach 返回 NOT_READY；重复 attach 不创建第二个欢迎语。
4. 同一控制任务在每轮 `dispatcher_tick` **前后**调用 `service`，与所有控制/运动 API
   串行。`WOULD_BLOCK` 表示 RX 暂持锁，下轮再试；协议拒绝码也不代表通道断开。
5. `close` 先关闭协议会话，再请求 RX 自行退出，最多等待 100ms。退出超时或驱动
   释放失败时保留资源句柄，调用方保留实例并重试，禁止释放仍被任务使用的内存。

`session_io.receive` 故意为空：协议输入只能消费带会话键及 epoch 的数据包。
多传输应用需提供自己的 session_io 路由，分别持有各传输会话，不能向旁路通道广播
响应。`attach` 的同步开会话窗口允许 dispatcher 在返回 session 之前发送欢迎语；
其他时间不能用陌生键创建或接管 UART 会话。关闭丢弃软件 TX，已提交到物理 FIFO
的字节无法撤回；FIFO 未排空前不允许重新 attach。

epoch 改变时，上游未消费输入被清理；存在残留时同时要求下一次 CR 才恢复解析。
由此不能把旧请求在出队时重新盖成新 epoch。RX 任务与该屏障通过专用互斥锁串行，
控制入口取锁零等待，不让协议处理等待 UART 收满块。

## 接收故障

以下情况合并为接收丢字节故障：`UART_FIFO_OVF`、`UART_BUFFER_FULL`、帧错误、
奇偶校验错误、BREAK、事件队列饱和，以及读取失败或违反读取容量契约。
事件队列饱和可能掩盖后续错误通知，因此保守处理；`UART_BUFFER_FULL` 也不要求
等待进一步确认硬件 FIFO 已丢字节。普通数据、RX 空闲及 TX 暂时写入 0 字节不算断线。

RX 端只锁存事件并清不可信数据；控制任务优先调用
`gs232b_dispatcher_input_lost()`，不先 feed 已排队的旧数据：

- 来源为活动控制者：立即停止两轴，清序列、退出校准、作废旧请求并撤销所有权。
- 来源为旁路会话：只清其自身未执行请求、半行及上传预约，保留当前控制者运动。
- 按来源既有响应顺序返回一次 `?>\r\n`，丢弃后续字节至下一个 CR，再接受新完整行。
- 清 RX 持续失败时保持隔离，同一未清理故障不会每轮重复发错；停机失败保留现有
  运动核心的输出故障状态，不用恢复输入来掩盖输出未知。

UART1/CH340K 未连接可识别串口开关的 DTR/RTS/CTS；**没有 USB2 拔线检测**，也没有
把接收空闲、TX 拥塞或 USB1 状态伪装成 USB2 断线。持续运动仍依赖既有最大运行时间、
反馈异常和限位保护；本适配器不新增短租约。

## USB 日志与验证

应用和 bootloader 均由 `sdkconfig.defaults` 选择原生 USB Serial/JTAG 为唯一控制台，
禁用 UART 控制台和次级镜像。适配器在错误配置下直接拒绝编译；`scripts/test.py`
检查两个实际生成的 sdkconfig，防止本地旧配置覆盖 defaults 后悄悄恢复 UART 日志。
UART TX 唯一内容来源为指定会话的 `send`；不使用 stdout/stderr、日志回调、stdin、
USB 读函数或 USB console REPL 来收发 GS 协议。USB1 输入不连接协议解析链。

运行 `python scripts/test.py`：常规 ESP32-C3 构建、`uart-capture-api-check` 专用
目标链接、两个环境的应用/bootloader 控制台检查、全部原生测试。专用环境用
`--undefined` 强制保留启动入口和 NVS 端口，再检查 ELF 中真实 RMT/UART/NVS 符号；
避免链接裁剪掩盖目标 API 问题。它仍是构建验收，不执行捕获。
`esp_uart_tests` 直接编译实际适配器，使用 UART/RTOS API 替身验证引脚/格式、专用任务、
32KiB ring、16384 字节长行、二进制透明性、TX 部分写/队满/代际隔离、事件/读取故障、
RX 屏障、空闲非断线及资源释放重试。`esp_uart_boot_tests`、`esp_uart_capture_tests`
按 UART 起始位和 LSB 数据位合成边沿，经实际 RMT 回调/捕获解析/启动状态机验证
四种速率、±7% 偏差、全部 255 个非 CR 字节、混速、噪声、残缺数据/停止位、
短停止位、EOF、partial 跨字符、SDK 缓冲区复用、队列缺口、截止边界、断言单次
欢迎语，以及捕获/切速/清理重试。`uart_baud_store_tests` 验证 CRC/语义损坏、上一槽
恢复、revision 冲突、失败原子性与域隔离。`transport_fault_tests` 使用真实控制/运动核心
验证控制者停机、序列与校准清理、旁路隔离及 CR 重同步。三项反向编译测试拒绝 UART
日志、次级镜像和缺失 USB 控制台。

**硬件能力边界：**SDK 源码与真实 C3 目标编译/链接证明所用 API 及目标特性成立；
API 替身与合成边沿只证明软件判断和生命周期。GPIO7 实际捕获、停止位/idle 结束
表示、重启接收间隙、长串 CR、CH340K 时钟偏差、系统中断负载、NVS 掉电、USB 插拔
及堆余量均须上板验证，不能把这些主机测试当作真实捕获可靠性的证据。
