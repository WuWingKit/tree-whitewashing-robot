# 固件目录

> **V4.2 开发交接。** 本目录最新 ESP32 源码为 `0.5.5-dual-usb-spi`，目标板型 `pcb_v2`。当前接线、运行状态及水泵复测见[开发交接说明](../docs/15-v42-team-handoff.md)。下方早期联调记录按历史保留。


[remote_control](remote_control/README.md)提供2026-11-08底盘开环无线遥控测试：STM32F407 HAL本地驱动、ESP32-S3 Arduino无线转发和电脑浏览器操作页。接线、构建和验证范围见该目录。

整机喷涂、滑台、视觉和ToF自动避障尚未实现。2026-11-09已加入[ESP32网页OTA](remote_control/OTA.md)，STM32 OTA尚未实现。原[整机协议规划](../docs/06-protocol.md)用于后续设计，本测试版的实际帧格式以remote_control文档为准。
