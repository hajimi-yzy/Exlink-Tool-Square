# 固件使用

`firmware.bin` 是应用固件，仅用于服务器发布与 BLE OTA，不是完整 USB 镜像。
`exlink-1.0.24-usb-full.bin` 是 USB 完整刷写镜像，从地址 0x0 写入。

安装 esptool 4.x，退出串口监视器，在本目录执行（COM5 换成实际端口）：

```sh
python -m pip install "esptool>=4.5,<5"
python -m esptool --chip esp32s3 -p COM5 -b 115200 --before default_reset --after hard_reset write_flash --flash_size 16MB 0x0 exlink-1.0.24-usb-full.bin
```

硬件需与本工程引脚及16MB Flash布局相符。完整镜像包含OTA初始化数据，已有双槽设备升级优先使用App OTA；完整刷写可能重置当前OTA启动槽。首次从不同分区迁移时先备份原设备内容。

版本 1.0.24，触摸启用，内部 I²C 默认 100 kHz。
