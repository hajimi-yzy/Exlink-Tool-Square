# BLE 固件 / 息屏资源更新协议 v1

设备上电和休眠唤醒后默认关闭蓝牙。在 Settings → Phone connection 手动打开 App link 才广播 `Exlink` 的手机服务；原 Nordic UART 桥接模式需在 BLE Serial 页面打开，两种模式互斥，切换前断开原连接并清空旧队列。未连接广播和连接无通信默认各 120 秒后关闭，设置可修改；整机休眠可能提前关闭蓝牙。前台 App 的状态轮询算有效通信，退后台暂停轮询，由设备超时释放连接。更新期间锁定业务与休眠，App 短暂退后台不会主动关闭传输；仍建议保持前台，Android 杀进程或系统限制后台运行无法由本协议保证恢复。手机账号注册由 App/服务器实现，不等于设备所有权绑定。

## GATT

共同后缀：`-7b3f-4f6d-9e1a-71b4c3a22100`。

| UUID 前缀 | 用途 | 属性 |
| --- | --- | --- |
| 7c9e0001 | 更新 service | 广播搜索过滤 |
| 7c9e0002 | control | write with response，要求加密 |
| 7c9e0003 | data | write with response，要求加密 |
| 7c9e0004 | status | encrypted read / notify，仅认证且匹配当前事务拥有者 |
| 7c9e0005 | info | read，JSON 型号、ID、硬件功能、屏幕尺寸 |
| 7c9e0006 | telemetry | read，JSON voltage/current/power/output/pwm/servo/updating；battery无可靠来源则null |
| 7c9e0007 | logs | encrypted read，最多480字节受限日志快照 |

配对采用 LE Secure Connections Just Works + bond。它提供链路加密，但没有 MITM 或设备所有者身份保证；不可把此机制宣传为云账号设备绑定。后续可增加设备屏幕确认或服务器签名授权。当前 SHA-256 是完整性校验，不是固件发布者签名。

BEGIN必须先在设备上按住中键3秒，屏幕显示授权提示，120秒内允许手机开始更新。事务开始后不受120秒窗口限制，仍有60秒数据活动超时；没有本地授权的新BEGIN返回Unauthorized。

所有多字节字段均为小端；UUID 是通常的文本 UUID。控制/数据回调仅复制到最多4帧的队列，Flash写入、状态机及输出操作在主循环处理。发送端等待 status ACK 后才发送下一帧，不用无响应连续灌包。

## 请求

| Opcode | 长度 | 字段 |
| --- | --- | --- |
| 01 BEGIN | 42 | opcode:u8, kind:u8(1固件/2资源), session:u32非零, size:u32, sha256:32字节 |
| 02 DATA | 15+N | opcode:u8, session:u32, offset:u32, N:u16, crc32:u32, payload:N |
| 03 END | 5 | opcode:u8, session:u32；确认已收到全部字节并验证SHA/格式 |
| 04 ACTIVATE | 5 | opcode:u8, session:u32；选中已验证应用/资源；固件随后重启 |
| 05 ABORT | 5 | opcode:u8, session:u32；取消事务并恢复输出 |
| 06 STATUS | 1 | opcode:u8；通知当前状态，也可直接读status |
| 07 RESTORE_DEFAULT | 1 | opcode:u8；本机授权后恢复内置五组表情，须无更新事务 |

CRC 是 CRC-32/ISO-HDLC（多项式倒序 `EDB88320`，初始/最终异或 `FFFFFFFF`），仅覆盖 DATA payload。`123456789` 校验值 `CBF43926`。SHA-256覆盖完整传输内容的原始字节，固件是 `firmware.bin`，不能传合并整片镜像。

N最大480；DATA实际限制为 `min(480, MTU-3-15)`。BEGIN必须一次GATT写入，要求协商MTU至少45，当前 App 要求至少60，推荐517。offset必须等于已接收字节数；校验/偏移错误不会写Flash，App读STATUS后重试。END/ACTIVATE分离，END成功并不改变启动槽。

v1 支持幂等和重连处理：同一完整 manifest 的 BEGIN、立即上一成功 DATA（同 session/offset/长度且 CRC 再次通过）、Ready 状态的 END 可以重发；重复 DATA 只确认进度，不重复写入 Flash。ACTIVATE 不自动重发，缺少确认时必须重连检查结果。App 每次 GATT 只允许一个读/写/发现/MTU 操作，按连接代次、操作类型和 UUID 匹配回调；DATA 通知未到时先加密读取 Status，再有限重发，绝不猜测或提前增加偏移。

## 状态响应（固定16字节）

