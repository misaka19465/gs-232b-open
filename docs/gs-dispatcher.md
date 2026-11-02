# 官方 GS 命令纵向集成

入口为 [`gs_dispatcher.h`](../include/gs232b/gs_dispatcher.h)。dispatcher 将增量字节解析、会话所有权、真实运动核心、共用序列、交互校准、显示设置持久化和响应编码连接成完整链路，不使用测试执行端。所有 API 与运动、存储和传输回调必须串行调用；回调有界且不得重入。

## 接入

1. 启动流程初始化唯一的输出管理器、采样端口和运动核心，将已保存的零点加载到运动配置。调用 `gs232b_dispatcher_init`，提供原子分域存储、会话 IO 和至少 2KiB 的独占请求池。dispatcher 使用运动核心的单调时钟，加载显示域；没有记录时使用 P45/N Center，其他加载错误拒绝初始化。
2. UART 波特率匹配/回退完成或 WebSocket 协议入口就绪后调用 `gs232b_dispatcher_session_open`。每个会话提供至少 2KiB 的行工作区，保存返回的完整 `session_id + generation`。新会话只收到一次 `Connect OK\r\n`。
3. 字节进入任何上游缓冲前调用 `gs232b_dispatcher_capture`，将上下文与 RX 数据一起保存；分成最多 256 字节的小块调用 `gs232b_dispatcher_feed`，块间让出调度。周期调用 `gs232b_dispatcher_tick`，不可等待目标到位或阻塞收完整长行。
4. 连接关闭、链路失效或 IO 故障时调用 `gs232b_dispatcher_session_close`。控制者关闭时同步全停、清任务并撤权。dispatcher 内部的 `control` 只用于只读快照，不绕过门面修改其状态。

`io.send` 必须按完整会话键校验目标，将整个字节范围原子复制到有界 FIFO，不自动补行尾、记录日志或广播。发送失败会在本次 feed/tick 返回前关闭该会话；控制者失败同时全停。平台入口见 [UART 适配](uart-transport.md)与 [WebSocket 适配](websocket-transport.md)；`app_main` 已通过统一控制任务接入 dispatcher；网络失败不会阻塞 UART。

## 命令路由与持久化

| 命令 | 实际处理路径 |
| --- | --- |
| `R/L/U/D` | 检查所有权后调用真实 `motion_manual`，只改变指定轴；成功后清序列。 |
| `A/E/S` | 独立停止槽优先执行，作废旧写请求；A/E 保留另一轴，S 全停并撤权。 |
| 单点/长格式 `M/W` | 控制核心全量校验、坐标编译、原子接纳首点、提交共用序列；后续目标由同一运动核心执行。 |
| 裸 `M/W` | 输入预约与执行权限检查通过后清序列、停止两轴，返回 `?>\r\n`；非控制者错误不改变他人任务。 |
| `X1/X2/X3/X4` | 经 `motion_set_speed_level` 读取固件四档参数，默认 AZ PWM 为 250/500/750/1000‰；定位进入 15° 减速区时使用当前档与近目标档的较小值。参数接口及实际转速/电压确认边界见 [LEDC 调速](ledc-speed.md)，四档实际转速仍需实机验证。 |
| `C/B/C2` | 读取真实快照，校验所用轴的采样与位置，按 P36/P45 和中心映射；禁用 EL 填 000，无效反馈返回错误。 |
| `T/N` | 控制核心的绝对截止时间与已下发点序号，不另设计时器。 |
| `H/H2/H3` | 使用既有固定帮助编码；H3 读取当前模式与保存的中心。 |
| `P36/P45/Z` | 检查停止、序列计时及校准状态，沿用版本化显示域原子保存；保存成功后应用 RAM 并清序列。重复模式及 P45 下 Z 不写 Flash，也不清序列。 |
| `O/O2/F/F2` | 共用控制核心的会话隔离校准；确认瞬间采样后保存零点，持续显示按 500ms 推进，不保存满量程。 |

