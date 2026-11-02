# 无按钮 SoftAP 配网生命周期

配网由 `gs232b_provisioning_t` 管理，不依赖 GPIO 按键。应用启动时先加载已确认的网络域，再调用 `gs232b_dispatcher_network_configure`；有效的已确认记录会关闭自动配网窗口。没有已确认记录时，第一次启动自动打开 SoftAP，窗口为 600 秒（10 分钟）。STA 后续认证、找不到 AP、DHCP 超时或退避重连不会改变这个判定，也不会因为重连失败自动再次打开 AP。

AP SSID 和口令由 Wi-Fi MAC 派生为独立的 `GS232B-SETUP-xxxxxx` / `GS232B-xxxxxx` 凭据，只在 UART1 的 `!PROV STATUS` 响应中返回口令。SoftAP WebSocket 的状态响应只返回 SSID、地址、窗口剩余时间和 `trial_recovery`，不会返回口令；`NET GET` 也不返回任何口令。

## UART / SoftAP 命令

```text
!PROV START                         # 默认 600 秒
!PROV START {"timeout_s":900}
!PROV STATUS
!PROV SCAN
!PROV STOP
```

`PROV START`、`PROV STATUS`、`PROV STOP` 和 `PROV SCAN` 只能从 UART1 或 SoftAP 管理 WebSocket 执行。首次 `PROV SCAN` 扫描并暂存最多 64 个有效 SSID，按 RSSI 从强到弱排序，每次返回 8 个。响应示例：

```json
{"scan_id":1,"offset":0,"total":1,"next":null,"truncated":false,"aps":[{"ssid":"radio","rssi":-45,"auth":3}]}
```

更多结果会在 `next` 中返回下一块游标。例如 `next:8` 时，用 `!PROV SCAN {"scan_id":1,"cursor":8}` 续读；`next:null` 表示完成。续读复用同一份扫描快照，不重新扫描；快照在 30 秒后、新扫描开始或 SoftAP 停止时失效。过期或被新扫描替换的 ID 返回 `CONFLICT`，超范围游标返回 `INVALID_ARGUMENT`。超过 64 个时 `truncated` 为 `true`，设备保留信号较强的 64 个，界面提示可手动添加。网页收齐并校验扫描 ID、游标和总数后再更新网络列表，中途失败保留上次完整列表。

`CFG BEGIN`、`NET SET`、`CFG CHECK`、`CFG APPLY`、`CFG CONFIRM` 和 `CFG ABORT` 继续使用同一套候选、试运行和回滚事务；`CFG APPLY` 默认建议 60 秒试运行。

SoftAP 使用 `192.168.4.1/24`，若 Ethernet、STA 或已配置静态地址占用该网段，则按顺序尝试 `192.168.5.1/24` 至 `192.168.8.1/24`、`10.77.1.1/24` 和 `172.31.1.1/24`。冲突判断按两个子网的地址区间计算，不把不同掩码误判为不冲突。

## AP+STA 共存和停止语义

AP 和 STA 共享一个 Wi-Fi radio owner、一个工作任务和一次 `WIFI_MODE_APSTA` 启动。STA 配置或试运行重建 netif 时保留 AP intent，重建后自动恢复 AP；停止 AP 只清除 AP intent，不会清除 STA 凭据、活动 trial 或回滚路径。窗口超时也只关闭 AP，活动 trial 仍由 dispatcher 继续确认、超时回滚或恢复待重试。

配网页位于 SoftAP 的根路径 `/`。源文件由 `provisioning.template.html`、JavaScript、CSS 和简体中文消息目录组成；运行一次 `npm ci` 安装前端工具后，执行 `npm run build:web` 会把 MDUI 与 `@lit/localize` 打包为单个 `src/web/provisioning.html`，并生成固件嵌入的 `provisioning.html.gz`。这两个生成文件已加入 `.gitignore`。每次 PlatformIO 构建都会先执行 `npm run build:web`，确保固件使用最新网页源码；ESP-IDF CMake 和宿主测试 CMake 则会在缺少产物时自动生成。从干净克隆构建前仍需先运行 `npm ci`。HTTP 响应标记 `Content-Encoding: gzip`，页面离线可用且不请求 CDN。

在项目根目录使用 Node.js 20 或更新版本：

```sh
npm ci                 # 首次使用或 package-lock.json 更新后安装依赖
pio run                # 自动生成 HTML/gzip 并编译固件
npm run build:web      # 也可只手动生成 HTML/gzip
```

页面采用 MDUI Material You 组件和本地固定色板，用一个网络列表与刷新按钮选择网络。“其他...”收集 SSID 与可选密钥，再进入共用连接弹窗；公开网络不显示密码框。“高级...”在原弹窗内展开 Wi-Fi IP 获取方式，固定方式只显示地址、掩码和可选网关，不设置 DNS。控制协议仍通过现有管理 WebSocket 传输。应用需要在 AP 获得地址后调用 `gs232b_esp_ws_interface` 发布 `GS232B_TRANSPORT_SOFTAP_MANAGEMENT_WS`，这样 WebSocket 和页面只在已发布的 AP 地址接入。

无 STA 凭据的首次配网也使用 AP+STA 无线电模式，因此仍能扫描附近网络。扫描结果保存在 dispatcher 的配网对象中，SDK 结果逐条取出，避免将完整 SDK AP 数组放到管理任务栈上。

网页不会自动确认 trial。应用后需等目标 STA 取得 IPv4 地址，再手动确认保存；刷新或 AP+STA 切换导致重连时，通过 `NET GET` 的 `trial.config.wifi` 与运行 SSID 恢复待确认连接。Wi-Fi STA 支持独立 DHCP 或固定 IPv4；新网络记录使用 NVS schema 3，旧 schema 1/2 仍读取并默认 STA DHCP。
