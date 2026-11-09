# ESP32-S3 N16R8烧录文件

2026-11-09起，当前固件已支持[网页OTA](../OTA.md)。首次安装/USB恢复仍按本文操作；后续无线升级使用应用BIN及校验清单，不使用合并镜像。

本地交付ZIP内`esp32/build/RemoteControl_ESP32S3_N16R8_merged.bin`是合并镜像，包含S3 bootloader、分区表、初始化OTA数据与应用。目标为本次N16R8开发板，未在真实板上烧录验证。

## 直接使用现成镜像

1. 使用USB数据线连接S3的**USB转UART口**，在设备管理器确定COM号。
2. 打开硬件资料附带的Flash Download Tool，选择**ESP32-S3**，UART模式。
3. 添加上述`merged.bin`，勾选，地址填**0x0**。这是完整合并镜像，不能填0x10000。
4. 选正确COM，初次速度用115200。若工具要求SPI参数，本版镜像写入头为DIO、80MHz、16MB；其bootloader支持选定的QIO Flash与OPI PSRAM配置，不要改成OPI Flash。
5. 开始下载；无法进入下载模式时，按住BOOT、按RESET、松开BOOT，再重试。结束后按RESET正常启动，串口监视器115200查看热点IP。

烧录会覆盖对应Flash区域的旧程序与数据。本版未实现OTA；不要把原相机工程或其他应用与该固件混烧。

也可用已安装的esptool：

```powershell
esptool --chip esp32s3 --port COM实际编号 --baud 115200 write_flash 0x0 RemoteControl_ESP32S3_N16R8_merged.bin
```

命令中的COM实际编号和文件路径需要按实际替换。也可直接使用Arduino IDE正常编译上传，无需手动配置分文件地址。

## 分文件地址（仅供核对）

|文件|ESP32-S3地址|
|---|---|
|RemoteControl.ino.bootloader.bin|0x0|
|RemoteControl.ino.partitions.bin|0x8000|
|boot_app0.bin|0xE000|
|RemoteControl.ino.bin|0x10000|

请注意S3 bootloader地址是0x0，不要照搬普通ESP32的0x1000。实际编译使用Arduino-ESP32 **2.0.17**，目标ESP32S3 Dev Module、Flash16MB、OPI PSRAM、USB CDC关闭、QIO80MHz菜单。分区使用默认4MB布局，仅用于小体积测试，剩余Flash未分配。
