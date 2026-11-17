# Exlink 方屏版

基于 ESP32-S3 的便携式多功能调试工具。本项目在 Exlink / Exlink_0603 的基础上，适配 480×480 CO5300 方屏，并增加 Android App、蓝牙 OTA、息屏素材更新、水平仪和温湿度页面。

## 功能

- 保留串口助手、数控电源、逻辑分析仪、简易示波器及原有调试功能。
- LVGL 原生 480×480 界面，支持触摸、按键、亮度调节与自动休眠。
- MPU6050 水平仪与校零，AHT20 温湿度读取。
- Android App 查看设备状态，通过蓝牙更新固件。
- 手机本地裁剪、转换图片/GIF，并通过蓝牙更新息屏素材。
- 邮箱注册登录，OTA 授权码一次兑换后绑定账号长期使用。

## 下载

[下载 App、固件和完整软件包](https://github.com/hajimi-yzy/Exlink-Tool-Square/releases/latest)

当前版本：固件 **1.0.24**，App **0.4.0**。

本次修改与已知限制见 [v1.0.24 说明](docs/RELEASE_1.0.24.md)。

## 目录

| 目录 | 内容 |
| --- | --- |
| `src/`、`include/` | 设备固件 |
| `lib/`、`vendor/` | 库、运行资源与驱动 |
| `android/` | Android App 源码 |
| `firmware/` | 应用固件、USB 完整镜像与刷写说明 |
| `app/` | Android 安装包 |
| `scripts/`、`tests/`、`docs/` | 构建工具、测试与开发文档 |

## 构建

固件使用 Python 3.11 与 PlatformIO：

```sh
python -m pip install platformio
python -m platformio run -e esp32s3
```

输出为 `.pio/build/esp32s3/firmware.bin`，默认内部 I²C 频率 100 kHz。刷写方法见 [固件说明](firmware/README.md)。

使用 Python 3.11 与 Zig 0.13.0 运行可靠性回归：

```sh
python scripts/run_reliability_tests.py --output deliverables/reliability-tests
```

可通过 `--compiler` 或 `ZIG_EXE` 指定 Zig。主机测试的覆盖和实机验证边界见版本说明。

Android App 使用 JDK 17、Android SDK 35：

```sh
cd android
./gradlew :app:assembleDebug
```

Windows 使用 `gradlew.bat`。App 使用与构建配置见 [Android 说明](android/README.md)。

## 项目来源与许可

- 原作者：**expert电子实验室**。
- 方屏改版及软件扩展：**一只羊蒸鱼**。
- [原版 Exlink](https://github.com/physicsexpert/Exlink_Tool)
- [Exlink_0603](https://oshwhub.com/usolve/exlink_0603)
- [Exlink_usolve](https://gitee.com/tank-deng/exlink_usolve)

遵守原项目协议：禁止商用，转载注明出处。各第三方组件保留原有版权和许可证，详见 [第三方声明](THIRD_PARTY_NOTICES.md)。
