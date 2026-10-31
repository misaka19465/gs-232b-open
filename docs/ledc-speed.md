# GPIO20 LEDC 调速适配

板级接口见 [`esp_ledc.h`](../include/gs232b/esp_ledc.h)，实现见 [`esp_ledc.c`](../src/platform/esp_ledc.c)。共享 SPI 板级适配器持有 LEDC 实例，并将唯一输出管理器的 PWM 提交交给它；运动层和命令层不直接操作 LEDC。已接入固件组件；`app_main` 仍为构建基线，未在上电时自动运行电机。

## 引脚、计数与边界

| 项目 | 固定配置 |
| --- | --- |
| PWM | GPIO20 / `SPEED_PWM`，不反相 |
| LEDC | `LEDC_LOW_SPEED_MODE`、`LEDC_TIMER_0`、`LEDC_CHANNEL_0` |
| 定时器 | 20kHz、10bit、`LEDC_AUTO_CLK` |
| 初始状态 | 通道初始 duty=0、hpoint=0 |
| 逻辑请求 | 0..1000‰，非法值返回 `INVALID_ARGUMENT`，不访问 LEDC |
| 原始计数 | 0..1024（含两端），非法值不截断、不钳位、不调用 IDF |
| 换算 | `count = (permille × 1024 + 500) / 1000`，四舍五入 |

当前锁定的 ESP-IDF 6.1.0 `driver/ledc.h` 将 duty 范围定义为 `0..2^duty_resolution`。C3 的硬件最大位宽为 14bit，只有选用最大位宽时不能写入 `2^N`；本模块固定为 10bit，并以编译期断言要求它严格小于硬件最大位宽。因此 **1024 是该配置合法的满占空比**，1023 约为 99.90%，不能将两者混称。大于 1024 的计数（包括无符号转换后的负值）在进入 API 前被拒绝。

依据为锁定 SDK 的 `components/esp_driver_ledc/include/driver/ledc.h`、`components/esp_driver_ledc/src/ledc.c` 的通道配置说明及 `components/soc/esp32c3/include/soc/soc_caps.h`；也可参照 [ESP-IDF C3 LEDC 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/peripherals/ledc.html)。修改目标、位宽或 SDK 后须重新核对这些约束。

`gs232b_esp_ledc_set_permille` 负责换算，`gs232b_esp_ledc_set_duty` 负责计数校验和 `ledc_set_duty → ledc_update_duty` 提交。两步必须由同一串行所有者调用，不启用 fade、不在适配器内自动重试。set 失败不调用 update；update 失败可能已有部分更新，必须向输出管理器传播错误。

`gs232b_esp_ledc_close` 先将通道停止到低电平，再释放引脚、通道和暂停后的定时器。释放后引脚不再主动驱动，滤波节点由板上的 R31 下拉泄放；不能由此推断 SPEED 电压已立即归零。关闭或清理失败保留资源标志，须显式重试 close 成功后才能重新 open。活动实例不可复制，open/close 不能与提交并发。

## 四档固件参数

[`speed.h`](../include/gs232b/speed.h) 提供 `gs232b_speed_levels_t`、默认初始化、整表校验与 1..4 档号查询。四档均须在 1..1000‰ 内且严格递增；零值、超范围、重复或降序均拒绝。停止使用独立的零占空比请求，不占用任何速度档。

`gs232b_motion_config_init` 初始化 `azimuth_speed_levels`，可在 `gs232b_motion_init` 前替换整表。运动初始化校验并按值复制参数；它是固件配置，不改变 NVS 记录，也不增加用户数字校准步骤。GS dispatcher 将 `X1..X4` 路由到 `gs232b_motion_set_speed_level`，不再通过档号乘 250 固定推导。

| 档位 | 默认请求 | LEDC 计数 |
| --- | --- | --- |
| X1 | 250‰ | 256 |
| X2 | 500‰ | 512 |
| X3 | 750‰ | 768 |
| X4 | 1000‰ | 1024 |

这些默认值仅延续已有开发参数，**不是实测四档转速**。选择速度档沿用运动核心的控制权、故障、限位和近目标减速策略；定位减速仍取当前请求与 `azimuth_slow_speed_permille` 的较小值。仰角没有 PWM 调速。

## 请求、提交与电压确认

| 状态 | 能说明的事实 |
| --- | --- |
| `output_manager_snapshot.requested` | 最新接纳的逻辑方向及 PWM 请求，可以仍在换向等待 |
| `output.requested` | 最近实际尝试提交的完整请求；AZ 停止或等待时 PWM 被置零 |
| `output.last_successful`、`output.known` | 最后成功提交的请求；`known=true` 只表示完整驱动提交成功 |
| `output.speed_voltage_confirmed` | 现板没有 SPEED 电压反馈，初始化、成功提交、失败及停机后均为 `false` |

LEDC 更新从后续 PWM 周期生效。即使提交成功，软件也未测量滤波器、LM358、外部负载或电机实际速度；等待约 45ms 滤波时间也不等于已确认电压。驱动失败时 `known=false`，历史成功请求保留用于诊断，不能作为当前物理输出读回；输出管理器撤销待执行运动，显式全停成功前不接纳新的运动。

共享输出仍保持停止先尝试 PWM=0，即使 LEDC 失败也尝试清除 595 方向；非零 PWM 仅在方向成功锁存后写入。

## UART0 隔离

`sdkconfig.defaults` 将主控制台选为 USB Serial/JTAG，关闭次控制台，不选择 UART 控制台；应用和 bootloader 使用相同设置。GPIO20 不作为 UART0 RX 初始化，UART1 留给 GS 协议。LEDC 源文件对 UART0 控制台配置设置编译错误，防止旧生成配置重新占用引脚。

`scripts/test.py` 在目标构建后检查应用与 bootloader 的实际生成配置：USB Serial/JTAG 为主控制台，UART 控制台禁用且编号为 -1。ROM 在应用前的行为不由本适配器重新配置；本次也不修改 eFuse。

## 软件验收与待板验证

运行 `python scripts/test.py` 执行 ESP32-C3 目标编译、控制台配置检查和原生 CTest；仅运行主机用例可用 `python scripts/test.py --host-only`。

| 用例 | 覆盖 |
| --- | --- |
| `esp_ledc` | 真实 LEDC 适配器配 IDF API 替身；全遍历 0..1000‰ 与 0..1024 计数；零/满输出、舍入、1025 与极大非法计数不进入 IDF；未打开、错误映射、配置/释放失败与重试 |
| `ledc_rejects_uart0_console` | 启用 UART0 控制台时实际 LEDC 源文件必须编译失败 |
| `speed_levels`、`motion_tests` | 默认和自定义四档、零值/越界/重复/降序、非法档号及非法初始化不改变输出 |
| `esp_ledc_outputs` | 真实管理器→共享提交→LEDC；等待期间请求与提交区分；满幅/停止；错误可部分生效但状态未知；PWM 失败仍清方向；电压确认始终为 false |
| `gs_commands_tests` | 默认及自定义 `X1..X4` 到运动输出，仰角独立、参数按值复制、非法档号无副作用与近目标减速 |

**仍待实机验证：** GPIO20 实际频率与零/满占空比；滤波及运放输出下限、满幅、纹波、约 45ms 响应及负载影响；X1..X4 四档实际转速和近目标减速效果。目标编译及 API 替身测试不代替上述电气与机械验收。