`version:u8(1), state:u8, result:u8, kind:u8, session:u32, accepted:u32, total:u32`

state: `0 Idle, 1 Receiving, 2 Ready, 3 Failed`。

result: `0 Ok, 1 Unauthorized, 2 Busy, 3 InvalidManifest, 4 WrongSession, 5 WrongOffset, 6 BadChunk, 7 StorageError, 8 HashMismatch, 9 NotReady`。

收到BEGIN后关闭电源/PWM/舵机输出，暂停业务loop和息屏。更新进度在480×480屏幕显示，刷新最多约5次/秒，仅运行显示刷新，不调用业务/输入计时器。取消、失败、资源激活完成或60秒超时后恢复此前输出。固件成功ACTIVATE保持输出关闭并重启。

断连立即撤销新的本机授权，但保留当前 Receiving/Ready 事务与关闭的输出，屏幕提示重连；同一已绑定且认证通过的蓝牙地址在60秒内可接续原 session，不需要再给这个原事务授权。App 最多自动重连2次，读取设备ID及 session/kind/total/accepted，仅允许当前待确认数据包的写入前/后偏移。其他设备不可读状态或接管事务，也不能延长原断连期限。队列带连接代次，旧连接尚未处理的写入作废。

续传依赖设备及 App 当前内存状态，不支持掉电、App进程被杀或设备手动关闭蓝牙后的原会话恢复；手机 RPA 地址变化也不保证续传，此时提示重新授权开始。关闭蓝牙、切换串口模式或shutdown立即取消会话。旧固件断连仍会取消事务，新 App 会拒绝错误 session 并提示重新授权，不会伪装成续传成功。

App 收到固件ACTIVATE的 Idle/Ok/同session/accepted=total 后显示“命令已确认，设备重启”；只有重新连接读出的版本与目标版本一致才显示升级已确认。设备重启后蓝牙默认关闭，需手动再开 App link。设备端 Off/On 会重新注册并重建原 GATT 对象的服务/特征/描述符句柄，保留服务顺序，清理CCCD与旧长写状态；App 释放旧 GATT，不继续轮询失效句柄。

恢复默认内置五组表情：control写单字节`0x07`，要求当前连接加密认证、本机120秒授权、无进行中更新。成功返回Idle/Ok/kind0/session0/accepted0/total0；不会擦除内置表情。

## Flash 与部署

`my.csv` 新布局：4MiB `ota_0` + 4MiB `ota_1`，两个3.5MiB自定义raw资源区（subtype0x40），NVS/otadata/coredump。原factory布局不兼容，首次必须USB刷写bootloader、分区和初始app；旧NVS缩小/地址迁移前应备份配置。旧固件不能通过BLE自行修改分区表。

固件sink写入非运行OTA槽，完成SHA后显式检查ESP32-S3 chip_id=9、标准app descriptor的project_name=`exlink-co5300-31pin`，再调用 `esp_ota_end` 验证ESP镜像，ACTIVATE才 `esp_ota_set_boot_partition`。项目名是产品兼容性筛选，并非签名或防伪认证。构建hook在elf2image之前幂等写入descriptor，不能给旧/第三方镜像随意补名字后发布。

服务器发布可检查裸app bin：image magic byte0=`E9`，chip_id小端位于12..13且为9；app descriptor在32..287，magic=ABCD5432，project_name在80..111以NUL结尾。必须使用本项目生成的firmware.bin，禁止合并镜像/bootloader，大小不得超过4MiB。

发布版本统一来自仓库`version.txt`，构建可用环境变量`EXLINK_FIRMWARE_VERSION`覆盖，也可由PlatformIO环境的`custom_firmware_version`指定，必须是31字节以内ASCII语义版本；写入标准app descriptor的version（bin偏移48..79）。服务器发布版本须与该字段一致；SHA/ELF hash是完整性标识，不当作版本号排序。仅改变版本也会使bin重新生成。

新镜像首次启动需通过显示初始化、缓冲分配、UI、触摸 ACK、INA 探测及剩余堆大于
16 KiB 的检查，才标记为有效；失败请求回滚。pending setup 配有 30 秒任务看门狗。
回滚依赖支持该机制的 bootloader，以及另一槽中可启动、已确认有效的旧镜像。
首次 USB 部署仅写一个应用槽时，没有旧版本可供恢复；分区迁移和回滚能力应在
目标硬件上验证后使用，不能仅依赖应用层调用。

资源sink接口见 `include/device_update.h`；产品通过 `screenAssetUpdateSink()` 提供inactive资源写入/校验/原子切换，禁止直接覆写内置五组fallback。BLE通道不会执行未注册的任意硬件命令，info仅返回支持的功能列表。
