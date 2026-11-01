# 候选网络配置事务

Dispatcher 内置 `!CFG BEGIN`、`!CFG CHECK`、`!CFG STATUS`、`!CFG ABORT`、`!NET SET` 和 `!NET GET`。当前事务只在 RAM 中修改 candidate；没有 APPLY，因而不改运行网络、不写 NVS，也不改变 active revision。

启动代码可在打开第一个会话前调用 `gs232b_dispatcher_network_configure`，传入已确认配置、revision 和可选的只读运行快照端口。未装载时 active 默认为 Ethernet DHCP、Wi-Fi 禁用。配置加载和网络适配器启动仍由应用启动流程负责。

只有 UART1 和 SoftAP 管理会话可改配置；ETH/STA 会话在管理认证接入前收到 `NOT_AUTHORIZED`。查询可从任何会话执行。事务由完整的 session ID/generation 持有，发起会话关闭时丢弃；成功的 `NET SET` 和 `CFG CHECK` 将 60 秒空闲期限续期，读命令、非法修改和校验失败不续期。事务状态不持有运动锁，GS 状态查询和 `S` 仍按正常控制路径工作。

```text
!CFG BEGIN
!NET SET {"eth":{"mode":"static","address":"192.168.10.50","netmask":"255.255.255.0","gateway":"192.168.10.1"},"wifi":{"enabled":true,"ssid":"radio","password":"correct horse"}}
!CFG CHECK
!NET GET
!CFG ABORT
```

`NET SET` 合并给出的字段，省略值保留 candidate 原值。ETH mode 为 `dhcp` 或 `static`；IPv4 使用严格四段十进制，不接受前导零。静态配置要求单播主机、连续 `/1` 至 `/30` 掩码和同子网的不同网关；DNS 可省略或设为 `0.0.0.0`。切到 DHCP 时保留候选中的静态地址字段，以便继续编辑；DHCP 有效配置比较和实际运行快照不使用这些保留字段。

Wi-Fi security 固定为 `wpa2-personal`。SSID 按解码后的 UTF-8 字节计数，范围 1 至 32；禁用 Wi-Fi 时可同时清空 SSID 和密码。口令接受 8 至 63 个可打印 ASCII 字节，或 64 个十六进制字符 PSK。字符串不得含 NUL；纯星号、`redacted`、`[redacted]` 和 `<redacted>` 不作为新密码接受。错误 patch 不修改 candidate。

`NET GET` 的 active 和 candidate 都只提供 `password_set` 布尔值，不返回口令或可回写的脱敏字符串。若调用方未装载运行状态端口，`runtime` 为 `null`；运行快照由端口提供实际接口状态，不从 candidate 推导。接口状态用 `link_down`、`link_up_no_ip`、`dhcp_wait`、`ready`、`error`（ETH）及 `disabled`、`starting`、`connecting`、`dhcp_wait`、`ready`、`retry_wait`、`error`（Wi-Fi）表示。
