# EXLINK Android

EXLINK 配套 Android App，支持 Android 8.0 及以上系统，使用 Kotlin、Jetpack Compose、Android BLE 和 OkHttp 开发。

- **设备**：搜索并连接 Exlink，查看电压、电流、功率、输出状态、日志和功率曲线。
- **OTA**：登录账号、兑换 OTA 授权码，从云端下载固件并通过蓝牙升级设备。
- **息屏素材**：导入图片或 GIF，在手机本地裁剪和转换，通过蓝牙更新设备素材，支持恢复默认表情。

## 编译

安装 JDK 17 或更高版本、Android SDK 35，并设置 `JAVA_HOME`、`ANDROID_HOME`。在项目根目录执行：

```powershell
Set-Location android
.\gradlew.bat :app:assembleDebug
```

生成的 APK 位于 `app/build/outputs/apk/debug/app-debug.apk`。

发布构建使用：

```powershell
.\gradlew.bat :app:assembleRelease
```

生成 `app/build/outputs/apk/release/app-release-unsigned.apk`，使用自己的 keystore 签名后安装或发布。Release 已启用 R8 混淆和资源压缩。

## 云服务配置

云端 OTA 需要配套服务。仅使用蓝牙设备与息屏素材功能时，无需配置云服务。

将 `cloud.local.properties.example` 复制为 `cloud.local.properties`，填写自己的 HTTPS 服务地址和叶证书 SHA-256 指纹后重新编译：

```properties
EXLINK_API_BASE_URL=https://192.0.2.10
EXLINK_TLS_FINGERPRINT_SHA256=替换为自己的叶证书SHA256指纹
```

上面的 IP 为示例地址。也可设置同名环境变量，环境变量优先。两项均留空时可构建本地版，在启动页选择“使用本地设备功能”，无需账号即可使用 BLE 和息屏素材。

服务地址须为 HTTPS IPv4 源地址，可带端口，不包含路径、查询参数或用户名。指纹为 32 字节 SHA-256，可使用冒号分隔。两项必须同时配置；格式无效时构建会报错。`cloud.local.properties` 不纳入版本控制。

App 校验证书有效期、指纹及主机名，禁用明文 HTTP 和自动重定向。密码、邮箱验证码和访问令牌不写入普通偏好文件，也不记录到日志。

### 账号与 OTA 授权

云端版支持邮箱验证码注册和账号密码登录。登录后进入 OTA 页，填写授权码并兑换：每个码只可兑换一次，兑换后绑定当前账号长期使用。

授权状态由服务器验证，固件列表和二进制下载均要求账号具有 OTA 权限。授权失效会清除 App 中的固件列表及下载缓存；登录失效时需重新登录。

接口定义位于 `CloudApi.kt` 的 `ApiConfig`：

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| POST | `/api/auth/email-code` | 发送注册邮箱验证码 |
| POST | `/api/auth/register` | 注册账号 |
| POST | `/api/auth/login` | 登录账号 |
| GET | `/api/ota/entitlement` | 查询 OTA 授权 |
| POST | `/api/ota/redeem` | 兑换 OTA 授权码 |
| GET | `/api/firmware?product=exlink-co5300-31pin` | 获取固件列表 |

## 蓝牙连接与升级

1. 在设备 **Settings → Phone connection** 开启 **App link**。
2. 在 App 的“设备”页点击“搜索设备”，授予蓝牙权限，选择 Exlink 并完成配对。
3. 等待“已就绪”后读取设备状态。App 只显示硬件状态，不控制电源输出。
4. 升级前关闭电源输出，保持设备供电。在 OTA 页确认账号已授权，获取并选择固件，下载校验完成后点击“通过蓝牙升级”。
5. 升级时保持 App 在前台和蓝牙连接，手机及设备屏幕会显示进度。重启后重新开启 App link 并连接，App 读取新版本以确认升级。

设备蓝牙默认关闭，App link 与 BLE Serial 桥接互斥。广播和连接空闲超时可在设备设置中调整。首次迁移 OTA 分区须通过 USB 完整刷写。

固件下载校验大小和 SHA-256；BLE 数据包使用 CRC32、会话号和偏移确认。短暂断连可尝试自动重连续传，设备保留事务 60 秒；掉电、关闭设备蓝牙或 App 进程退出后需重新开始。传输期间暂停状态轮询，避免 GATT 操作冲突。

BLE 服务 UUID 为 `7c9e0001-7b3f-4f6d-9e1a-71b4c3a22100`，control/data/status/info 特征后缀分别为 `0002/0003/0004/0005`；`0006` 读取硬件状态，`0007` 读取日志。协议详见 [BLE_UPDATE_PROTOCOL.md](../docs/BLE_UPDATE_PROTOCOL.md)。

## 图片与 GIF

在“息屏素材”页导入图片或 GIF，调整水平、垂直裁剪位置，点击“转换素材”，检查预览和转换结果，再点击“确认更新到设备”。更换裁剪位置后需重新转换。

- 输出尺寸为 **480×480**，图片和动画只在手机本地处理，不上传云端。
- 源文件最大 **16 MiB**；GIF 原尺寸最大 **2048×2048**，最多取前 **25 秒**。
- GIF 最多接受 2500 个源帧，累计索引像素不超过 64 MiB；超过限制时需缩短或缩小原图。
- 设备素材槽为 **3.5 MiB**，容量限制针对转换后的 EXA1 文件。源 GIF 大小不直接决定是否能上传。
- GIF 优先按约 10 fps 采样，合并连续相同帧；超出容量时减少采样帧并保留片段时长。较短、背景简单的 GIF 更适合设备播放。
- 只有一帧时按照片展示，展示时长由设备 **Photo time** 设置。

上传前在设备端开启蓝牙更新授权并停止正在运行的输入输出功能。“恢复设备默认表情”可切换回固件自带的五组动画。
