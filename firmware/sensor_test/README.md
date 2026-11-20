# 双 USB 摄像头 + 三路 ToF 台架测试

**后续已整合回原工程：请优先使用[遥控＋OTA＋传感器整合版](../remote_control/INTEGRATED.md)。本目录的独立固件仅保留作诊断对照，不需要先刷本版。**

日期：2026-11-10。版本：ESP32 `0.3.0-sensor-test`。本次交付源代码、已编译固件、Keil 工程和电脑接收程序，**未连接实物相机/测距模块测试，也未给任何开发板刷机**。

## 实现内容

- STM32：TOF050C / VL6180X，三条独立 I²C；参考本地供应商提供的 Adafruit 初始化序列移植至 HAL。轮流发起测量，有总线检查、超时、错误状态、自动重试；原电机控制和失联停车逻辑保留。
- 两板通信：原 USART2 ↔ ESP32 UART1，12 字节 CRC16 帧；新增 0x90/0x91/0x92 测距帧，兼容旧电机帧。测距传感器异常不无限阻塞主循环。
- ESP32：ESP-IDF **5.4.2**，官方 `usb_host_uvc` **2.6.0 固定源码提交**；启用外部 Hub 支持。两台相机可枚举，网页选择一路 MJPEG 采集，切换前先释放前一路 USB 资源。
- 电脑：网页显示三路测距有效性及实时 JPEG，查询相机实际声明的 MJPEG 分辨率；Python 保存原始 JSON、测距 CSV、JPEG 图片。
- 两个 HTTP 服务：80 端口网页/遥测，81 端口 JPEG。UART 接收另设任务，图片不经串口、不在 MCU 中做视觉识别。

**这是独立传感器测试固件。刷入 ESP32 后暂时代替原无线遥控与 OTA 网页，持续向 STM32 发 STOP，不提供电机启动接口；当前测试固件没有 OTA。** 原遥控/OTA 工程和 2026-11-09 交付包仍保留，后续验证摄像头后再合并功能。

## 文件位置

|路径|用途|
|---|---|
|[WIRING.md](WIRING.md)|详细接线与 USB Host 供电检查|
|`esp32/`|ESP-IDF S3 源码、网页、分区表|
|`../remote_control/stm32/RemoteControl.uvprojx`|更新后的 F407 Keil 工程（含 tof.c）|
|`../remote_control/stm32/App/tof.c`|三路 VL6180X 驱动|
|`pc/receive.py`|电脑 HTTP 接收，Python 3 标准库|
|`build/esp32/`|编译后的 bootloader / 分区表 / APP|
|`build/stm32/`|F407 HEX / BIN / AXF|
|`tools/flash_esp32.py`|用户指定串口后才执行 USB 写入|
|[SOURCES.md](SOURCES.md)|本地资料核对、官方源码版本和验证范围|

## 按此顺序测试

1. 按接线文档接两板串口，只接第一只 ToF，暂不连接电机电源。
2. STM32：Keil 打开 `RemoteControl.uvprojx`，编译下载；也可用 STM32CubeProgrammer 写入 `build/stm32/remote_control.hex`。若用 BIN，地址 **0x08000000**；正常运行 BOOT0 回到低电平。
3. ESP32：使用 **COM/CH343 插口**连接电脑，原生 OTG 插口留给 Hub。使用下述命令通过 USB 写入；**本包改变分区布局，不能上传到旧 OTA 网页**，不能只写 APP 而沿用旧分区表。
4. ESP32 重启后电脑连接 Wi-Fi：**TreeRobotSensors**，密码 **TreeTest2026**。浏览器访问 **http://192.168.4.1/**。这与旧遥控固件的热点名称不同。
5. 测距：网页应显示 `stm32_online: true`。在 ToF0 前放 30–100mm 的目标；另外两只没接时显示无效是正常的。确认 ToF0 后接另外两只，分别移动目标核对编号。
6. 摄像头：先接一台，确认 MJPEG 格式列表出现，选 **640×480 或更低**格式，点“采集此路”。再接第二台，分别点击两路采集按钮验证切换。若插拔后列表异常，重启 ESP32 重新枚举；本轮不把热插拔当作已验证功能。
7. 确认两路镜头与前方/作业端的对应关系，固定 Hub 端口并记录；编号按枚举分配，重启后不保证固定。没有新帧时网页清除旧图，不以旧图冒充实时画面。
8. 需要记录时，在 `pc` 目录运行 `python receive.py --camera 0 --format 0`；`format` 用网页格式列表对应索引，默认 0 不保证是 640×480。不加 `--camera` 仅记录测距。Ctrl+C 停止。

