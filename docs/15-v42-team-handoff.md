# 树木涂白机器人 V4.2 开发交接

2026-11-24。适用 V4.2 四层主控底板和上层电源板、STM32F407ZGT6、ESP32-S3、四驱 MD04。固件构建目标为 `pcb_v2`。

## 当前状态

| 项目 | 交接状态 |
|---|---|
| 实车接线 | 现场反馈整车接线已完成，下文给出项目定义的针序。滑台实际选用的电压支路和接线的逐针回读由现场记录。 |
| ESP32 | 最近实车验证版为 `0.5.4-dual-usb-spi`。交接源码 `0.5.5-dual-usb-spi` 修正遥控重新启用时可能被旧状态锁住的问题，已通过主机测试，待上车烧录与复测。 |
| STM32 | 沿用已验证的 USB2／SPI 配套程序，四轮开环、三路 ToF、搅拌器、水泵通过 STM32 执行。滑台和舵机当前只有接口初始化。 |
| 摄像头 | `0C45:6367` 在 USB2 已实测出图，USB1 只能枚举此相机。该相机的 956 字节等时端点超过 ESP32-S3 主机限制。现有双相机尚未同时在小车出图。 |
| 水泵 | 现场反馈第一次能启动，点水泵旁的「关闭水泵」后，再点开启时水泵不转，网页 CH2 显示「高电平／通电指令」。已记录，待现场复测。 |

## 源码和运行入口

| 模块 | 主要职责 | 源码入口 |
|---|---|---|
| STM32F407 | 解析 UART，执行底盘和 CH1/CH2，采集三路 ToF 和 USB2 | [主程序](../firmware/remote_control/stm32/App/main.c) · [ToF](../firmware/remote_control/stm32/App/tof.c) · [USB2 与 SPI 发送](../firmware/remote_control/stm32/App/camera_f407.c) |
| ESP32-S3 | 热点、遥控会话、UART 转发、USB1／SPI 取图、网页 OTA | [主程序](../firmware/remote_control/esp32/RemoteControl/RemoteControl.ino) · [遥控页](../firmware/remote_control/esp32/RemoteControl/index.html) · [SPI 接收](../firmware/remote_control/esp32/idf/main/camera_link_esp.c) |
| UART 与 SPI | UART 走动作指令和遥测，SPI 走 JPEG 分段及相机控制 | [UART 协议](../firmware/remote_control/common/rc_protocol.h) · [动作安全状态机](../firmware/remote_control/common/rc_control.c) · [SPI 帧格式](../firmware/remote_control/common/camera_link.h) |
| 传感器和电脑联调 | 相机／测距网页、独立 ESP32 传感器测试程序、电脑端接收脚本 | [传感器页](../firmware/sensor_test/esp32/main/index.html) · [PC 接收脚本](../firmware/sensor_test/pc/receive.py) |
| 板型配置 | `pcb_v2` 的 GPIO 分配和头文件生成 | [pinmaps.json](../firmware/board_profiles/pinmaps.json) · [生成脚本](../firmware/board_profiles/generate.py) |

