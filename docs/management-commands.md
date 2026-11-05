# 管理与配网

管理命令与[GS 控制命令](gs-232b-serial-protocol.md)共用串口或 WebSocket 字节流，以独立的 `!` 前缀区分。本文保留上位机接入和设备操作所需的规则。

## 格式与权限

请求为 `!DOMAIN ACTION [JSON对象]<CR>`，最长 2048 字节，不含 CR。命令名不区分 ASCII 大小写，JSON 键和值区分大小写；字符串使用标准 JSON 转义，禁止原始 CR/LF、NUL、重复键及未知字段。

每条完成请求返回一行 `!JSON\r\n`。错误包括 `INVALID_ARGUMENT`、`BUSY`、`NOT_OWNER`、`NOT_AUTHORIZED`、`NOT_READY`、`CONFLICT`、`TIMEOUT`、`HARDWARE_FAULT`；扩展不使用普通 GS 的 `?>`。停止作废未执行扩展时返回 `CONFLICT`，不执行其写入。

成功响应包含 `ok:true`、规范命令名 `cmd` 和结果对象 `data`；失败使用 `ok:false`、`error` 和可选 `detail`。例如：

```text
!{"ok":true,"cmd":"CFG BEGIN","data":{"candidate_id":1,"base_revision":7,"idle_timeout_s":60}}
!{"ok":false,"cmd":"CFG APPLY","error":"BUSY"}
```

| 入口 | 权限 |
| --- | --- |
| USB2 / UART1 | 本地管理，独占 ADMIN ENABLE/DISABLE 及 NET RESET |
| ETH/STA `/ws/gs232b` | GS 命令及扩展；网络写入须先启用管理并对本连接 SESSION AUTH |
| ETH/STA `/ws/manage` | 仅扩展；网络写入同样要求认证，拒绝普通 GS、校准和 M/W |
| SoftAP `/ws/manage` | 仅在活动配网窗口内放行 SYS INFO、NET GET/SET、CFG 全组、PROV STATUS/SCAN/STOP |
| SoftAP `/ws/gs232b` | 拒绝连接 |

LAN 查询及 GS 运动面向可信局域网；网络管理 token 和 AP 密码不授予运动所有权。AP 不能操作 OWNER、应用看门狗、MOTION STATUS、FAULT CLEAR、管理 token、网络重置或延长自身窗口。当前无 TLS/HTTPS，token 不提供传输加密。

## 命令速查

表中请求均需追加 CR；ID 和 revision 使用设备实际响应，示例数值不是固定值。

| 请求 | 用途 |
| --- | --- |
| `!SYS INFO` | 固件、板型、轴能力、schema 与入口可用性 |
| `!ROTOR GET` | 已选型号 ID、当前生效 ID、两者轴能力与是否需重启 |
| `!ROTOR SET {"model_id":5}` | 保存型号 ID；全停后设置，重启后切换配置 |
| `!OWNER GET` | 控制者、占用原因、运动/序列/交互状态；无人控制时 owner 为 null |
| `!OWNER TAKE` / `!OWNER RELEASE` | 空闲时申请 / 当前控制者在已知全停且无待运行任务时释放 |
| `!MOTION STATUS` | 两轴角度、反馈、方向、目标、限位、故障及运动就绪状态 |
| `!FAULT CLEAR` | 显式停机并重新检查反馈后清故障，不恢复旧动作 |
| `!SESSION WATCHDOG {"timeout_ms":10000}` | 当前控制者启用应用看门狗；0 关闭，其他值为 1000～600000ms |
| `!SESSION PING` | 当前控制者续期应用看门狗 |
| `!ADMIN ENABLE` / `!ADMIN DISABLE` | UART 启用或轮换 / 禁用网络管理 token |
| `!SESSION AUTH {"token":"..."}` | ETH/STA 当前连接认证；重连需重做 |
| `!NET GET` | 已确认 active、candidate、trial 和实际运行状态，密码仅报告是否已设 |
| `!CFG BEGIN` | 从 active 建立唯一候选，返回 candidate_id 和 base_revision |
| `!NET SET {...}` | 合并指定候选字段，省略字段保留 |
| `!CFG CHECK` | 校验完整候选，报告接口变更；不切网或保存 |
| `!CFG APPLY {"candidate_id":1,"revision":7,"timeout_s":60}` | 开始 30～600 秒试运行，revision 可省略；返回 trial_id |
| `!CFG STATUS` | 查询配置事务及期限，不续期 |
| `!CFG CONFIRM {"trial_id":1}` | 确认保存当前试运行；最近一次成功确认可重复调用 |
| `!CFG ABORT` / `!CFG ABORT {"trial_id":1}` | 发起者放弃自己的候选 / 回滚匹配试运行 |
| `!PROV START {"timeout_s":600}` | UART 或已授权 LAN 开配网窗口；省略参数默认 600 秒，最长 3600 秒 |
| `!PROV STATUS` | SSID、地址、窗口及恢复状态；只有 UART 响应含 AP 密码 |
| `!PROV SCAN` | 扫描附近网络；后续页使用 scan_id/cursor |
| `!PROV STOP` | 关闭 AP；活动或待回滚 trial 时返回 CONFLICT |
| `!NET RESET {"confirm":"NETWORK_ONLY"}` | UART 仅重置网络配置，见下文 |

