# GS-232B Open

ESP32-C3 旋转器控制器，提供 CH340K 串口、W5500 Ethernet 和 Wi-Fi WebSocket 入口，兼容 GS-232B 的控制、查询、定时序列与交互校准。各入口可同时连接和查询，两轴由单一会话控制，响应返回来源会话；网络配置经试运行确认后保存。设备没有用户可操作的配网或恢复按钮。

[板级配置](src/app/board_config.c)默认启用两轴，换向等待为 250 ms。运动就绪状态依据各轴零点、反馈、限位、输出及故障状态判断。

| 说明 | 内容 |
| --- | --- |
| [硬件](docs/gs-232b-open-hardware.md) | 接线、电源、模拟通道、复位限制与实板验收 |
| [GS 控制协议](docs/gs-232b-serial-protocol.md) | 串口/WS 接入、命令、角度映射、序列、校准与控制权 |
| [管理与配网](docs/management-commands.md) | 权限、网络试运行、SoftAP 网页和恢复操作 |

## 构建与验证

需要 Python 3、PlatformIO、Node.js 20+ 和 npm；主机测试需要 C11 编译器、CMake 3.22+、线程支持及 OpenSSL Crypto 开发库，sanitizer 建议使用 Clang。目标依赖由 [platformio.ini](platformio.ini)、[组件清单](src/idf_component.yml)和 [dependencies.lock](dependencies.lock)锁定；当前目标为 ESP32-C3 / ESP-IDF 6.1.0。

从干净检出构建：

```sh
npm ci
pio run
```

PlatformIO 会生成内置离线配网页；修改网页后可单独执行 `npm run build:web`。生成的 HTML/gzip 不提交。使用原生 ESP-IDF CMake 或仅跑主机测试时，也应先安装 npm 依赖；已有网页产物需要手动重建以反映修改。

| 命令 | 用途 |
| --- | --- |
| `python3 scripts/test.py` | 固件及 API 构建、链接/控制台检查、资源预算和主机回归 |
| `python3 scripts/test.py --host-only` | 仅主机回归 |
| `python3 scripts/test.py --host-only --sanitizers` | ASan/LSan/UBSan |
| `npm run test:web` | 配网页浏览器测试；首次需 `npx playwright install chromium` |
| `python3 scripts/resource_budget.py` | 已有默认固件构建时重新生成目标 ABI 资源数据 |

主机结果位于 `build/host/results.xml` 或 `build/host-sanitized/results.xml`，资源数据位于 `build/resource-budget/budget.json`。资源预算统计目标 ABI 的静态分配；实板堆峰值和控制延迟独立记录。

## 维护入口

应用组合在 [src/app](src/app)，协议与运动逻辑在 [src/core](src/core)，外设适配在 [src/platform](src/platform)，配网页在 [src/web](src/web)。内部调用约束随 [include/gs232b](include/gs232b) 中的 API 注释维护，生命周期、容量及故障行为由实现和 [tests](tests) 验证。硬件原件为 [SCH.svg](docs/SCH.svg) 和 [BOM.csv](docs/BOM.csv)。
