# GS-232B Open

## FULL of AI slop, use at your own risk

基于 ESP32-C3 的天线旋转器控制器，通过 USB 串口、Ethernet 或 Wi-Fi 接收 GS-232B 命令。

[硬件说明](docs/gs-232b-open-hardware.md) · [控制协议](docs/gs-232b-serial-protocol.md) · [管理与配网](docs/management-commands.md) · [原理图](docs/SCH.svg) · [物料清单](docs/BOM.csv)

## 功能

- 方位、仰角控制，位置查询，目标定位，定时序列与交互校准。
- CH340K USB 串口、W5500 Ethernet 和 Wi-Fi；网络控制使用 WebSocket。
- 内置离线配网页，支持网络配置试运行、确认保存和超时回滚。
- 默认使用 G-5500 双轴配置，可切换[旋转器型号](docs/management-commands.md#旋转器型号)。

## 连接与首次配网

| 接口 | 用途 |
| --- | --- |
| USB2 / CH340K | 主要串口，默认使用 9600 波特率，8N1，关闭硬件流控。如果需要修改，流程与原版一致，详见[控制协议](docs/gs-232b-serial-protocol.md#串口配置)。 |
| Ethernet / Wi-Fi STA | WebSocket：`ws://<设备IPv4>/ws/gs232b`。 |
| USB1 / 原生 USB | 固件烧录与调试日志。 |

串口启动后等待 `Connect OK`，命令以 CR（`\r`）结束。首次使用前，按[控制协议](docs/gs-232b-serial-protocol.md#校准)完成零点校准。

设备没有用户可操作的配网按钮。首次启动且没有已确认的网络配置时，会开启 600 秒 SoftAP：

1. 从 USB2 串口发送 `!PROV STATUS`，取得 AP 名称、密码和地址。
2. 连接该 Wi-Fi 网络，在浏览器打开 `http://<AP地址>/`。
3. 配置网络；确认目标 Wi-Fi 已取得 IP 地址后，再在页面确认保存。

已保存配置后，连接失败不会自动重新开启 SoftAP；可从 USB2 发送 `!PROV START` 重新配网。完整流程见[管理与配网](docs/management-commands.md#首次配网与恢复入口)。

## 构建与烧录

需要 Python 3、PlatformIO Core、Node.js 20+ 和 npm。在仓库根目录执行：

```sh
npm ci
pio run
pio run -t upload
```

默认目标为 ESP32-C3-WROOM-02U-N4，构建时自动生成内置配网页。通过 USB1 烧录；如需指定端口，使用 `pio run -t upload --upload-port <串口设备>`。

## 测试

```sh
python3 scripts/test.py
npm run test:web
```

完整固件与主机测试需要 C11 编译器、CMake 3.22+、线程支持及 OpenSSL Crypto 开发库。首次运行网页测试时，先执行 `npx playwright install chromium`。

## 硬件安全

本板连接旋转器控制箱的外部控制接口，不直接驱动电机；各接口共地、无光耦隔离。**DC1 不能接 12 V 适配器。** 接线和供电要求见[硬件说明](docs/gs-232b-open-hardware.md)。

MCU 复位、卡死或烧录时，硬件不保证方向输出立即关闭。接入实机前需验证[启动与复位行为](docs/gs-232b-open-hardware.md#启动与复位边界)，软件停止不能替代硬件急停。

## 许可

本项目使用 GPL-3.0-only 许可。

YAESU 为 八重洲无线株式会社（Yaesu Musen Co., Ltd.）注册商标，本项目不涉及任何对八重洲无线株式会社所生产设备的逆向工程、反编译、仿制或其他侵权行为，仅通信协议有部分兼容。GS-232B Open 仅为项目名，不代表任何官方产品或品牌。
