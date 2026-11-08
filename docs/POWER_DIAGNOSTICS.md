# MCP4017 数控电源诊断固件

环境：`esp32s3_power_diag`；版本：`1.0.5-power-diag`；产品：`exlink-co5300-31pin`。
该 PlatformIO 环境保留屏幕、触摸、实体按键和业务功能，用于手动诊断。
它用于区分“按钮未发出请求”“I²C 写入失败”“档位读回不一致”和“数字档位正确但模拟输出不变”。

## 本版行为

- MCP4017 使用内部 I²C：SDA=GPIO18、SCL=GPIO21，7 位地址 `0x2F`。
- 开机打印当前加载的 11 V／5 V／3.3 V 预设及启动档位，探测地址并读取初值；地址应答后，按原功能写入保存的启动档位 `power_boot_preset`。不会自动轮流测试电压，也不会后台反复重试写入。
- 点击预设按钮写相应档位，再直接读回校验。`+`／`−` 每次先读芯片当前档位，再单步调整，避免使用过期的软件值。保留原档位范围 `1..126`；原电路对应 `+` 减少一档、`−` 增加一档。
- 已到边界时，继续向外调节只读不写；即使芯片实际为 `0/127`，也不会为了限幅而反向调节。此时 `target=readback`、`queued=0`、`tx=255`、`result=OK`。
- 诊断版开机将日志临时路由到原生 USB，级别为 INFO，并暂停自动息屏／休眠；不把这些诊断覆盖保存到 NVS。
- NVS 预设不擦除，分区不变。进入 UART／BLE 等串口桥接页面，仍按原功能暂时静音；请在数控电源页面收集诊断日志。
- 诊断版允许手动开启电源输出；测试时应先移除不适合承受预设电压的负载。

MCP4017 的读操作直接请求一字节，不先发送所谓“寄存器地址”；额外发送字节会被该芯片视为档位写入。依据 [Microchip MCP4017/18/19 数据手册 DS22147A](https://ww1.microchip.com/downloads/en/DeviceDoc/22147a.pdf) 第 5.4 节。

## 写入与读回日志

每次手动请求打印 `[power]`，操作名包括 `write`、`plus`、`minus`；启动还打印 `boot-read`／`boot-probe`。示例：

```text
[power] write addr=0x2F target=44 queued=1 tx=0 rx=1 readback=44 result=OK
```

| 字段 | 含义 |
| --- | --- |
| `target` | 本次请求写入的档位；纯读或读失败后未尝试调整时可能为 `-1` |
| `queued` | `Wire.write()` 接受的字节数，正常写入为 `1` |
| `tx` | I²C 发送返回码：`0` 成功，`2` 地址 NACK，`4` 其它错误，`5` 超时；`255` 表示未发起写事务 |
| `rx` | 读回收到的字节数，正常为 `1` |
| `readback` | 实际读回的档位；`-1` 表示没有有效读数；若是 `INVALID_READBACK`，会保留超出 `0..127` 的原始返回值 |
| `result` | 本次请求／采样的校验结果，见下表 |

| `result` | 判断 |
| --- | --- |
| `OK` | 写请求已成功且读回与目标一致；纯读或边界不写时表示读到合法档位 |
| `QUEUE_FAILED` | 未成功将完整的一字节写入发送缓冲 |
| `TX_FAILED` | I²C 发送返回非零，结合 `tx` 判断 NACK／超时等 |
| `READ_FAILED` | 未收到可用的一字节读数 |
| `INVALID_READBACK` | 收到的值超出 MCP4017 合法档位范围 |
| `READBACK_MISMATCH` | 写入已应答，但读回档位不等于请求值 |

**`OK` 只证明数字档位及其通信结果，不证明输出电压已经达到 11 V、5 V 或 3.3 V。** 如果档位随预设正确变化而万用表电压不变，需要继续检查电位器模拟端、反馈网络、供电和调压电路。读回校验失败时，软件不再把请求值当作已知硬件状态。

## 每两秒的状态日志

电源页面可看到周期性 `[power.diag]`，它只读档位，不主动改变输出：

```text
[power.diag] fw=1.0.5-power-diag t=12000 addr=0x2F read=OK raw=44 output=1 ina_active=1 V=5.000 last_target=44 last_tx=0 last_result=OK requests=2
```

以上数字仅为格式示例，不是实测结果。

- `fw`：实际运行的固件版本；`t`：启动后毫秒数。
- `read`／`raw`：本轮读取结果与档位；`output`：GPIO1 输出使能状态。
- `ina_active`：INA 测量任务是否启用；`V`：现有 INA 任务的电压值，只有测量任务启用时才更新，停用后可能保留旧值。
- `last_target`／`last_tx`／`last_result`：最近一次写入／调整请求的结果，不是周期读的结果。
- `requests`：累计请求数，包含启动写入或启动探测失败；周期读取不增加计数。

## 构建、刷写和收集日志

在项目根目录使用 PlatformIO 构建。将下方 COM5 替换为实际串口，上传前关闭串口监视器。

在电脑 PowerShell 中执行：

```powershell
python -m platformio run -e esp32s3_power_diag
if ($LASTEXITCODE -ne 0) { throw '构建失败，停止刷写' }

python -m platformio run -e esp32s3_power_diag -t upload --upload-port COM5
if ($LASTEXITCODE -ne 0) { throw '刷写失败，停止启动监视器' }

python -m platformio device monitor -p COM5 -b 115200 --dtr 0 --rts 0
```

构建产物为 `.pio/build/esp32s3_power_diag/firmware.bin`。若设置过 `EXLINK_FIRMWARE_VERSION` 环境变量，它会覆盖环境里的版本号；应先在当前终端取消该覆盖，确保实际产物／日志版本为 `1.0.5-power-diag`。


标准 USB 上传使用匹配的 bootloader `0x0000`、分区表 `0x8000`、OTA 初始化数据 `0xe000` 和应用 `0x20000`，会重新选择 `ota_0` 启动。不要把应用写到旧工程的 `0x10000`，也不需要先整片擦除。

建议先保存完整启动日志，再进入电源页面，逐次点击预设及 `+`／`−`，每次等待一条周期日志，记录对应的万用表读数。不要依靠按钮显示或 `OK` 字样推断实际输出正常。结合日志和实测电压区分通信、预设标定和模拟电路问题。
