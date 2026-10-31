# 共享 SPI、595 提交与 W5500 外置复位

板级入口见 [`esp_shared_spi.h`](../include/gs232b/esp_shared_spi.h)，可替换平台端口和完整提交协议见 [`shared_spi.h`](../include/gs232b/shared_spi.h)，复位协调接口见 [`w5500_reset.h`](../include/gs232b/w5500_reset.h)。这些适配器已加入固件组件；`app_main` 仍是构建基线，应用任务、netif、会话和网络恢复服务通过下面的接口集成。

构建使用 `espressif32@8.0.0`，实际解析为 ESP-IDF **6.1.0**（PlatformIO framework 包 `4.60100.0`）。IDF 6 已将 W5500 移至官方组件，工程在 `src/idf_component.yml` 和 `dependencies.lock` 锁定 `espressif/w5500 2.0.1`、`espressif/wiznet_common 1.1.0`。无需为此更改 `sdkconfig.defaults`。

## 总线和提交边界

| 项目 | 实现 |
| --- | --- |
| Host | 唯一 `SPI2_HOST`；MOSI GPIO4、MISO GPIO5、SCLK GPIO3 |
| W5500 | mode 0、MSB first、硬件 CS GPIO8；16bit 地址、8bit 控制、VDM 数据 |
| 595 | 同一 host 上的独立 device；mode 0、MSB first、`spics_io_num=-1`，恰好 8bit 数据 |
| STCP | GPIO2，发送前低，完成后高→低；每次电平设置后保持至少 1µs |
| 时钟 | 两个 device 使用相同频率，建议入口参数 `GS232B_BOARD_SPI_CLOCK_HZ`（1MHz），适配器拒绝更高值；仍需板验 |
| 入口锁 | 两个 device 共用 FreeRTOS mutex，默认建议等待 50ms；只执行同步 SPI，无异步排队 |
| PWM | GPIO20，LEDC 20kHz/10bit，0..1000 换算到 0..1023 |

成功的提交轨迹为：

```text
入口 lock
  spi_device_acquire_bus(595)
    STCP=0
    完整 shadow byte（8bit）
    spi_device_polling_transmit 返回：传输已完成
    STCP=1 → 保持 → STCP=0 → 保持
  spi_device_release_bus(595)
入口 unlock
```

锁存低电平建立、完整发送、完成等待、锁存上升沿和恢复低电平均在 host 所有权内。SPI host 会阻止其他 device 的事务进入此区域，W5500 硬件 CS 保持非选中。网络事务不访问 STCP；它可以破坏 595 移位内容，之后的输出提交始终重发完整 shadow，绝不复用移位寄存器。

IDF 6.1 的 `spi_device_acquire_bus` 只接受 `portMAX_DELAY`。适配器在调用它之前，先通过带有限等待的共用入口锁排空全部同步事务；两个原始 device handle 只由适配器使用，禁止外部直接提交、注册其他 device 或 bit-bang。这样 host acquire 时没有其他待完成事务。入口超时上报 `GS232B_ERROR_TIMEOUT`；该超时不代表 SPI 电气传输完成期限，也不能处理 CPU 或 SPI 硬件卡死。

发送失败不产生锁存上升沿；已取得 host 后的所有错误路径都先尝试将 STCP 拉低，再释放 host 和入口锁。锁存调用失败可能已改变输出，PWM 也可能部分更新；任何输出驱动错误均交给输出管理器标记 `known=false`，不在适配器内部重试或伪造成功。PWM=0 在尝试 SPI 前先写入，PWM 失败也继续尝试停止方向；非零 PWM 仅在方向成功锁存后提交。

## 初始化与恢复接口

由一个串行所有者维护输出管理器和复位协调器，按以下顺序启动：

1. 用清零的 `gs232b_esp_shared_spi_t` 调用 `gs232b_esp_shared_spi_open`。此时 W5500 访问门关闭，STCP 置低但尚未锁存。
2. 用 `gs232b_shared_spi_output_driver(&adapter.bus)` 初始化唯一输出管理器，立即提交 `0x00`、PWM=0。时钟使用覆盖提交耗时的单调微秒时钟。初始化失败先处理故障，再显式 `stop_all`，成功后才开始网络恢复。
3. 将相同管理器和 bus、网络生命周期回调交给 `gs232b_w5500_reset_init`。默认建议低脉宽 1ms、释放后等待 10ms；接口强制最低 500µs 和 1ms，与锁定 W5500 PHY 的外置硬复位参数一致。
4. `gs232b_w5500_reset_begin` 请求首次启动或运行中恢复；周期调用 `gs232b_w5500_reset_tick`，同时继续处理运动 tick 和优先停止事件。OK 可表示等待中，只有 `phase == GS232B_W5500_READY` 才表示网络恢复完成。

