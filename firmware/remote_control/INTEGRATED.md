# 原工程整合版：遥控 + OTA + 摄像头 + 测距

> **2026-11-20 更新：** 现板 `pcb_v2` 已加入高电平有效的搅拌器/水泵网页开关、STM32 输出报告和 USB 枚举诊断，保留三路 TOF、底盘遥控及 OTA。见 [联合测试与接线说明](BENCH_TEST_20261120.md)。请刷入两端配套版本 `0.4.0-upper-usb-tof`；下方早期版本记录作为历史保留。


版本 `0.3.1-integrated`，2026-11-10。以这个整合版作为后续开发主线。上一份 `sensor_test` 独立固件仅保留作诊断对照，不需要先刷它。

## 原功能与新增功能

|功能|整合方式|
|---|---|
|电脑控制前进、后退、左右转弯|复用原 `RemoteControl.ino`、原网页、原 `/api/arm`、`/api/drive`、`/api/stop`|
|启用授权、指令序号、租约、失联停车|原逻辑保留；速度上限与开环方式不变|
|OTA|复用原密码、上传页面、SHA256/长度/项目标识校验、停车联锁；新增升级前关闭相机|
|测距|STM32 原 Keil 工程已增加 `tof.c`，原串口新增三路 CRC 遥测；旧电机指令格式不变|
|USB 摄像头|复用官方 UVC 驱动，相机管理与 JPEG 服务运行在独立任务；双相机接入、一路采集、切换|
|网页入口|原遥控主页增加“摄像头与测距”链接，打开 `/sensors`|
|电脑接收程序|沿用 `../sensor_test/pc/receive.py`；可接收整合版 `/api/status` 中的测距数据|

原 Arduino 2.0.17 的底层是旧 IDF，不能直接加入本次使用的外部 Hub 驱动。因此迁移的是**构建底层**：在 ESP-IDF5.4.2 中使用 Arduino3.2.1 作为组件，直接编译原 `.ino` 源文件。不是重新实现一套遥控程序，也不是要求换掉 STM32 或再买一块 ESP32。

`RemoteControl.ino`、`ota_support.h`、`index.html` 仍是原工程位置；新增 `sensor_support.h` 和 `esp32/idf` 构建入口。`sensor_test/esp32/main/cameras.c` 作为公共相机模块复用，构建时复制到英文路径，避免维护两份相机驱动。

## 接线不变

下述接线适用于现有车的 `legacy` 配置（构建时必须显式 `--profile legacy`）。新PCB底板另提供 `pcb_v1` 构建，区别及对应输出目录见[统一引脚配置](../board_profiles/WIRING_PCB_V1.md)。GPIO表、PCB网表和STM32/ESP32头文件由同一份JSON生成；新配置不自动烧入现车。

完整接线见[摄像头与测距接线](../sensor_test/WIRING.md)。这份接线表的 GPIO 与供电要求同样适用于整合版；其中“独立传感器固件持续发送 STOP”的说明仅针对旧独立测试固件，不适用于整合版。

- 板间 UART：S3 GPIO17→F407 PA3、S3 GPIO18←F407 PA2、GND 共地。
- ToF0：PF4=SCL，PF5=SDA；ToF1：PF11=SCL，PF12=SDA；ToF2：PC4=SCL，PC5=SDA；每只 VIN 接 3V3、GND 共地，INT/SHUT 不接。
- Hub 接 S3 原生 USB/OTG，相机接 Hub；COM/CH343 口用于烧录与日志。
- 不接 TFT、不启用 FSMC/SRAM；保留 PG10 高电平禁用 SRAM。
- USB Host 的 VBUS/Type-C CC 配置及相机供电仍需核对。12V 不得接 USB 或 GPIO；不要把独立 5V 输出与电脑 USB 直接并联。

## 刷入与使用