## ESP32 USB 烧录命令

在项目目录下执行，**把 COM10 换成实际 ESP32 的串口**，不要选 STM32 的 CH340 口：

```powershell
python firmware/sensor_test/tools/flash_esp32.py --port COM10 --esptool "E:/tmp/tree_robot_rc_20261108/arduino_data/packages/esp32/tools/esptool_py/4.5.1/esptool.exe"
```

若使用其他电脑，先安装 Python 与 `pip install esptool`，再省略 `--esptool` 参数。若一直 Connecting，按住 ESP32 BOOT、短按 RESET 后松开 BOOT 重试。通信不稳可加 `--baud 115200`。脚本不擦除整片 Flash，但会写新启动程序和分区表；不要同时运行串口监视器占用串口。

烧录地址：bootloader `0x0`，partitions `0x8000`，APP `0x10000`。本测试使用 factory 分区，旧 OTA 选择数据不再决定启动槽。需要恢复时，使用原遥控交付包恢复其启动程序、分区表和 APP，勿混搭两版分区。

## 电脑接口

|请求|作用|
|---|---|
|`GET http://192.168.4.1/api/status`|JSON：版本、STM32 在线、三路测距、相机状态/格式/错误|
|`POST /api/camera/select?id=0&format=0`|异步排队选择一路；202 只表示收到请求，启动结果看 status|
|`POST /api/camera/stop`|停止相机采集|
|`GET http://192.168.4.1:81/camera.jpg?id=0`|当前一路的最新 JPEG；无新帧/未采集返回 503|

ToF 字段：`id` 为 0–2；`valid` 有效才可用 `mm`；否则 `mm:null`；`status` 是原始测距错误或驱动错误；`seq` 为 16 位采样序号，`age_ms` 表示该采样距今时间。超过 750ms 没有新采样判无效。STM32 的心跳超时 500ms 判离线。

|status|含义|
|---|---|
|0|有效（还需检查 valid=true）|
|1–15|VL6180X 原始 range status，包含信号/量程等错误|
|256|I²C NACK、总线故障或未接模块|
|257|测量/就绪超时|
|258|芯片型号 ID 不符，预期 0xB4|
|259|启动中或尚未收到模块信息|
|260|读数低于模块声明的 20mm 盲区|

UART 扩展仍为原协议，type=`0x90+id`，seq=采样计数，left=有效毫米或 -1，right=status，CRC16/CCITT-FALSE；整数小端。不要将 ToF 的 type 当作电机指令处理。

## 边界与故障定位

- ESP32-S3 原生 USB 是 **Full Speed 12Mbps**；N16R8 的内存容量不会增加 USB 带宽。两台相机必须在 Full Speed 下提供 **MJPEG**，USB UVC/720P 商品标签不能证明这一点。仅支持 YUY2 或只在 USB High Speed 暴露所需模式的相机不一定能用。
- 当前明确采用 **双相机接入、一路采集、人工切换**，不宣称两路同时 720P/30fps。先逐路低分辨率验证，随后才能决定是否提高分辨率或换硬件。每帧上限 256KiB，超出计入 oversize，不发送损坏帧。
- 两台都不出现：先看 USB Host 供电、Type-C CC/数据线、Hub 是否有原生 USB2 路径，再看 COM 串口日志；不要把摄像头插在 COM 口。
- 有列表却无图：查 MJPEG 格式、降低分辨率、检查 `error/bad_frames/oversize` 和串口；相机可能在 Full Speed 下无法使用选定参数。
- 测距全无效：先看 STM32 是否在线，再查 VCC/GND、SCL/SDA 是否反接、是否买到 UART 版本、板上 TFT/FSMC 是否占线。
- 本版未扩展 VL6180X 倍率量程，先验证 30–100mm；**没有验证 50cm 测距能力**。点式距离也不是完整障碍物覆盖，更不等于已计算树干中心。

编译验证与电脑接收测试已经执行，实物相机、Hub、供电、测距准确度和无线距离均待你接线刷入后测试。
