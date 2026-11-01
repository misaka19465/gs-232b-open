# WebSocket GS 传输适配

入口见 [`esp_ws.h`](../include/gs232b/esp_ws.h)。基线为锁定的
`espressif32@8.0.0` / ESP-IDF 6.1.0；服务监听 TCP 80 的 `/ws/gs232b`，
ETH 和 STA 合计最多四个控制连接。`app_main` 仍是构建基线，板级启动组合由调用方接入。

## 接入与任务边界

1. 静态分配、清零 `gs232b_esp_ws_t`，先取得 `gs232b_esp_ws_io`，用于初始化唯一 dispatcher。
   与 UART 共存时由应用的 `io` 路由按完整会话键分派；不得按 fd 或 transport 广播。
2. 用 `gs232b_esp_ws_open` 启动 HTTPD。控制任务通过 `gs232b_esp_ws_interface`
   发布 ETH/STA 的当前 IPv4（主机序）、`esp_netif_get_netif_impl_name` 得到的 lwIP
   接口名和对应生命周期快照的非零 `session_epoch`。不可用时发布地址 0 和 NULL 名称；
   地址、epoch 或接口名变化仅撤销该接口的旧连接。
3. 同一控制任务持续运行 `gs232b_esp_ws_service` 和 `gs232b_dispatcher_tick`。
   service 先关闭失效会话，再轮转至多一个 256 字节 RX 包，且至多保留一个 HTTPD pump。
   控制任务所有锁和 work queue 均零等待，输入包在 HTTPD 入队前盖完整会话键和 epoch。
4. 停止阶段调用 `gs232b_esp_ws_close`；它先关闭 dispatcher 会话，再同步停止 HTTPD。
   `httpd_stop` 失败保留资源供重试；成功后方可释放 adapter/dispatcher。

HTTPD 回调只接收、排队、编码和发送，不运行 dispatcher、运动或持久化操作。
pre-handshake 检查本地 socket 地址；锁定 SDK 默认 IPv6 dual-stack listener 时只接受
能映射到已发布 IPv4 的 `::ffff:a.b.c.d` 目标，未发布的地址（包括管理 SoftAP）拒绝接入。
同一个 IPv4 同时发布给两个接口时拒绝新连接，避免错误归属。识别出接口后，在返回 Upgrade
响应前对已接受 socket 设置 `SO_BINDTODEVICE`，使用 `esp_netif_get_netif_impl_name` 提供的
lwIP 接口名固定该 TCP 会话的后续出口；名称不合法或绑定失败时拒绝握手。记录同时保存入站
transport、当时的本机 IPv4 和 lifecycle epoch，后续变更只关闭对应接口的会话。

ETH netif 路由优先级为 150，STA 为 100，因此两者都可用时 ETH 是默认路由；ETH 不可用时
lwIP 可回退到 STA。优先级影响默认路由选择，不会迁移既有 TCP 连接；已建立的 WebSocket
通过 socket 绑定保持原接口。双网同网段时的真实回包出口、ARP 选择及 `SO_BINDTODEVICE`
在目标板上的实际效果仍列为板级网络验证项。

锁定 SDK 在 Upgrade 后**不调用普通 GET handler**。必须开启
`CONFIG_HTTPD_WS_PRE_HANDSHAKE_CB_SUPPORT` 和 `CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT`：
post hook 最多等待控制任务 100ms 建立全新会话，将一次 `Connect OK\r\n` 放入 TX FIFO。
客户端在 Upgrade 后立即发来的命令保留在 socket 中，随后读取；欢迎语始终先于该会话命令响应。
握手或资源失败关闭新连接，不驱逐活动控制者。URI 上下文借用 adapter，不交给 SDK 默认 `free`。

## 帧与字节流

- Text 和 Binary 的 payload 都按原始字节交给已有 GS 解析器；只以 CR 提交命令，不追加 CR、NUL、JSON 或日志。
- 命令可跨多个 WS 消息，消息也可包含多个命令；CRLF 容错完全沿用已有解析器。
- 保存每连接的 continuation 状态及消息累计长度。无起始的 continuation、未结束时另起数据消息、未知 opcode、
  非 FIN 控制帧及超过 125 字节的控制帧以 1002 关闭；单帧或累计消息超过 16384 字节以 1009 关闭。