`!` 扩展命令使用独立注册表、JSON schema 和 handler；未注册、非法字段及权限/状态失败均只返回一行扩展 JSON，不混用 `?>`。注册 handler 的 schema 和上下文由调用方持有，入口接纳及执行前都会调用无副作用检查，handler 仅在完整校验通过后执行。扩展请求与 GS 响应按原队列顺序发送，且不取得 GS 控制权。存储沿用既有格式与 revision，不引入兼容桥或新记录格式。

## 响应顺序

每个会话有 16 个固定响应占位，保存命令标识及响应值，不复制长上传、帮助全文或其他会话的数据。完整行和解析错误先登记占位，执行完成后填充正文；只发送已经就绪的连续前缀。因此解析、参数和权限错误不会越过此前查询。

停止动作优先于普通请求执行。控制核心通过 `submitted / discarded` 生命周期回调关联响应：失效写请求静默跳过，已接纳查询保留，在停机完成后读取状态并回复；合并的 A/E/S 只填充对应来源会话的停止占位。这样 `C\rS\rN\r` 先完成停机，再按 `AZ=...\r\n`、`\r`、`=0000=0000\r\n` 的顺序返回。校准周期帧与错误也进入发起会话的同一响应队列。

响应占位或 TX FIFO 耗尽时关闭来源会话；已经锁存的停止仍执行，不依赖普通请求队列或响应资源。关闭/复用槽位会清响应占位，旧 generation 不会向新连接发送结果。

## 第 3.1 节验收映射

全部测试使用真实 dispatcher、控制/序列/校准/运动/坐标/持久化/响应模块，仅 IO、存储介质和机械负载为测试端口，断言最终 TX 字节及实际动作状态。

| 范围 | 端到端用例 |
| --- | --- |
| `R/L/A/U/D/E/S`、双轴并行、停止优先 | [`gs_commands_tests.c`](../tests/host/gs_commands_tests.c)、[`gs_sessions_tests.c`](../tests/host/gs_sessions_tests.c) |
| 单点 `M/W`、`X1`～`X4`、15° 减速 | [`gs_commands_tests.c`](../tests/host/gs_commands_tests.c) |
| `C/B/C2/N`、三/四位字段、失效/禁用轴 | [`gs_commands_tests.c`](../tests/host/gs_commands_tests.c)、[`gs_sequences_tests.c`](../tests/host/gs_sequences_tests.c) |
| `H/H2/H3` 全文、`P36/P45/Z` 映射/重启恢复/保存失败 | [`gs_sessions_tests.c`](../tests/host/gs_sessions_tests.c)、[`gs_commands_tests.c`](../tests/host/gs_commands_tests.c) |
| 最大 3800 点/1900 对上传、首点、T/N、M/W 混用、裸 M/W 鉴权与副作用 | [`gs_sequences_tests.c`](../tests/host/gs_sequences_tests.c) |
| `O/O2`、同会话 Y/YES、确认瞬间采样、取消、保存失败 | [`gs_calibration_tests.c`](../tests/host/gs_calibration_tests.c) |
| `F/F2` 首帧/500ms/不补帧/CR 退出、不保存满量程 | [`gs_calibration_tests.c`](../tests/host/gs_calibration_tests.c) |
| 大小写、分片、CRLF、不回显、错误与连接提示、来源隔离、响应顺序/TX 故障 | [`gs_commands_tests.c`](../tests/host/gs_commands_tests.c)、[`gs_sessions_tests.c`](../tests/host/gs_sessions_tests.c) |

运行 `python3 scripts/test.py --host-only` 和 `pio run -e esp32-c3-wroom-02u-n4`。原有模块边界测试继续覆盖全部 1～999 秒间隔、序列超期、epoch 耗尽及存储中断等边界；板级时序、真实网络与机械停机需另外实测。