### 旋转器型号

型号设置保存在独立记录域，与网络、零点、显示和管理认证互不覆盖；缺少型号记录时默认为 ID 1。ROTOR SET 只保存选择，不立即改动运动核心；成功响应中的 `restart_required` 为 true 时，重启设备后新型号生效。设置要求输出已知全停、无运行/待运行序列、上传或校准。UART 可直接设置；ETH/STA 必须启用 ADMIN 并认证当前会话；SoftAP 配网会话不可读取或更改型号。GET 为只读，不授予运动控制权。

代码和命令只传递数字 ID。下表的名称供人工查阅；每个 ID 在固件中均有独立的 `gs232b_motion_config_t`，目前配置仅按轴能力区别，零点仍为全局存储。

| ID | 旋转器 | 方位 | 仰角 |
| ---: | --- | :---: | :---: |
| 1（默认） | G-5500 | 是 | 是 |
| 2 | G-5400B | 是 | 是 |
| 3 | G-5600B | 是 | 是 |
| 4 | G-5500DC | 是 | 是 |
| 5 | G-400 | 是 | 否 |
| 6 | G-500A | 否 | 是 |
| 7 | G-550 | 否 | 是 |
| 8 | G-800DXA | 是 | 否 |
| 9 | G-1000DXA | 是 | 否 |
| 10 | G-2800DXA | 是 | 否 |
| 11 | G-800DXC | 是 | 否 |
| 12 | G-1000DXC | 是 | 否 |
| 13 | G-2800DXC | 是 | 否 |
| 14 | SDX 系列 | 是 | 否 |
| 15 | G-400 + G-500A | 是 | 是 |
| 16 | G-400 + G-550 | 是 | 是 |

GET 和 SET 成功响应使用相同字段：`model_id` 为已保存选择（无记录时为默认值），`active_model_id` 为本次启动加载的型号，`capabilities` 为当前运行轴能力，`configured_capabilities` 为已选型号的轴能力。再次设置同一已保存 ID 不重复写入；改回当前生效 ID 可取消待重启的型号变更。

例如从默认双轴配置保存方位单轴型号，各行追加 CR：

```text
!ROTOR SET {"model_id":5}
!ROTOR GET
```

响应正文 `data` 为：

```json
{"model_id":5,"active_model_id":1,"capabilities":{"azimuth":true,"elevation":true},"configured_capabilities":{"azimuth":true,"elevation":false},"restart_required":true}
```

重启后 `active_model_id` 变为 5、`capabilities.elevation` 为 false，`restart_required` 为 false。未知 ID、非整数及未知 JSON 字段返回 INVALID_ARGUMENT；忙时返回 BUSY。写入失败保留旧选择；启动读取优先恢复上一有效记录，所有记录均无效时保持全停并报告启动失败，不把损坏记录当作出厂默认。

G-500A/G-550 纯仰角配置可使用 U/D、B、E、S、O2；R/L、X1～X4、C/C2、M/W、O、F/F2 返回错误，A/S 停止及帮助、序号查询仍可用。纯方位配置继续沿用 GS 协议中禁用仰角时的既有规则。各型号共用零点和显示设置，型号切换不重新校准；更换实物后应重新检查对应轴零点。

### 控制权、故障与看门狗

