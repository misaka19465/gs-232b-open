# 管理命令与 WS 隔离

第 7.2 节命令由 `gs_dispatcher` 的同一注册表验证、排队和执行。每条完成的扩展请求恰好返回
一行 `!{"ok":...,"cmd":"DOMAIN ACTION",...}\r\n`。停止撤销尚未执行的扩展时，返回
`CONFLICT`，不能执行其写入副作用。普通 GS 继续使用原编码。

## 入口及权限

| 入口 | 可用命令及权限 |
| --- | --- |
| UART1 | 本地管理；独占 `ADMIN ENABLE/DISABLE`、`NET RESET`。 |
| ETH/STA `/ws/gs232b` | 普通 GS、所有权和状态扩展；网络写入默认禁用，启用后须对本会话 `SESSION AUTH`。 |
| ETH/STA `/ws/manage` | 扩展命令；网络写入同样要求认证。拒绝普通 GS，包括校准和 M/W 上传预约。 |
| SoftAP `/ws/manage` | 只在配网窗口内接受连接和请求；仅放行 `SYS INFO`、`NET GET/SET`、`CFG *`、`PROV STATUS/SCAN/STOP`。 |
| SoftAP `/ws/gs232b` | 拒绝握手。 |

AP 密码准入和 ETH/STA 管理 token 均不授予运动权限。AP 不得操作 `OWNER`、应用看门狗、
`MOTION STATUS`、`FAULT CLEAR`、管理 token、网络重置，或延长自己的窗口。配网窗口在控制任务
建立会话时、命令接纳时和执行前复查；窗口关闭后清候选并回收 AP 会话。

`PROV START` 在两轴已知全停、没有序列、上传或校准时可执行；窗口已经开启时保持原期限。
`PROV STOP` 在活动或待回滚 trial 中返回 `CONFLICT`，保留恢复入口。

ETH/STA 的 `OWNER TAKE` 不抢占其他会话；`OWNER RELEASE` 只允许控制者在已知全停且无任务时调用。
`FAULT CLEAR` 可由 UART1 或可信 ETH/STA 会话显式调用；有其他控制者时返回 `NOT_OWNER`，
运动、待运行序列或校准期间返回 `BUSY`。清除时重新停止和采样，故障仍存在则保留锁定。

## 状态数据

- `SYS INFO`：实际固件构建版本、板型、轴能力、记录/网络/显示/零点/管理 schema、入口可用性。
  ETH/STA/AP 可用性来自传输适配器发布的接口；UART1 来自活动本地会话。无密码或认证材料。
- `OWNER GET/TAKE/RELEASE`：完整会话键、transport、epoch、模式、占用原因、序列和空闲计时。
  没有控制者时 `owner:null`；不包含凭据。
- `MOTION STATUS`：两轴机械角（毫度）、原始 ADC、芯片校准电压（微伏）、采样时间及有效性、
  模式、实际方向、目标、限位和故障。无效角度/目标或未知输出方向为 `null`，不伪造零值。
  方向枚举为 0 停止、1 方位顺时针/仰角上升、2 方位逆时针/仰角下降；故障码沿用 `types.h`。
- `SESSION WATCHDOG/PING`：当前 `timeout_ms` 和 `enabled`。默认关闭，允许 0 或 1000..600000ms。

只有当前控制者成功完成的完整命令（包括它自己的查询）或显式应用 PING 续期。
其他会话的 GS/扩展查询、失败和不完整请求、周期校准显示，以及 WS Ping/Pong 均不续期。
到期先于普通请求执行，关闭来源会话、全停、清序列及交互并推进 epoch；迟到 PING 不能恢复旧控制者。

## 管理 token 与持久域

`ADMIN ENABLE` 生成 256bit 随机 token，以 64 个十六进制字符仅在当前 UART1 响应返回一次。
固件用 ESP-IDF PSA Crypto 随机数和 SHA-256，宿主验证用 OpenSSL；独立 `GS232B_STORE_ADMIN`
记录（schema 1）仅保存 32 字节校验材料，沿用已有 version/revision/CRC 及原子槽机制。
新增域追加在原枚举末尾，不改变已有域编号或网络 schema 1/2/3 读取规则。

`SESSION AUTH` 不回显 token。禁用或换 key 会撤销全部网络管理身份并丢弃相应未应用候选；
重连 generation 不继承认证。已经开始的 trial 使用独立期限和恢复机制。禁用/轮换失败保留旧材料，
成功禁用擦除管理域两槽。响应复制到来源 TX 队列后清空 dispatcher 的临时扩展正文。

## NETWORK_ONLY

`NET RESET {"confirm":"NETWORK_ONLY"}` 仅 UART1；须已知全停、无序列/上传/校准、
无其他控制者或配置事务。无效确认字符串和冲突在擦除前拒绝。

只擦除 `GS232B_STORE_NETWORK` 的当前及历史槽，清 RAM 网络配置，恢复 ETH DHCP/禁用 STA，
重开有界配网窗口并标记未配网。显示、两轴零点、UART 波特率、管理 token、trial ID 分配域及机械配置保留。
保留单调 trial ID 可拒绝重置前的旧确认。

持久擦除失败保留旧运行配置。擦除成功后运行时应用或 AP 重启失败时保留 `network_reset_pending`，
控制任务继续重试，期间拒绝新的网络写入；重启后仍从已擦除的网络域进入未配网流程。

## 验收

`python3 scripts/test.py` 构建五个真实固件环境、核对链接的管理/PSA/HTTPD 路径，并执行主机测试。
原生构建要求 OpenSSL Crypto 开发库。`npm run test:web` 验证配网页使用 `/ws/manage`。
固件使用 IDF 标准尺寸优化，保留既有 1MiB 应用分区。已有 sdkconfig 须在 menuconfig 设置
`Optimize for size`，或移走生成的环境 sdkconfig 后按 defaults 重建。

`management_matrix_tests.c` 对第 7.2 节 23 个独立命令分别检查成功、无权和停止造成的 epoch 冲突。
公有查询的无权场景来自已关闭窗口的 AP 会话；不为可信 LAN 公有查询增加认证要求。
额外回归涵盖所有权竞争、执行前撤销认证、token 换 key/禁用/重连/存储失败、故障未消失及清除后
不重跑目标、网络域两槽清除和其他域逐字节保留、运行时恢复重试、迟到 PING，以及匹配 Pong 不续期。

WS 行为替身还覆盖两个 URI、AP 运动/校准拒绝、窗口过期前已排队请求复查和 Origin 策略。
这些验证不代表已完成目标板运动、网络出口或断电实测。
