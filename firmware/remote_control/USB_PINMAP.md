# 双USB相机连接ESP32-S3：程序与引脚

> 本文记录 0.4.1 的外置 Hub 方案。现在无 Hub 的 V4.2 配套固件和接线见 [双相机与 SPI 固件](DUAL_CAMERA_SPI.md)。

版本 `0.4.1-usb-pinmap`，2026-11-22，现板配置 `pcb_v2`。本次更新ESP32，STM32继续使用此前配套的上装/TOF版本；UART协议与STM32 GPIO未改变。没有烧录设备。

## 固定引脚与两路连接

ESP32-S3内部USB PHY固定为 **D−=GPIO19、D+=GPIO20**。不能通过软件分配出第二组独立USB数据引脚。两个PCB USB口分别连接外置USB Hub的两个下行口，Hub上行口共用这一对ESP32引脚。

```text
相机1 → PCB相机1 USB母座 → J_HUB1四线 → Hub下行口1 ┐
                                                  ├→ Hub上行口 → ESP32 USB/OTG
相机2 → PCB相机2 USB母座 → J_HUB2四线 → Hub下行口2 ┘                 D− GPIO19
                                                                 D+ GPIO20
```

|主控板两组转接排针|分别连接到|
|---|---|
|J_HUB1 Pin1/2/3/4 = VBUS/D−/D+/GND|Hub第一个下行口的VBUS/D−/D+/GND|
|J_HUB2 Pin1/2/3/4 = VBUS/D−/D+/GND|Hub第二个下行口的VBUS/D−/D+/GND|

Hub上行D−接ESP32 GPIO19，上行D+接GPIO20，GND共地，VBUS依经确认的USB Host供电方案提供5V。也可用Hub上行USB线接开发板的原生USB/OTG口，其数据端就是GPIO19/20。COM/USB-UART口用于下载和日志。不要在同一原生USB上同时接电脑和Hub。

**不能把J_HUB1和J_HUB2两组D+/D−直接并联到GPIO19/20。** 没有实际Hub时只能先接一路相机测试，程序不能代替Hub芯片，也不能通过GPIO产生USB 5V供电。相机已插PCB母座时，不需要把相机自身裸四针再并接一次。

## 本次程序改动

- 共用相机模块新增 `sensor_test/esp32/main/usb_camera_config.h`，明确GPIO19/20、两路PCB相机口与外置Hub的关系。
- 编译时检查ESP32-S3目标、官方SDK的内部PHY引脚和外部Hub支持开关。USB脚仍由官方Host驱动初始化和占用，未把它们设置成普通GPIO输出。
- 启动日志显示固定USB引脚；`/api/usb/status`及传感器状态中的`usb`对象新增`d_minus_gpio`、`d_plus_gpio`、`pcb_camera_ports`、`external_hub_required_for_two_cameras`及接线说明。
- `/sensors`网页显示USB引脚和枚举状态。两台相机可接入，当前一次采集一路MJPEG，人工切换。
- 相机ID按枚举顺序分配，不等于固定的物理USB口编号；首次遮挡镜头确认身份。未宣称双路同时720p或已通过实物回传测试。

## 烧录与测试

ESP32输出仍位于 `esp32/build/integrated/pcb_v2/`。完整USB烧录：bootloader.bin=0x0、partitions.bin=0x8000、reset_otadata.bin=0xe000、app.bin=0x10000；OTA只上传app.bin和配套ota.json，不能把app.bin写到0x0。登录配置沿用本地配置。

```powershell
python firmware/remote_control/tools/build_integrated.py --profile pcb_v2
python firmware/remote_control/tools/flash_integrated.py --profile pcb_v2 --port COM实际编号
```

刷入后连接TreeRobotTest（密码TreeTest2026），打开 `http://192.168.4.1/sensors`。先看到Hub及相机枚举，再选择低分辨率MJPEG采集；确认frames持续增加、画面随场景改变。仅看到USB连接标志不能证明图像已经回传。

供电及三路TOF、泵/电机接线沿用 [联合测试说明](BENCH_TEST_20261120.md)。

官方依据：[固定USB PHY引脚](https://docs.espressif.com/projects/esp-iot-solution/en/release-v2.0/usb/usb_overview/usb_phy.html)、[USB Host及Hub支持](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html)。
