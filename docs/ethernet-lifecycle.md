# W5500 Ethernet 生命周期

`gs232b_esp_ethernet_t` 把 W5500、`esp_netif` 和 lwIP 的生命周期放在共享 SPI
复位协调器之后。应用先调用 `gs232b_esp_ethernet_open`，再周期调用
`gs232b_esp_ethernet_service`；`recover` 和 `configure` 都是非阻塞请求，只有
快照显示 `GS232B_ETHERNET_READY` 才表示接口已经有可用 IPv4。

入口见 [`esp_ethernet.h`](../include/gs232b/esp_ethernet.h)，配置、状态及 IPv4
校验见 [`ethernet.h`](../include/gs232b/ethernet.h)。本模块作为应用组装接口交付，
`app_main` 仍保留构建基线；WebSocket、Wi-Fi、管理命令、配置试运行确认和持久化
格式由上层负责。

| 状态 | 含义 |
| --- | --- |
| `LINK_DOWN` | 物理链路未建立或接口正在恢复。 |
| `LINK_UP_NO_IP` | 链路已建立，但尚无地址且 DHCP 未进入运行状态。 |
| `DHCP_WAIT` | DHCP client 运行中，等待租约。 |
| `READY` | 当前链路已建立且 IPv4 有效，静态或 DHCP 均可进入。 |
| `ERROR` | 驱动、地址读取或配置失败，或 DHCP 等待超时。 |

链路事件只改变链路状态，不代表已经获得地址。DHCP 租约、续租和丢失通过
`IP_EVENT_ETH_GOT_IP` / `IP_EVENT_ETH_LOST_IP` 更新当前运行地址。租约的地址、掩码或
网关变化会递增 `session_epoch`，上层应撤销该 Ethernet 接口上的旧会话和运动所有权；
延迟到达的旧 LOST_IP 事件在当前 netif 已有新地址时会被忽略。

MAC 使用 `esp_read_mac(..., ESP_MAC_ETH)`，由芯片基准 MAC 的 Ethernet 派生分配，
然后在每次驱动重建时重新写入 W5500。它不依赖 W5500 模块寄存器中的预置值。

静态模式建立不带 `ESP_NETIF_DHCP_CLIENT` 的新 netif，并在应用静态地址前停止 DHCP
client。切回 DHCP 时先销毁旧 netif、清除运行地址和 DNS，再建立带 DHCP client 的新
netif；保存的配置和当前租约始终是两个字段。DNS 为可选项，IDF 6.1 拒绝通过
`esp_netif_set_dns_info` 写入零地址，因此空 DNS 由 netif 重建清除。

网络驱动的停用、探测、安装和卸载都在网络工作任务执行。复位协调器只通过输出管理器
提交完整 shadow byte 来改变 bit4；它保留当前方向与 PWM，请求停止时也不会被恢复流程
重新启用。输出提交失败会使状态保持未知，必须由上层处理输出故障并显式全停后重试。

主机测试覆盖无 IP 链路、DHCP 租约和变化、丢租约、重连、DHCP 超时、静态/DHCP 互斥、
MAC 稳定性、驱动失败和输出恢复；`scripts/test.py` 还会检查 ESP32-C3 / ESP-IDF
6.1.0 的 W5500、`esp_netif` 和 lwIP 符号都进入目标链接。

## 状态与调用顺序

快照的 `link_up`、`ipv4_valid`、`dhcp_running` 彼此独立。`configured` 表示被接纳的
目标配置；`current` 只在 IPv4 有效时包含运行地址、掩码、网关和 DNS。取得同一租约
或仅 DNS 更新不会使旧会话失效。上层须同时检查 `ipv4_valid` 和 `session_epoch`，
即使两次快照之间已经重连，也不能继续旧会话。代次不会回绕，耗尽后保持 ERROR，须
关闭并重新初始化。网络回调只更新网络状态，不直接调用 dispatcher 或运动核心。

1. 打开共享 SPI，并初始化唯一输出管理器提交四路停止、PWM=0、bit4 保持复位。
2. 用清零的 Ethernet 实例调用 `open(adapter, bus, outputs, config)`；传入 NULL 使用 DHCP。
3. 同一控制任务调用 `recover`，周期推进 `service`，同时处理运动 tick 和优先停止事件。
4. 消费状态快照，先处理失效的 ETH 会话与控制权，再处理新会话输入。
5. 用 `configure` 校验并接纳 DHCP 或静态配置；只有 READY 才确认运行地址可用。
6. 停机时反复调用 `close`，WOULD_BLOCK 表示等待工作任务；成功后才能释放共享 SPI。

`open` 复用全局 `esp_netif` 和默认事件循环，只释放自己的 handler、glue、netif、
MAC/PHY、驱动、队列和工作任务。`close` 不提交方向、PWM 或 bit4；释放错误保留资源，
可重试。stop 后先排空默认事件循环，再移除 glue；MAC 删除之后才销毁 netif，并通过
第二道事件屏障排空旧驱动间接发布的 IP 通知。

`configure` 校验连续 /1../30 掩码、非网络/广播的单播主机地址、同网段且不同于本机的
网关，并允许网关或 DNS 为零。IPv4 字段为主机序 32 位数，严格四段十进制解析不接受
缩写、十六进制、空白或前导零；DHCP 配置的 IPv4 字段必须全部为零。

DHCP 连续等待 30 秒后显示 ERROR/TIMEOUT，保持无 IP；lwIP 自身仍可重试并接纳迟到
租约，断链再接通会重新等待，且不会自动退到静态 IP。驱动/配置失败保留 ERROR 并
关闭共享总线访问门，需要显式重试；本层不擅自提交或回滚持久化事务。异步驱动操作
完成前若输出故障，迟到结果会被消费，事件接纳会被取消，不会永久停在 BUSY 或自动
恢复动作。

## 验收

```sh
python3 scripts/test.py
python3 scripts/test.py --host-only
```

`ethernet-api-check` 在空 `app_main` 下强制保留所有公开生命周期入口，检查真实
W5500、netif、lwIP DHCP、GPIO ISR 服务及输出管理器符号，并检查接口 DNS 配置。
`sdkconfig.defaults` 开启 `CONFIG_ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF`；如果本地旧
sdkconfig 显式关闭接口 DNS，需先更新该设置后重建。
接口路由优先级为 150，高于 SDK 默认 Wi-Fi STA 的 100；默认路由改变不迁移已有 TCP 连接。

软件验收不能替代实板测量。仍需验证真实 DHCP 续租/撤租、断网重连、MAC 跨重启稳定性
及 W5500 流量/复位期间方向输出和 PWM 的电气稳定性；双网上行同网段的回包出口、
地址冲突检测与配置事务回滚留在应用层验收。