OWNER TAKE 不抢占他人，RELEASE 要求全停且无待运行序列或交互；忙时可先发 S。FAULT CLEAR 在存在其他控制者时返回 NOT_OWNER，运动、待运行序列或交互期间返回 BUSY；故障条件仍在则保持锁定。

MOTION STATUS 的角度单位为毫度、电压为微伏，含有效性及 `motion_ready`；无效角度、目标或未知方向为 null。方向 0 为停止、1 为方位 CW / 仰角 UP、2 为方位 CCW / 仰角 DOWN。轴禁用、零点不足、反馈或限位无效、输出未知或存在锁定故障时，`motion_ready` 为 false。

空闲时无效反馈只报告 `position_valid=false` / `motion_ready=false`，不锁存 `FAULT_SAMPLE`；运动命令因位置未就绪返回错误且不提交方向输出。已有运动期间反馈失效仍会锁存故障并停机。

应用看门狗默认关闭。仅当前控制者成功的完整命令或 SESSION PING 续期；旁路查询、失败或不完整输入、周期校准显示与 WS Ping/Pong 均不续期。到期关闭来源、清任务并尝试全停；迟到 PING 不恢复控制者。低速长序列上传时应选择足够长的期限，或先停机关闭看门狗。

### 管理 token

ADMIN ENABLE 产生 256bit、64 个十六进制字符的随机 token，仅在本次 UART 响应返回；请当场保存。设备只持久化校验材料，SESSION AUTH 不回显 token。禁用或换 key 成功后撤销全部网络管理身份与对应未应用候选；失败保留旧凭据，已开始的 trial 按自己的期限继续。密码和 token 不写入 USB 日志，也不出现在普通状态查询中。

## 首次配网与恢复入口

未有已确认网络配置时，启动自动开启 600 秒 SoftAP；已有确认记录后，STA 连接失败不会自动重新开 AP。设备无用户可操作按钮，需要经 UART 或可达的已授权 LAN 开窗口：

```text
!PROV START
!PROV STATUS
```

AP SSID 为 `GS232B-SETUP-xxxxxx`，独立密码只能从 UART 的 PROV STATUS 或设备交付凭据取得。连接 AP 后打开 `http://<返回的AP地址>/`；页面内置、离线可用，不依赖 CDN 或 DNS 劫持。AP 通常为 `192.168.4.1/24`，若与 ETH/STA 地址冲突会选其他私网，以 STATUS 返回值为准。

网页选择网络、填写凭据后应用试运行；应等目标 STA 获得 IPv4，再手动确认保存，网页不会自动确认。AP 与 STA 共用无线电，信道切换可能短暂断开网页；重连可继续确认。AP 窗口到期或关闭不会取消独立的试运行期限，未确认配置仍按期回滚。

PROV START 要求两轴已知全停、没有待运行序列、上传或校准；窗口已开启时不延长原期限。AP 不能给自己重新开窗口。

扫描返回最多 64 个网络，按 RSSI 降序，每页 8 个。`next:8` 时发送 `!PROV SCAN {"scan_id":1,"cursor":8}`，直至 next 为 null。快照 30 秒后、新扫描开始或 AP 停止时失效；过期 ID 返回 CONFLICT，非法游标返回 INVALID_ARGUMENT。`truncated:true` 表示可手动输入未列出的 SSID。ETH/STA 扫描同样要求当前连接的网络管理认证。

扫描响应的 data 示例（仅发现一个网络）：

```json
{"scan_id":1,"offset":0,"total":1,"next":null,"truncated":false,"aps":[{"ssid":"radio","rssi":-45,"auth":3}]}
```

## 网络配置试运行

出厂状态为 ETH DHCP、STA 禁用。配置使用“候选 → 试运行 → 确认保存”；确认前旧 active 保留。BEGIN/SET/CHECK 只编辑候选，不影响运行；候选绑定发起连接，断线或 60 秒没有成功 SET/CHECK 后丢弃，查询和失败请求不续期。

APPLY 要求两轴无动作、已知全停，无已加载序列、上传或校准，并匹配 candidate_id 及可选 revision。试运行 ID 跨重启不复用；试运行不依赖原连接，新授权连接可凭 ID 确认。APPLY 响应只表示请求接纳，须从 NET GET 核对实际接口已取得有效地址，才能确认；仅连上物理链路或关联 AP 不等于有 IP。

