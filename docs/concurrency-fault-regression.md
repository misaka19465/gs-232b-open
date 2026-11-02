# 并发、资源上限与故障注入回归

记录日期：2026-11-02。主机回归与目标构建通过；板上堆峰值、真实调度延迟、硬件任务看门狗及物理停机测量尚未执行，不能据此关闭硬件验收。

## 运行与证据

```sh
python3 scripts/test.py
python3 scripts/test.py --host-only --sanitizers
python3 scripts/resource_budget.py
```

本次常规主机测试 **53/53** 通过；Clang 的 ASan/LSan/UBSan **53/53** 通过，设置 `detect_leaks=1:halt_on_error=1` 和 `halt_on_error=1:print_stacktrace=1`，无忽略列表。默认固件和 UART/Ethernet/STA/WebSocket 四个 API 构建环境全部通过，既有 USB 控制台、ESP32-C3/ESP-IDF 6.1.0 和真实 SDK 符号检查均通过。

每次运行在 `build/host/results.xml` 或 `build/host-sanitized/results.xml` 写出 JUnit 结果，CTest 详细日志位于相应目录的 `Testing/Temporary/LastTest.log`。资源脚本写出 `build/resource-budget/budget.json`，其中板上指标保持 `null`。这些目录是构建产物，不提交历史结果冒充后续运行证据。

新增 `concurrency_regression_tests` 直接编译真实 UART/RMT 和 WS RX/TX 适配器、dispatcher、控制/运动/序列核心；同一实例保持 1 个 UART、2 个 Ethernet WS 和 2 个 STA WS，共用产品实际的 **2048B** 请求正文池。测试路由遵守 `esp_app.c` 的完整会话键规则。RTOS/HTTPD/UART 使用可逐步推进的替身，因此验证的是交错次序、资源边界和故障状态；不代表 FreeRTOS 抢占、网络吞吐或墙上时间。共享 SPI 的真实 pthread 争用边界继续由 `shared_spi_tests` 覆盖。

## 验收矩阵

| 场景 | 可执行证据 | 关键断言 |
| --- | --- | --- |
| 最大长行慢速上传 | `concurrency_upload_tests.c::maximum_slow_upload`、既有 `sequence_upload_tests` | Ethernet 上传 M 的 3800 点、UART 上传 W 的 1900 对；CR 前恰 16384B，每块最多 113B，逻辑间隔 1s，总逻辑时间 145s。其余 4 个会话每块完成查询，WS 心跳正常；CR 前无所有者、无活动序列、输出全停；CR 后一次提交且释放预约，无整行副本。 |
| 4 个网络会话加 UART | 全部 `concurrency_*` 场景 | 真实适配器共用 dispatcher，响应按来源隔离，网络连接/发送失败不阻止 UART 或其他客户端查询。 |
| 查询洪泛 | `concurrency_pressure_tests.c::query_flood_and_connection_exhaustion` | 同一 256B 输入包含 128 个查询，超过单会话请求/响应占位预算后仅关闭来源；UART 控制者保留，旁路查询和 S 仍工作，关闭槽可重连。 |
| 满队列下 S | `full_live_queue_stop`、`full_core_queue_stop` | 实际 5 个传输会话各积压 8 条，共 40 条；核心单独填满 7×8=56 条，额外普通查询被拒。S 不占普通队列，下一次 tick 先停机、推进 epoch、清普通队列和字节池；已接纳只读响应顺序保留。 |
| 旧 epoch | `concurrency_fault_tests.c::queued_old_epochs`、`stale_epoch_and_watchdog`、既有 UART/WS 故障测试 | WS 的 L 和 UART 的 U 在入队时盖旧章；另一 WS 的 S 执行后，队列内容不重盖新章。WS 返回 CONFLICT，UART 清旧队列并等待 CR 重同步，无运动重放；新查询恢复。 |
| 慢客户端 | `slow_owner_and_nvs_failure`、`ws_fault_tests` | TX 满 4096B、socket 零写或 HTTPD pump 暂不执行；满额不部分复制，2s 期限由控制端检查，控制者同步全停/撤权，其他 3 个 WS 与 UART 继续；既有短写测试验证保留后缀。 |
| 连接耗尽 | `query_flood_and_connection_exhaustion`、`ws_fault_tests` | 4 个槽占满后连续 64 次第 5 连接失败，不逐出既有控制者；释放后重连，初始化分配/握手失败资源回收仍由既有测试覆盖。 |
| SPI 失败 | `spi_failure_and_retry`、`shared_spi_tests`、`app_startup_tests` | 真实共享 SPI 输出端在移位阶段注入失败；单次失败允许一次安全停机，连续两次失败保留 output.known=false；撤权并锁存 OUTPUT 故障，后续 tick 不重试驱动，显式 clear 后恢复。锁存、释放、PWM 和 W5500 争用/失败边界由既有测试覆盖。 |
| NVS 失败 | `nvs_store_fault_tests`、`esp_store_worker_tests`、`slow_owner_and_nvs_failure` | 直接编译真实 `nvs_store.c`，分别在 blob 写、数据 commit、活动槽选择写、选择 commit、erase commit 注入错误；失败后旧活动记录保留，未激活的新槽不会成为当前配置，成功后双槽可读。worker 验证独立 NVS owner、运动中拒写、失败不更新 RAM；组合测试验证持久化失败后显示状态保留并可重试。 |
| 应用看门狗超时 | `stale_epoch_and_watchdog`、`ws_watchdog_tests` | UART 控制者到 1000ms 时关闭、全停、撤权，generation 增加后重新 attach；其他 WS 保持可查询。既有测试验证 Pong、旁路查询和非法命令不续期。这里覆盖的是 dispatcher 应用看门狗，SDK 任务 WDT 的真实阻塞/复位另测。 |

