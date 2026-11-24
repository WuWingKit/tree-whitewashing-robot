# V4.2 双相机与 SPI 固件

配套构建 `pcb_v2`，已实车验证的 ESP32 版本为 `0.5.4-dual-usb-spi`，交接源码 `0.5.5-dual-usb-spi` 沿用相机协议和接线。ESP32 直接采集兼容的 USB1 相机。STM32F407 通过 USB Host/UVC 采集 USB2 相机，经 SPI 传 JPEG 给 ESP32。ESP32 通过 Wi-Fi 发布相机画面。原有 UART 承载四轮控制、三路 ToF、搅拌器、水泵、停机状态和相机诊断。

## 接线与程序

- 相机 1 用 ESP32 `IO19 D− / IO20 D+`，相机 2 用 STM32 `PA11 D− / PA12 D+`。各自的 VBUS 为稳压 5 V，与主控共地。排针针号见[整车集中接线清单](../../docs/14-v42-harness-checklist.md)。
- STM32（SPI2 主机）到 ESP32（SPI2 从机）依次为 `PB13→IO12 SCK`、`PC3→IO11 MOSI`、`PC2←IO13 MISO`、`PB12→IO10 CS`、`PF3←IO14 READY`。排针针号和供电接法见[整车集中接线清单](../../docs/14-v42-harness-checklist.md)。
- STM32 为 USB2 执行 UVC Probe/Commit。SPI 使用 2.625 MHz、模式 0，每次交换 256 字节，其中最多 240 字节为 JPEG 数据。ESP32 按帧号和偏移校验与拼接。STM32 超过 500 ms 收不到有效 SPI 控制时停止送图。
- STM32 采集使用 USB Full Speed、MJPEG，分辨率筛选上限为 640×480，单帧缓存 60 KiB。ESP32 直连兼容相机可采集 MJPEG，或预览 320×240 及以下、5 fps 及以下的 YUY2 灰度画面。
- 遥控状态刷新超时会重试。控制心跳失败、窗口失焦和显式停止仍会停车并锁定。根据 V4.2 实车反馈，STM32 已反转四轮输出极性。

## 构建和烧录入口

```powershell
python firmware/remote_control/tools/prepare.py
python firmware/remote_control/tools/build_stm32.py --profile pcb_v2
python firmware/remote_control/tools/build_integrated.py --profile pcb_v2
```

`prepare.py` 从本地正点原子资料提取 HAL，并把校验过的 ST USB Host 3.5.1 下载到忽略目录。准备脚本会将 USB Full Speed 等时端点包长的检查上限设为 1023 字节。已取得对应 ZIP 时可用 `--usb-host-archive` 指定离线包。

- STM32 产物为 `stm32/build/pcb_v2/remote_control.hex`，Keil 目标为 `RemoteControl_PCB_V2`，经 ST‑Link 烧录。
- ESP32 产物在 `esp32/build/integrated/pcb_v2/`。首次完整烧录使用 `bootloader.bin@0x0`、`partitions.bin@0x8000`、`reset_otadata.bin@0xe000` 和 `app.bin@0x10000`。已有兼容分区的设备走 OTA 时使用该目录的 `app.bin` 与 `ota.json`。

## 实车验收

1. 先断开执行器动力电源，确认两台相机的 VBUS，再启动两端。打开 ESP32 网页的 `/sensors`，检查 ToF 状态和 SPI 在线。
2. 单独启用相机 1 和相机 2，确认各自 JPEG 帧数增加，分别遮挡镜头并确认对应画面。随后同时启用两个预览。网页资源也可用 `:81/camera.jpg?id=0` 和 `:81/camera.jpg?id=1` 分路读取。
3. 检查停止采集会清掉两路预览。拔下任一相机，检查该路过期，并检查另一 USB Host 和控制 UART。接回相机后再确认枚举与画面。
4. 架空底盘后复测四轮、失联停机、三路 ToF 和上装开关。检查双路画面打开时控制响应有无退化。滑台与舵机仍处在接口初始化阶段。

诊断页分别显示 ESP32 USB1 和 STM32 USB2 的连接及采集状态。READY 持续低且发送数为零时，先检查 IO14–PF3 和信号地。发送增长而 ESP32 的 SPI 事务数为零时，检查 CS、SCK 和地。ESP32 有事务但无效包增加时检查 MOSI 并复核线长。STM32 有发送但无有效回复时另查 MISO。

USB2 单路实测相机 `0C45:6367` 使用 160×120 MJPEG。停止后重开会重新协商视频流，STM32 在封包前会清理 JPEG 结束标记后的零填充。网页等待成功解码后显示首帧，短时断帧时保留上一帧并提示更新时间。连接小车热点后可直接访问 `http://192.168.4.1/sensors`，页面和图像服务都在小车上。

2026-11-24 实车测试 USB2，连续采集 45 秒拿到 191 张不同帧。停止后重开，在 1.3 秒左右拿到首帧，两轮均抽样解码出 160×120 JPEG。

同一相机改插 USB1 时，ESP32 能枚举 160×120 MJPEG，但无法出图。该相机只提供 956 字节的等时视频端点，超过 ESP32-S3 UVC 主机的 596 字节上限。STM32 的第二组片内 PHY 数据脚固定为 PB14/PB15，其中 PB14 在核心板上接板载 Flash 片选，且未引出。现板要同时保留两台该规格相机，需外部 USB Host 承担另一台相机的采集，或由外部主机统一采集。 [Espressif UVC 更新记录](https://github.com/espressif/esp-usb/blob/master/host/class/uvc/usb_host_uvc/CHANGELOG.md) · [STM32F407 数据手册](https://www.st.com/resource/en/datasheet/stm32f407zg.pdf)

实现依据：[ST USB Host 组件](https://github.com/STMicroelectronics/stm32-mw-usb-host/tree/v3.5.1) · [STM32 USB Host 接口手册](https://www.st.com/resource/en/user_manual/um1720-stm32cube-usb-host-library-stmicroelectronics.pdf) · [Espressif SPI 从机与 READY 握手](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/spi_slave.html)。