超时、ABORT 或重启恢复旧 active。CONFIRM 成功才保存完整配置；写入失败不更新 active，回滚失败保留 `rollback_pending` 并继续恢复。新写入网络记录为 schema 4，已有 schema 1/2/3 仍可读取；旧记录继续按开放/WPA2 的安全策略恢复，schema 1/2 默认 STA DHCP。无需为了升级擦除零点或其他配置。

下面以 UART 设置 Wi-Fi 为例，各行发送 CR。将 SSID、密码及响应 ID 换为实际值：

```text
!CFG BEGIN
!NET SET {"wifi":{"enabled":true,"ssid":"YOUR_SSID","password":"YOUR_PASSWORD"}}
!CFG CHECK
!CFG APPLY {"candidate_id":1,"timeout_s":60}
!NET GET
!CFG CONFIRM {"trial_id":1}
```

改 ETH 固定地址或 STA 固定地址时，在各自事务内把 NET SET 替换为：

```text
!NET SET {"eth":{"mode":"static","address":"192.168.10.50","netmask":"255.255.255.0","gateway":"192.168.10.1","dns1":"192.168.10.1"}}
!NET SET {"wifi":{"mode":"static","address":"192.168.20.50","netmask":"255.255.255.0","gateway":"192.168.20.1"}}
```

| 配置字段 | 规则 |
| --- | --- |
| `eth` | mode 为 dhcp/static；静态支持 address、netmask、gateway、dns1、dns2 |
| `wifi` | enabled、ssid、password、可选 security，以及独立 mode/address/netmask/gateway；security 支持 `wpa-personal`、`wpa2-personal`、`wpa3-personal`（SAE）和 `auto`；开放网络可用 `open` 或空密码表示；不接受 DNS 字段 |
| IPv4 | 严格四段十进制，无前导零；单播主机，连续 /1～/30 掩码，非零网关须同子网且不同于本机；可省略网关或使用 0.0.0.0 |
| Wi-Fi SSID | 解码后 1～32 个 UTF-8 字节；ESP32-C3 仅支持 2.4GHz |
| Wi-Fi 密码 | WPA/WPA2 或 auto 为 8～63 个可打印 ASCII 字节或 64 位十六进制 PSK；WPA3 SAE 为 1～63 个可打印 ASCII 字节，不接受原始 PSK；空密码表示开放网络；不接受脱敏占位字符串 |
| 省略字段 | 保留候选原值；NET GET 仅报告 password_set，不返回可回写的密码占位值 |
| 禁用 Wi-Fi | enabled:false，可同时清空 SSID 和密码；不会修改 ETH 地址 |
| 切回 DHCP | 设置相应 mode:"dhcp"；候选保留静态字段供编辑，实际地址以运行状态为准 |

security 是管理接口的连接策略字段：`wpa-personal` 允许 WPA/WPA2/WPA3，`wpa2-personal` 允许 WPA2/WPA3，`wpa3-personal` 强制 SAE 与 PMF；同名 AP 优先更强认证，混合网络优先协商更强模式。配网页面无需选择认证方式：扫描网络时自动匹配，手动输入 SSID 时使用 `auto`，1～7 字节口令自动使用仅支持此长度的 WPA3 SAE。省略 security 保留候选策略，单独清空密码则切为开放网络。`open` 不能与非空口令组合；NET GET 的 config.security 是配置策略，不是已协商结果。不支持 WEP、OWE 或使用认证服务器的 Enterprise/EAP；配网热点自身继续使用 WPA2-Personal。

ETH/STA 地址配置相互独立；DHCP 不要求外网。保存配置与运行地址分别报告，不能把 candidate 或旧 DHCP 租约当作当前连接成功的证据。

## 仅重置网络

UART 发送以下命令前，须全停、无序列/上传/校准、其他控制者或配置事务：

```text
!NET RESET {"confirm":"NETWORK_ONLY"}
```

成功后恢复 ETH DHCP、禁用 STA、重新开启有界配网窗口。只删除网络当前及历史配置，保留显示、两轴零点、UART 波特率、管理 token、试运行 ID 及机械参数。擦除失败保持旧配置；擦除成功而运行恢复尚未完成时，新的网络写入暂拒，恢复继续重试。
