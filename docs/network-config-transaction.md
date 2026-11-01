# 网络配置试运行事务

Dispatcher 内置 `!CFG BEGIN`、`!CFG CHECK`、`!CFG STATUS`、`!CFG APPLY`、`!CFG CONFIRM`、`!CFG ABORT`、`!NET SET` 和 `!NET GET`。BEGIN/SET/CHECK 只在 RAM 中编辑 candidate；APPLY 启动 30 至 600 秒试运行，不改变已确认 active revision。CONFIRM 将完整 Ethernet/Wi-Fi 配置和确认的 trial ID 以新的 revision 原子写入 NVS；ABORT 或超时恢复完整 active。

启动代码可在打开第一个会话前调用 `gs232b_dispatcher_network_configure`，传入已确认配置、revision 和可选的只读运行快照端口，并调用 `gs232b_dispatcher_network_apply_configure` 提供完整配置应用端口。未装载时 active 默认为 Ethernet DHCP、Wi-Fi 禁用。配置加载和网络适配器启动仍由应用启动流程负责。

只有 UART1 和 SoftAP 管理会话可改配置；ETH/STA 会话在管理认证接入前收到 `NOT_AUTHORIZED`。查询可从任何会话执行。事务由完整的 session ID/generation 持有，发起会话关闭时丢弃；成功的 `NET SET` 和 `CFG CHECK` 将 60 秒空闲期限续期，读命令、非法修改和校验失败不续期。事务状态不持有运动锁，GS 状态查询和 `S` 仍按正常控制路径工作。

```text
!CFG BEGIN
!NET SET {"eth":{"mode":"static","address":"192.168.10.50","netmask":"255.255.255.0","gateway":"192.168.10.1"},"wifi":{"enabled":true,"ssid":"radio","password":"correct horse"}}
!CFG CHECK
!CFG APPLY {"candidate_id":1,"revision":7,"timeout_s":60}
!CFG STATUS
!CFG CONFIRM {"trial_id":1}
!NET GET
!CFG ABORT {"trial_id":1}
```

`CFG APPLY` 仅在运动、序列、校准和输入保留均空闲时接纳；`candidate_id`、可选的 `revision` 和期限必须匹配当前候选。trial ID 跨重启单调递增，确认不绑定 BEGIN 的会话，因此可由新的授权 UART1 或 SoftAP 管理会话继续。已经确认的同一 ID 重复 CONFIRM 返回成功；活动试运行中的旧 ID 返回冲突。

掉电恢复依赖网络域双槽记录和独立 trial ID 记录：写入中断时启动读取最近完整 active；试运行本身不作为 active 持久化，重启后仍从已确认 active 启动。应用或回滚端口返回失败会保留 `rollback_pending`，后续 dispatcher tick 继续恢复旧 active。

`NET SET` 合并给出的字段，省略值保留 candidate 原值。ETH mode 为 `dhcp` 或 `static`；IPv4 使用严格四段十进制，不接受前导零。静态配置要求单播主机、连续 `/1` 至 `/30` 掩码和同子网的不同网关；DNS 可省略或设为 `0.0.0.0`。切到 DHCP 时保留候选中的静态地址字段，以便继续编辑；DHCP 有效配置比较和实际运行快照不使用这些保留字段。

Wi-Fi security 固定为 `wpa2-personal`。SSID 按解码后的 UTF-8 字节计数，范围 1 至 32；禁用 Wi-Fi 时可同时清空 SSID 和密码。口令接受 8 至 63 个可打印 ASCII 字节，或 64 个十六进制字符 PSK。字符串不得含 NUL；纯星号、`redacted`、`[redacted]` 和 `<redacted>` 不作为新密码接受。错误 patch 不修改 candidate。

`NET GET` 的 active 和 candidate 都只提供 `password_set` 布尔值，不返回口令或可回写的脱敏字符串。若调用方未装载运行状态端口，`runtime` 为 `null`；运行快照由端口提供实际接口状态，不从 candidate 推导。接口状态用 `link_down`、`link_up_no_ip`、`dhcp_wait`、`ready`、`error`（ETH）及 `disabled`、`starting`、`connecting`、`dhcp_wait`、`ready`、`retry_wait`、`error`（Wi-Fi）表示。
