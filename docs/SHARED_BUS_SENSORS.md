# MPU6050 与 AHT20

水平仪与温湿度使用独立页面。
菜单次序为 FRE Count → Level → Environment → 2048 → Settings → Device INFO。
页面采用原生 480×480 布局。

## 总线及生命周期

两只传感器与 CST9220、MCP4017、INA226 共用内部 Wire：SDA=GPIO18、SCL=GPIO21。
MPU6050 轮询 0x68/0x69，WHO_AM_I 必须为 0x68；不需要 INT，不使用 XDA/XCL。
AHT20 使用七位地址 0x38。没有占用新的 GPIO，也没有使用 Flash/PSRAM 引脚。

运行期只有既有 `InternalI2cService` FreeRTOS 任务操作这条总线：到期触摸先读，
再处理电源/测量请求，最后分配一个可选传感器步骤。传感器初始化及转换等待均
用时间戳推进，不在 UI、Wire 事务或共享任务中等待转换时间。触摸通信错误/恢复
所在循环不追加传感器操作；缺失传感器以两秒退避重新探测，不连续扫描整条总线。
既有任务优先级、4 KiB 栈、总线频率与超时配置保持不变。

只有对应页面打开才采样，页面退出即时失效缓存并请求后台停用；MPU 仅尝试一次
睡眠写入，AHT20 转换完成后自行空闲。快速退出/重开采用页面代际检查，旧事务不能
向新页面发布。休眠及 OTA 沿用原 pause/resume：后台先停传感器，再确认总线暂停；
确认后不继续传感器 I/O。恢复后重新初始化并等待新数据，不继续显示过期样本。
这两个只读页面不阻止空闲休眠，不改变电源/示波器运行时的休眠保护。

独立任务隔离前台阻塞，不等于消除了物理故障：同总线短路、供电/波形异常或旧 IDF
恢复路径仍可能延迟触摸。20 ms Wire 设置不是底层恢复的绝对时限；桌面测试不能替代硬件电气测量。

## 水平仪

沿用现有 MPU 驱动、滤波和 ZERO：±2g、±250°/s，25 Hz 突发采样，20 Hz 页面刷新。
静态倾角来自重力方向，陀螺角速度用作静止判断，不积分出航向。保持原有效重力门限
0.5～1.5g；静置约 0.6 秒且滤波收敛后才允许校零，不能通过放宽门限掩盖异常。
ZERO 再次原子检查稳定性及 ≤250 ms 新鲜度；相对零点保留至重启，不写 NVS。
轴向按芯片坐标，安装方向改变时需调整轴映射。倾角与校零见 [水平仪](LEVEL_PAGE.md)。

## 温湿度

AHT20 数据解析参考 [YOLO-Human-Detection-Project-Based-on-STM32H750 的传感器驱动](https://github.com/hajimi-yzy/YOLO-Human-Detection-Project-Based-on-STM32H750/blob/94cd9c02c3259292076218ff92a25016cc6bd8dd/H7_onboard/MDK-ARM/USER/APP/env_sensors.c)。
STM32 HAL 传输替换为 Wire，转换等待改为分步状态机，避免阻塞共享 I²C 任务。

- 状态先直接读取，失败后分两步发送 `71`、读取状态；只以 bit3 判断校准。
- 启用时执行一次 `BA` 标准软复位，等待 200 ms，再发送 `BE 08 00`。
  此后仅未校准时初始化；0% 湿度本身不触发额外复位。
- 测量命令 `AC 33 00`，等待 80 ms，再独立读取一字节忙状态；忙状态最多再等 20 ms。
  就绪后原始读取七字节，不附加寄存器前缀。每一步最多一次总线事务。
- 初始化忙标志有 200 ms 截止；失败清除有效标志并退避两秒。
- 沿用新版 CRC-8：初值 0xFF、多项式 0x31，对前六字节校验第七字节。
- 湿度 `raw × 100 / 2^20`，温度 `raw × 200 / 2^20 − 50`；约每秒更新。
- 沿用新版 -40～85°C、0～100% RH 数据范围检查；0% 本身不作为通信错误。
- 串口记录状态读取方式及最后一次完整的七字节原始帧，CRC 失败帧也保留供诊断。
- 缺失、初始化、读取错误和 >3 秒过期分别显示状态及占位符，不将无效数据显示为零。

复核资料（协议事实参考，没有整库引入）：

- https://github.com/sparkfun/SparkFun_Qwiic_Humidity_AHT20_Arduino_Library
- https://github.com/enjoyneering/AHTxx （AHT2x 七字节帧及 CRC 参数）
- https://github.com/adafruit/Adafruit_MPU6050 （既有 MPU 复位及寄存器流程）
- https://github.com/adafruit/Adafruit_AHTX0/blob/master/Adafruit_AHTX0.cpp （标准软复位后校准）
- https://cdn.sparkfun.com/assets/d/2/b/e/d/AHT20.pdf （Aosong 手册第 5.5 节软复位）

## 开发测试

桌面测试直接编译生产传感器模板、实际内部 I²C worker 及真实 LVGL 页面。
`scripts/run_aht20_tests.py`、`scripts/run_level_tests.py` 验证数据解析、时序、
CRC、失败退避、初始化和时间戳溢出；`scripts/run_internal_i2c_tests.py` 验证
连续触摸下采样、旧 Power/INA 请求、暂停/恢复、快速切页代际及 ZERO。
运行脚本可传 `--zig <zig.exe>` 或 `--compiler <C++编译器>`。

`tests/environment_page_host/run.py`、`tests/level_page_host/run.py` 使用实际 LVGL
检查异常提示、控件回收和反复进出。`tests/ui_native_host/run.py` 继续严格比较
其它页面几何；菜单仅对两个合法新增条目作显式差异处理，不重录旧基准。
