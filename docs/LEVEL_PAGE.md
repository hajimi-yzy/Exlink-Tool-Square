# MPU6050 水平仪

主菜单的 Level 位于 2048 前面，采用原生 480×480 布局，显示横纵倾角及圆形气泡。
共享总线调度见 [共享总线传感器](SHARED_BUS_SENSORS.md)。

## 接线与采样

MPU6050 与触摸共用内部 I²C：SDA→GPIO18、SCL→GPIO21。除电源和地外不需要
连接 INT，不使用 XDA/XCL。驱动探测七位地址 0x68、0x69，要求 WHO_AM_I=0x68。

配置为 ±2g、±250°/s、44 Hz DLPF、50 Hz 内部采样。I²C 每秒 25 次从 0x3B
突发读取 14 字节，页面以 20 Hz 刷新。复位、PLL 启动和配置读回通过分步状态机
执行，不在共享任务中等待；通信或配置错误后退避两秒再探测。

只有页面打开时采样。页面退出、整机休眠或 OTA 暂停时请求传感器睡眠；恢复后
重新建立滤波与稳定窗口。界面只读取缓存，不直接访问 Wire。

## 倾角与校零

ROLL 使用 `atan2(ay, az)`，PITCH 使用 `atan2(-ax, sqrt(ay²+az²))`，低通时间
常数为 160 ms。轴向按芯片坐标；PCB 安装方向改变时需按实际方向调整轴映射。
气泡显示范围为 ±45°，越界时停在圆盘边缘，数字继续显示角度。

加速度计提供重力方向，陀螺角速度用于判断静止，不积分航向。有效重力模长为
0.5～1.5g。ZERO 要求模长 0.92～1.08g、三轴角速度小于 4°/s、连续角度变化
小于 0.7°、滤波偏差小于 0.35°，并持续静置约 0.6 秒。数据间隔超过 200 ms
重新开始稳定计时；校零时再次检查样本新鲜度。

触摸 ZERO 或实体确认键可设置相对零点。零点仅保留至重启，不写入 NVS。
此功能适合静态和低动态倾角，不用于持续加速运动中的姿态导航，也不是出厂标定。

## 状态

| 提示 | 含义 |
| --- | --- |
| SENSOR NOT FOUND | 未检测到匹配芯片 |
| INITIALIZING SENSOR | 尚未完成配置或首次采样 |
| DATA STALE - WAITING | 数据超过 250 ms 未更新 |
| HOLD STILL - NO GRAVITY | 重力向量无效 |
| HOLD STILL TO ZERO | 当前不满足校零稳定条件 |

无效状态隐藏气泡，倾角显示占位符。地址和身份寄存器应答不代表采样数据有效。

## 测试与参考

`python scripts/run_level_tests.py --compiler g++` 测试驱动、滤波、校零、超时、
短读和错误身份；`tests/level_page_host/run.py` 检查页面状态与对象回收。
硬件测试应覆盖两轴倾斜、静置校零、反复切页、休眠唤醒及传感器断线。

- [TDK MPU-6050 资料](https://invensense.tdk.com/products/motion-tracking/6-axis/mpu-6050/)
- [Adafruit MPU6050](https://github.com/adafruit/Adafruit_MPU6050)