连接小车热点 `TreeRobotTest` 后访问 [遥控首页](http://192.168.4.1/) 或 [相机和 ToF](http://192.168.4.1/sensors)。本地 [状态接口](http://192.168.4.1/api/status) 提供遥控会话、STM32 在线和上装指令回报。相机诊断在 [传感器状态](http://192.168.4.1/api/sensors/status)。图像端点为 `http://192.168.4.1:81/camera.jpg?id=1`，其中 `id=1` 是 USB2。热点用于局域网联调。

## 板载与整车接线

[两板接线详图](V4.2_底板到上层接线.png) · [整车集中接线清单](14-v42-harness-checklist.md) · [板级针序](../hardware/pcb/robot_electronics_v4_2_four_layer/WIRING.md) · [全部焊盘网表](../hardware/pcb/robot_electronics_v4_2_four_layer/PINOUT.csv) · [各接口丝印及功能](../hardware/pcb/robot_electronics_v4_2_four_layer/INTERFACE_FUNCTIONS.csv) · [嘉立创 EDA 工程](../hardware/pcb/robot_electronics_v4_2_four_layer/project/RobotElectronicsV4_2_FourLayer.eprj3)

| 线束 | 接线摘要 |
|---|---|
| 底盘电源与主控低压 | 第一组 12 V 电池接 MD04，输入按实物极性与设备限值。经确认的稳压 5 V 接主控板 `J_5V_IN` 的 1=5 V、2=GND。通过载板供两块开发板时装好对应电源跳帽。 |
| MD04 控制与轮组 | `J_TB_CTRL` 1–3→`1P/1IN1/1IN2`，4–6→第 2 路，7–9→第 3 路，10–12→第 4 路，13 或 14→GND。车轮 M1 左前、M2 左后、M3 右前、M4 右后，电机六针整组插入同编号插口。 |
| 载板 UART | ESP32 `IO17 TX`→STM32 `PA3 RX`，STM32 `PA2 TX`→ESP32 `IO18 RX`，板内已连并共地，115200 8N1。 |
| 三路 ToF | 测距 1 对树干，SCL/SDA=`PC0/PC1`，网页 ToF 0。测距 2 在靠车头一侧，`PD11/PD12`，网页 ToF 1。测距 3 在靠车尾一侧，`PC4/PC5`，网页 ToF 2。底板每口 1=3.3 V→模块 VIN、2=GND、3=SCL、4=SDA。正对黑色窗口、排针向下时，模块从左到右为 `SHUT INT SCL SDA GND VIN`，前两针留空。 |
| USB2 相机 | 插 `J_CAM2`。转接排针 `J_HUB2` 的 1=稳压 5 V、2=D−→STM32 `PA11`、3=D+→`PA12`、4=公共 GND。载板口和开发板原生 OTG 口共用该数据对，载板口工作时原生口留空。 |
| USB1 相机 | `J_HUB1` 的 1=稳压 5 V、2=D−→ESP32 `IO19`、3=D+→`IO20`、4=公共 GND。此口使用与 ESP32-S3 兼容的相机。 |
| 相机 SPI | STM32→ESP32 的 SCK `PB13→IO12`、MOSI `PC3→IO11`、CS `PB12→IO10`。MISO `PC2←IO13`、READY `PF3←IO14`，两板共地。 |
| 底板到上层 | 底板 `J_UPPER`→上层 `J_CTRL_LINK`，双排八针逐针对号。1=3.3 V、2=逻辑 GND、3=CH1（PA4）、4=5 V、5=CH3（PA5）、6=CH2（PG12）、7=逻辑 GND、8=CH4（PD13）。CH3/4 预留。 |
| 上装动力电源 | 第二组 12 V 电池接上层 `J_DC`（5.5×2.1 mm，内正外负）或 `J_BAT_IN`（1=正、2=负），两者择一。 |
| IRF540 和负载 | 上层 `J_IRF_CH1/CH2` 分别接 IRF540 控制 CH1 搅拌器、CH2 水泵，每组 1=逻辑 GND、2=3.3 V、3=SIG。上层 `J_IRF_12` 1=+12 V、2=功率 GND 接模块电源输入。负载各接对应通道的 +/−。 |
| 滑台驱动 | 底板 `J_STEP_PUL/DIR/ENA` 各自 Pin 1 接驱动器的 PUL+/DIR+/ENA+，Pin 2 接对应负端。上层 `J_SLIDE_12` 或 `J_SLIDE_24` 给驱动器 VCC/GND 供电。使用 24 V 时短接 `JP_BOOST_EN` 1–2 并实测输出，12 V 时接 12 V 口，跳帽 2–3。步进电机黑/绿一组接 A+/A−，红/蓝一组接 B+/B−。 |
| 限位和夹爪 | 下限位 `J_LIMIT_LOW` 1=PB7、2=GND，上限位 `J_LIMIT_HIGH` 1=PB4、2=GND，使用无源触点。舵机 `J_SERVO` 的 5264-3P 针序为 1=PB6 经缓冲的信号、2=5 V、3=GND。 |

## 水泵已知问题

现场序列为「开泵成功 → 点击水泵旁的『关闭水泵』→ 再点开启，泵不转」。第二次开启时，网页 CH2 显示「高电平／通电指令」。这是 STM32 的输出指令回报，水泵实际端电压、CH2 指示灯和动力供电尚无现场读数。根因待确认，当前先登记，暂缓排查。原反馈末尾的「221」尚未确认对应哪项读数。

0.5.5 另修正了模拟测试中可复现的遥控重新启用状态竞争。该改动尚未上车，也未验证能否影响这次水泵现象。

### 后续复测参考

将车轮架空、执行区域清空，开启遥控。每次只切换水泵，记录「开、关、再开」时的网页状态及对应电压。电脑可以用以下只读命令查看状态，不会发送控制指令。

```powershell
$state = Invoke-RestMethod -TimeoutSec 2 http://192.168.4.1/api/status
$state | Select-Object version,online,armed,owner,estop,maintenance
$state.upper | Select-Object available,pump_requested,pump_output,report_age_ms
```

| 第二次点开启后的现象 | 排查位置 |
|---|---|
| 按钮变灰、遥控已锁定或 `owner=false` | 记下网页提示和 STM32 在线状态，重新启用遥控再测。若再次锁定，记录发生时的 `online/armed/owner/estop/maintenance`。 |
| `pump_requested=true`，`pump_output=false` 或无回报 | 查 ESP32→STM32 的 UART、上装状态回报时效和两端配套程序。先核对网页版本与复测时间。 |
| `pump_output=true`，IRF540 CH2 指示灯不亮 | 量上层 `J_IRF_CH2` 的 Pin 2 对 Pin 1 是否约 3.3 V，再量 Pin 3 对 Pin 1 是否随关／开由约 0 V 切至约 3.3 V。对照 `PG12 → J_UPPER 6 → J_CTRL_LINK 6 → J_IRF_CH2 3` 与 IRF540 控制端三针。 |
| CH2 指示灯亮，水泵不转 | 量 IRF540 电源输入 +/− 和 CH2 负载 +/− 在第二次启动时的电压。输入跌落先查上装电池、保护板和压线，输入正常而负载无电压则查 CH2 输出或模块，负载有电压再查泵及线束。 |

网页的 `pump_output` 是 STM32 的 GPIO 输出指令回报，泵端电压与转动由现场验证。验收至少做三轮水泵独立开关，再测试全局停止可关断水泵且重新启用后保持关闭，直到再次明确点开。

## 构建与测试

准备 Python 3、Node.js、Keil ARM Compiler 5。STM32 的准备脚本要用正点原子资料中包含的 `3，标准例程-HAL库版本.zip` 生成 HAL/CMSIS 依赖和 Keil 工程，并校验 ST USB Host 3.5.1。将该资料放到团队机器上，通过 `--materials` 指定目录。ESP32 工具链和固定 UVC 提交见[官方依赖说明](../firmware/sensor_test/SOURCES.md)，整合版还使用 Arduino-ESP32 3.2.1。Windows SDK 和中间构建目录用英文路径。各脚本的 `--help` 可查看 SDK、Keil 和工具链路径参数。

```powershell
python firmware/board_profiles/generate.py --check
python firmware/remote_control/tools/test_core.py
python firmware/remote_control/tools/test_core.py --suite camera
node firmware/remote_control/tests/test_web.mjs
python firmware/remote_control/tools/prepare.py --materials "厂商资料目录"
python firmware/remote_control/tools/build_stm32.py --profile pcb_v2
python firmware/remote_control/tools/build_integrated.py --profile pcb_v2
```

STM32 入口是生成的 Keil 工程 `RemoteControl_PCB_V2`，也可通过脚本生成 `firmware/remote_control/stm32/build/pcb_v2/remote_control.hex`。ESP32 本地生成 `firmware/remote_control/esp32/build/integrated/pcb_v2/app.bin` 和 `ota.json`。原车 OTA 使用与该台设备配套的凭据和分区，更新前核对[烧录方式](../firmware/remote_control/DUAL_CAMERA_SPI.md)。仓库提供源码和构建脚本，现车 OTA 凭据及包含它的预编译 ESP32 镜像留在设备维护侧。
