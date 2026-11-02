# GS-232B Open

## FULL of AI slop, use at your own risk

[EN](/README_EN.md) · [中文](/README.md)

An ESP32-C3-based antenna rotator controller that receives GS-232B commands over USB serial, Ethernet, or Wi-Fi.

[Hardware guide](docs/gs-232b-open-hardware.md) · [Control protocol](docs/gs-232b-serial-protocol.md) · [Management and provisioning](docs/management-commands.md) · [Schematic](docs/SCH.svg) · [Bill of materials](docs/BOM.csv)

## Features

- Azimuth and elevation control, position queries, target positioning, timed sequences, and interactive calibration.
- CH340K USB serial, W5500 Ethernet, and Wi-Fi; network control uses WebSocket.
- Built-in offline provisioning page with network configuration trials, confirmation before saving, and automatic rollback on timeout.
- Uses the G-5500 dual-axis configuration by default; other [rotator models](docs/management-commands.md#旋转器型号) can be selected.

## Connections and first-time provisioning

| Interface | Use |
| --- | --- |
| USB2 / CH340K | Primary serial port. Defaults to 9600 baud, 8N1, with hardware flow control disabled. For configuration changes, follow the same procedure as the original GS-232B; see the [control protocol](docs/gs-232b-serial-protocol.md#连接与分帧). |
| Ethernet / Wi-Fi STA | WebSocket: `ws://<device-IPv4>/ws/gs232b`. |
| USB1 / Native USB | Firmware flashing and debug logs. |

After the serial port starts, wait for `Connect OK`. Commands must end with CR (`\r`). Before first use, perform zero-point calibration as described in the [control protocol](docs/gs-232b-serial-protocol.md#校准).

The device has no user-accessible provisioning button. On first boot, if there is no confirmed network configuration, it starts a SoftAP for 600 seconds:

1. Send `!PROV STATUS` over the USB2 serial port to get the AP name, password, and address.
2. Connect to that Wi-Fi network and open `http://<AP-address>/` in a browser.
3. Configure the network. After confirming that the target Wi-Fi network has obtained an IP address, confirm and save the settings on the page.

After a configuration has been saved, a connection failure will not automatically restart the SoftAP. To provision again, send `!PROV START` over USB2. See [management and provisioning](docs/management-commands.md#首次配网与恢复入口) for the full procedure.

## Build and flash

Requires Python 3, PlatformIO Core, Node.js 20+, and npm. Run these commands from the repository root:

```sh
npm ci
pio run
pio run -t upload
```

The default target is ESP32-C3-WROOM-02U-N4. The built-in provisioning page is generated automatically during the build. Flash through USB1; to specify a port, use `pio run -t upload --upload-port <serial-device>`.

## Tests

```sh
python3 scripts/test.py
npm run test:web
```

The complete firmware and host test suites require a C11 compiler, CMake 3.22+, thread support, and the OpenSSL Crypto development library. Before running the web tests for the first time, run `npx playwright install chromium`.

## Hardware safety

This board connects to the external control interface of the rotator control box and does not drive the motors directly. All interfaces share a common ground and have no opto-isolation. **Do not connect a 12 V adapter to DC1.** See the [hardware guide](docs/gs-232b-open-hardware.md) for wiring and power requirements.

During an MCU reset, hang, or firmware flash, the hardware does not guarantee that direction outputs will turn off immediately. Before connecting a real rotator, verify its [startup and reset behavior](docs/gs-232b-open-hardware.md#启动与复位边界). A software stop is not a substitute for a hardware emergency stop.

## License

This project is licensed under GPL-3.0-only.

“The term ‘GS-232B’ in the project name is used only descriptively, to identify the intended compatibility target of the communication protocol implemented by this project. It does not mean that this project is an official product of Yaesu Musen Co., Ltd., nor does it imply any sponsorship, endorsement, or affiliation with that company. ‘YAESU’ is a registered trademark of Yaesu Musen Co., Ltd.

The protocol compatibility implemented by this project is based on independent development using publicly available technical information. This project does not include any reverse engineering, decompilation, or imitation of the firmware or software of devices manufactured by Yaesu Musen Co., Ltd. This project is intended only to provide limited compatibility with certain publicly available protocol specifications and does not guarantee full interoperability with all official devices.”