流程如下：

```text
quiesce 完成
  → 禁止新 W5500 访问并等待在途同步事务返回
  → 输出管理器只改 bit4=0
  → 从提交完成后等待低脉宽
  → 输出管理器只改 bit4=1
  → 从提交完成后等待恢复时间
  → 开门，resume 重新探测、安装并恢复网络
  → READY
```

`quiesce` 和 `resume` 均在 SPI 锁之外调用，返回 `WOULD_BLOCK` 可让后续 tick 轮询网络任务的结果。回调必须有界，不把驱动长时间停启工作阻塞在运动循环中。启动中的再次 `begin` 返回 BUSY；失败保持 FAILED 和 `last_error`，须显式重新请求。

网络层提供的回调合同是：

| 回调 | 返回 OK 之前必须完成的工作 |
| --- | --- |
| `quiesce` | 停止新发送和收包、处理本接口会话失效与必要的所有者停止事件、停用旧驱动及其访问者。首次启动没有旧驱动时直接完成。`esp_eth_stop` 本身不能作为所有并发访问者已退出的证明。 |
| `resume` | 使用 [`gs232b_esp_w5500_create`](../include/gs232b/esp_w5500.h) 创建 MAC/PHY，在恢复等待结束后才安装/探测，恢复 MAC 地址和网络配置并启动接口。硬复位会丢失 MAC/缓冲区等寄存器配置，不能只对旧实例调用 `esp_eth_start`。 |

工厂使用官方 W5500 驱动的 `custom_spi_driver` 回调，所有读写强制经共享入口锁和访问门，不再由官方驱动注册第三个 device。PHY 的 `reset_gpio_num=-1`，GPIO2 始终只用于 STCP；nRESET 始终由管理器修改 bit4。MAC/PHY 对象借用 bus，必须在关闭板级适配器之前停用并释放。

复位低电平和恢复等待期间，W5500 读写返回 NOT_READY，595 及停止提交仍可执行。关门先原子禁止新访问，再排空在途事务；排空超时也保持新访问关闭，此时不得继续拉复位。错误状态不再补写 bit4，保留原始错误和输出故障证据。

恢复保留当前实际方向及 PWM，不推进待换向动作。等待期间的停止请求优先保留，之后释放复位不会恢复旧运动。bit4 由协调器独占，其他完整输出请求须保留管理器当前复位值。恢复过程中输出变为未知时，协调器立即失败并关闭网络门，不通过下一次 reset 提交掩盖故障；应用先推进运动故障处理，再显式全停和重试恢复。

## 验证边界

运行 `python3 scripts/test.py`，构建 ESP32-C3 固件并运行独立 host 测试。新增用例：

| 测试 | 覆盖 |
| --- | --- |
| `shared_spi_tests` | 网络污染后重发全部有效方向/reset 字节；精确 acquire→8bit→完成→锁存→release 轨迹；在每个边界让 pthread 网络事务及直接竞争 host 的事务进入等待；结合 acquire、发送、完成、锁存高/低的失败注入；参数、PWM 和网络错误；关门先禁止新访问再等待在途事务。 |
| `w5500_reset_tests` | 首次启动与运行恢复；以提交完成时间计算脉宽/恢复等待；非阻塞停启回调；方向/PWM 保留、停止不被复活、待换向不被启用；开门失败与输出未知状态不自动重试、不覆盖故障。 |

**待板验收：SPI 电气时序及实际输出不跳变。** 需要测量共享 SCLK/MOSI、W5500 nSS、STCP、bit4 和方向输出，确认 3.3V 下所有以太网时钟满足 595 时序、锁存脉宽/建立保持符合器件要求，并在并发网络流量和硬复位时确认方向、PWM 实际输出不跳变。软件 fake 轨迹及目标编译不代替这些测量。
