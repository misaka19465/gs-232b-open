# Wi-Fi STA 生命周期

`gs232b_esp_wifi_sta_t` 提供独立的 STA DHCP/静态 IPv4 netif、RAM 凭据应用、异步 SDK
生命周期、有界指数退避和脱敏状态。入口见 [`esp_wifi_sta.h`](../include/gs232b/esp_wifi_sta.h)，
配置/状态契约见 [`wifi_sta.h`](../include/gs232b/wifi_sta.h)。同一无线电 owner 也管理配网
SoftAP；应用和 dispatcher 负责持久化及连接试运行事务。

## 配置与凭据

- `open(adapter, config)` 接纳完整配置；NULL 表示禁用且无凭据。
- SSID 为 1–32 个 UTF-8 字节；非空密码为 8–63 字节口令或 64 位十六进制 PSK，空密码表示
  开放网络。拒绝未终止字符串、畸形 UTF-8、短密码及非十六进制的 64 字节密码。
- WPA2 凭据使用 `WIFI_MODE_STA` 和 WPA2 安全阈值；开放网络使用 `WIFI_AUTH_OPEN`。
  配网 SoftAP 由同一 owner 以 `WIFI_MODE_APSTA` 管理。
- `mode=static` 时关闭 STA DHCP 并应用独立 IPv4 地址、掩码和可选网关；静态模式不设 DNS。
  `mode=dhcp` 使用 DHCP，动态租约可报告 DNS。Ethernet 地址保持独立。
- `esp_wifi_init` 关闭 Wi-Fi NVS，`esp_wifi_set_storage(WIFI_STORAGE_RAM)` 先于
  `set_config`。重连、禁用及失败均不擦除保存的凭据，不调用 restore 或 NVS 擦除。
- `configure` 应用完整候选；`set_enabled(false)` 保留原 SSID/密码，随后可直接重新启用。
  配置存储、已确认/试运行/回滚由上层负责；网络 schema 3 持久化 Wi-Fi IPv4，旧 schema
  1/2 读取时默认 STA DHCP。
- 快照仅含 SSID 与 `password_set`，不含密码；模块不打印凭据或 SDK 配置对象。

## 状态与重试

状态区分 `DISABLED / STARTING / CONNECTING / DHCP_WAIT / READY / RETRY_WAIT / ERROR`。
DHCP 关联成功后等待本 netif 的有效 GOT_IP；静态模式在关联后读取 netif IPv4，校验成功后进入
READY。
快照包含独立的 MAC、IP、掩码、网关、DNS、关联/IPv4 有效性和启用状态。

失败原因区分认证、找不到 AP、掉线、连接超时、DHCP 超时和 SDK 错误；同时保留原始
`disconnect_reason`、`sdk_error`、`last_error`、连续失败次数、下次重试时间与退避间隔。
认证/握手原因不意味着可以确定密码错误，调用方应展示原始原因供诊断。

退避为 1/2/4/8/16/30 秒，随后持续保持 30 秒上限；没有次数上限，不清凭据。
只有 READY 或显式重配置重置失败计数，关联成功本身不重置。连接与 DHCP 获取分别有
30 秒超时。`service` 到达 deadline 后才发起下一次异步尝试，不在事件回调中重连。

每次尝试先停止并重建 STA 驱动/netif，以清除旧租约和隔离没有 netif 标识的 Wi-Fi 事件。
停止后排空默认事件循环，再释放驱动与 netif；销毁后再次屏障。SDK 操作失败进入退避，
释放失败保留资源供下一次清理重试；禁用失败进入 ERROR，需显式重试配置或关闭。

`session_epoch` 从 1 开始；有效 IPv4 丢失、地址/掩码/网关变化、禁用、重配置或关闭时
撤销旧 STA 会话并递增 epoch。即使两次轮询间已经重连，上层也必须先关闭旧 STA 会话、
撤销相应控制权，再接纳新输入。DNS 更新不撤销会话。epoch 饱和后保持 ERROR，须关闭重建。
LOST_IP 到达时若当前 netif 已有租约，则忽略旧通知。

## 接入与接口隔离

1. 用清零且不会移动/复制的实例调用 `open`；之后周期调用非阻塞 `service`。
2. `open/configure/set_enabled` 返回 OK 仅代表请求接纳；通过 service/snapshot 确认结果。
   工作进行中重配置返回 BUSY，锁竞争返回 WOULD_BLOCK。
3. 所有公开 API 由同一管理任务串行调用，不在默认事件回调重入；SDK 停启/排空由独立
   `gs_sta` 任务执行，不阻塞运动循环。快照是零等待复制，竞争时输出清零。
4. `close` 异步取消重试并释放 STA；返回 WOULD_BLOCK 时继续调用，其他错误保留实例可重试。
   成功后才可释放实例；失败的 open 若仍保留 `opened`，同样必须 close。

模块独占 Wi-Fi 驱动，并拒绝同时打开第二个实例。STA 和配网 SoftAP 共用此统一无线电
owner；不能从其他管理器启动或停止其中一个接口。

STA key 为 `GS232B_STA`，路由优先级 100，低于本项目 Ethernet 的 150。
不引用 Ethernet 生命周期，不停用其驱动、不触及 W5500/SPI/复位或删除全局事件循环。
STA 成功、失败、禁用和重配置均不关闭 Ethernet。WebSocket 已建立会话通过 `SO_BINDTODEVICE`
固定到入站 netif；路由优先级不迁移已有 TCP 会话，同网段双网响应出口、ARP 和绑定效果
仍须实机验证，不由本次宿主机仿真宣称通过。

## 验证

运行 `python3 scripts/test.py`，同时构建 ESP32-C3 默认、UART、Ethernet 与 STA API 链接目标，
检查锁定 ESP-IDF 6.1.0 的真实 Wi-Fi、Ethernet、netif、DHCP 路径均保留在 ELF 中，随后执行
完整宿主机测试。只跑仿真可用 `python3 scripts/test.py --host-only`。

STA 仿真直接编译真实平台模块，覆盖错密码/握手失败、找不到 AP、掉线、重试截止时间与
30 秒封顶、连接/DHCP 超时、租约更新/丢失/迟到事件、禁用/重新启用/重配置、旧事件隔离、
SDK/分配/清理失败重试和关闭时取消。共存测试同时启动真实 Ethernet/STA 生命周期及两个
netif，在同网段租约下验证 STA 多次掉线/禁用/重配置/关闭后 Ethernet netif、IP、epoch、
驱动 stop 次数和 SPI 访问门保持不变。目标构建验证 API/链接，不替代 AP/RF/DHCP 实机测试。