1. STM32 刷本次更新的 `stm32/build/remote_control.hex`，或打开原 `stm32/RemoteControl.uvprojx` 编译下载；否则只有旧电机心跳，没有测距帧。
2. ESP32 使用本版 `esp32/build/integrated/app.bin`，**不要用上一份独立传感器包的 APP/分区表**。
3. 重启后连接原热点 **TreeRobotTest**，密码 **TreeTest2026**。原主页 `http://192.168.4.1/` 继续控制车辆；相机与测距页面为 `http://192.168.4.1/sensors`；OTA 仍为 `/update`。
4. OTA 用户名、密码沿用本机原配置，记录位于 `esp32/ota_access.json`；此凭据文件不公开打包。升级准备会停车、关闭相机并确认停止；升级期间拒绝启动相机与车辆。
5. 先逐只 ToF、逐台相机台架测试，最后验证“遥控 + 图像 + 测距”的并行负载。实际负载下若出现遥控租约超时应停车锁定，需记录日志排查；软件分任务不等于已验证带宽与电源。

原 OTA 分区仍为 ota_0=`0x10000/0x140000`、ota_1=`0x150000/0x140000`，保留 NVS、SPIFFS、coredump 布局。整合 APP 只有在长度不超过旧槽位且通过镜像/项目校验时才可走旧 OTA；构建工具对此执行检查。旧固件升级到本版的无线过程尚未实测。

更稳妥的首次安装方式是 USB 全套写入；脚本会同时重置 8KiB OTA 选择区，让本次写入的 ota_0 启动，避免板子仍启动旧 ota_1：

```powershell
python firmware/remote_control/tools/flash_integrated.py --port COM10 --esptool "E:/tmp/tree_robot_rc_20261108/arduino_data/packages/esp32/tools/esptool_py/4.5.1/esptool.exe"
```

将 COM10 换成实际 ESP32 的 COM 口。USB 写入地址为 bootloader=`0x0`、分区表=`0x8000`、空 OTA 选择区=`0xe000`、APP=`0x10000`；不会擦除整片 Flash，但会重置启动槽选择。仅写 APP 不会自动切换旧的活动槽。

## 接口

- 原 `/api/status` 字段保留，增加 `tof` 数组；旧网页仍可使用。
- `/api/sensors/status`：测距与相机格式/状态；相机页面使用此接口。
- `POST /api/camera/select?id=0&format=0`：排队选择一路相机；`POST /api/camera/stop` 停止相机。
- `http://192.168.4.1:81/camera.jpg?id=0`：最新 JPEG。无新图返回 503，未保证帧率。
- ToF 无效/过期时 `valid=false, mm=null`，不是 0mm；当前原始量程实现先验证 30–100mm。

## 构建与验证

`python tools/build_integrated.py` 使用本机已准备的 IDF5.4.2、Arduino3.2.1、UVC 固定提交在英文目录构建；可通过参数指定其他 SDK 路径。固定 Arduino 提交为 `ac961f671abd5ae1da0a15fd4bee71ed807c2cf3`。不需要升级旧 Arduino IDE 环境。

编译命令只生成固件，不打开串口、不刷机、不切换 Wi-Fi。

本次已验证：

- ESP32 整合固件编译、链接、镜像校验通过，APP **1009728 字节**（含UART诊断计数），原槽位剩余约 23%。旧 0.2.0 与新分区表**逐字节相同**，保留回滚配置。
- 原遥控网页行为测试通过：未启用禁止运动、按住/释放、转向、空格、失焦、网络失败、取消启用、OTA 锁定。
- 原 OTA 镜像头/项目标识校验测试通过，新 APP 符合旧校验的 S3 镜像头、项目标识及槽位大小限制。
- 共享 C 的运动逻辑与新增三路传感器帧测试通过；电脑 HTTP 接收、CSV 无效值、JPEG 去重测试通过。

随后已完成两板实际刷入、UART通信及真实Wi-Fi OTA，见[2026-11-10实机记录](HARDWARE_TEST_20261110.md)。相机尚未枚举、三只ToF未应答；实物感知仍需接线后验证。
