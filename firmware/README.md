# 固件目录

> **2026-11-20 更新：** 现板 `pcb_v2` 已加入高电平有效的搅拌器/水泵网页开关、STM32 输出报告和 USB 枚举诊断，保留三路 TOF、底盘遥控及 OTA。见 [联合测试与接线说明](remote_control/BENCH_TEST_20261120.md)。请刷入两端配套版本 `0.4.0-upper-usb-tof`；下方早期版本记录作为历史保留。


[remote_control](remote_control/README.md)提供2026-11-08底盘开环无线遥控测试：STM32F407 HAL本地驱动、ESP32-S3 Arduino无线转发和电脑浏览器操作页。接线、构建和验证范围见该目录。

整机喷涂、滑台、视觉和ToF自动避障尚未实现。2026-11-09已加入[ESP32网页OTA](remote_control/OTA.md)，STM32 OTA尚未实现。原[整机协议规划](../docs/06-protocol.md)用于后续设计，本测试版的实际帧格式以remote_control文档为准。