- 通过 `httpd_ws_recv_frame_part` 的 `left_len` 每次最多读 256 字节，最多 64 个 RX 包；不为帧长动态分配缓冲。
  16KiB 恰好可接纳；16384 字节 GS 行的 CR 可放到下一条消息。最大规范序列含 CR 为 15205 字节。
  超过 GS 行长度仍由 GS 解析器返回一次协议错误，不把该行可提交前缀当命令。
- Ping 原样排入 Pong 控制响应；Pong 完整消费但绝不写入 RX FIFO，不影响半行、命令边界、所有权或应用期限。
  控制帧可穿插分片消息，空数据帧和空控制帧只探测一次帧头，避免 SDK 的 `len=0` 再读取陷阱。

## 有界发送与失联

每连接有 4096 字节应用 TX FIFO、四个最多 125 字节的控制帧槽，以及一个最多 266 字节的 wire 缓冲。
`io.send` 原子复制全部字节，空间不足不接纳前缀。dispatcher 在发送/响应资源耗尽时关闭来源会话。
GS 输出统一编码为完整 Binary 帧，按最多 256 字节拆块；帧边界不改变 GS 响应字节和顺序，帮助正文亦可拆块。

锁定 SDK 的 `httpd_ws_send_frame_async` 实际同步调用 send override，且只判断负返回值，不能把 socket 短写当完整发送。
本适配器让 override 只把 SDK 生成的头和 payload 复制进 wire，再由 HTTPD pump 每连接每轮调用一次
`send(..., MSG_DONTWAIT)`，保存偏移并继续短写；EAGAIN/EWOULDBLOCK/EINTR 留待后续轮次，不忙等。
单帧/等待调度的发送期限为 2 秒，超时、读写故障、队列溢出、Close 或物理断线都会通知控制任务清会话。
可完整发送 Close 时尽力发一次；已发出半个帧时直接断线，避免把 Close 插入帧体。

每 5 秒生成带序号的 Ping，仅对应序号的 Pong 更新传输活性，15 秒无匹配 Pong 关闭。
TCP keepalive 设置 5 秒空闲、5 秒间隔、3 次探测。控制任务独立检查发送及 Pong 期限，
即使 HTTPD 正在慢接收、pump 无法执行，也先同步停止失联控制者、清序列/校准并推进 epoch。
关闭请求在 HTTPD 中执行 `shutdown`，由 SDK 随后的 EOF/错误路径释放 fd；避免异步排队的 SDK 内部 socket 槽指针因复用误关新连接。
free hook 发布物理关闭；控制任务撤销旧完整会话键后才回收槽，重连不重放旧指令或 TX。

应用看门狗默认关闭。dispatcher 的 `watchdog_set` 接受 0 或 1000..600000ms，只有当前控制者可设置；
成功的完整命令或显式 `watchdog_ping` 续期，旁路查询、非法命令、周期校准显示和 WS Pong 均不续期。
tick 在执行普通请求前检查到期；迟到的应用心跳也不能复活已过期控制者。此改动提供控制任务 API，
不扩展 GS 字节协议，`!SESSION ...` 文本命令仍不由官方 GS dispatcher 接纳。

## 验证

`python3 scripts/test.py` 构建所有固件验收环境并运行主机测试；新增 `websocket-api-check`
强制保留真实 HTTPD、WS 分块接收、握手 hooks、send override 及 ETH/STA 路径，并检查锁定版本、Kconfig 和 ELF 符号。
已有生成的 sdkconfig 若仍关闭 WS，需在 menuconfig 启用上述三项或移走该环境生成的 sdkconfig 后按 defaults 重新生成。

`esp_ws_tests` 用带掩码的 RFC wire 帧和 SDK 行为替身，接入真实 dispatcher/运动/序列核心，覆盖：
跨消息和多命令、分片穿插非空/空 Ping、Pong 隔离、零长帧、非法分片、16KiB 正反边界与累计长度、
15205 字节最大序列、GS 行超长恢复、1 字节短写、EAGAIN、EPIPE、TX/RX 边界、HTTPD 调度延迟、
四连接上限、socket 接口绑定、ETH/STA 同时在线、地址变化、ETH 失效/恢复导致的默认路由切换、
接口局部失效、重连 generation/epoch 隔离、非控制会话断开保留活动控制者、握手/分配/SDK 失败
清理及应用看门狗。
这些是确定性的主机行为测试及真实 SDK 编译链接验证，不代表已完成硬件网络、堆余量或双网同子网出口实测。