每轮组合服务均检查：普通请求数/正文池、每会话 pending/响应占位、增量角度数/实际存储量、UART ring/包/事件/TX、WS RX/TX/控制帧/wire 均不越界。关闭 fixture 后队列、信号量、任务计数为零。内存容量和 SDK 配置详见 [资源预算](resource-budget.md)。

## 本次故障与修正

| 编号 | 触发与影响 | 修正及回归 |
| --- | --- | --- |
| HOST-UB-01 | 首轮 UBSan 在 `line_parser_tests.c::collect_event` 报出 `memcpy(bytes, NULL, 0)`：合法 M/W 增量提交不携带原始正文。普通主机运行不会暴露这一测试收集器 UB。 | 零长度正文和零个角度跳过复制；保留原有内容断言，完整 sanitizer 重跑通过。未改变固件解析契约。 |
| FAKE-RTOS-01 | 组合握手的零等待二进制信号量探测会运行尚未停止的 UART RX 任务，使确定性替身进入循环。 | 零等待只探测；阻塞握手由显式 hook 推进控制入口，UART 退出仍由原任务路径确认。UART 原有测试与组合测试全部重跑通过。 |

未发现需修改固件并发或持久化协议的回归；新增测试不能证明硬件调度、断电持久性或电气停机。

## 板上仍需完成的记录

本环境没有已确认接入的目标板，未烧录、未驱动电机。板上实验应使用同一提交、ESP-IDF/配置、固定输入脚本，保留原始 USB 日志与逻辑分析仪轨迹；协议 UART 不写诊断日志。

| 量 | 测量方法与场景 | 当前结果 |
| --- | --- | --- |
| 最小空闲堆、峰值在用堆、最大可分配块 | 板上记录启动前、网络全部就绪、4 WS+UART 最大上传/洪泛/慢客户端/扫描、NVS 写入和反复断连重连后的 internal/8bit 空闲值、minimum free 和 largest free block。不同 capability 有重叠，不能直接相加。峰值算法须标明起点；启动后的采样差值不能代替全生命周期低水位。 | 未测 |
| 各任务栈高水位 | 在上述负载后分别记录 gs_control/adc/uart_rx/eth/sta/store/HTTPD 和系统任务的真实高水位，记录 SDK 对返回单位的定义。分配的栈字节数不是实际栈余量。 | 未测 |
| 实际调度迟到与控制执行时间 | 板上用单调微秒时间或可用 GPIO 标记，分别记录计划周期到开始的迟到、开始到结束耗时、分布及最大值；包括 Wi-Fi/ETH 中断、Flash 提交和共享 SPI 最长事务。20ms 是计划周期，不是测得的上限。诊断应写固定长度环形缓冲，停机后导出。 | 未测 |
| S 接收至输出停止 | 4 WS+UART 满队列与网络积压时，用同一逻辑分析仪记录完整 S 的 CR 到 595 锁存方向全零/PWM=0，区分前方未接收的输入、软件队列、任务调度与物理惯性。 | 未测 |
| SDK 任务 WDT/控制任务停止调度 | 仅在断开机械负载的安全台架注入任务阻塞，验证实际 WDT 行为、复位原因和启动安全锁存；它与应用会话超时分开记录。当前默认 SDK task WDT 不代表输出失调度后必然立即物理断电。 | 未测 |
| 真实 NVS 故障/断电、SPI 电气失败 | 台架注入并核验重启后的旧/新完整记录、输出未知诊断、595/PWM/继电器电平及机械响应；主机错误返回替身不能替代掉电试验。 | 未测 |

关闭硬件验收前须保存这些实测结果，并据控制箱速度、换向等待、反馈期限和机械停机要求确定延迟与余量门槛；主机测试耗时、逻辑推进时间、ELF 大小均不得填入这些结果列。
