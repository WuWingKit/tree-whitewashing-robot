# V4.2 整车集中接线清单

适用主控底板 V4.2、独立电源板 V4.2、四驱 MD04，以及 `pcb_v2` 固件。每组线的两端都贴编号，接完后可勾选该组。表内针号是 PCB 封装针号，翻到背面操作时按实板 Pin 1 标记定位。

| 接线范围 | 连接组数 | 导体或线缆数量 |
|---|---:|---|
| 当前联调：四轮、三只 ToF、搅拌器和水泵 | 17 组 | 47 芯逐针或端子接线，另有 2 条电池线和 4 条六针电机线 |
| USB2 相机 | 2 组 | 1 条相机 USB 线、1 条四芯转接线 |
| USB1 相机（兼容 ESP32-S3 的型号） | 2 组 | 1 条相机 USB 线、1 条四芯转接线 |
| STM32 → ESP32 相机 SPI | 1 组 | 1 条六芯 SPI 线束，含 READY 和 GND |
| 后续扩展：滑台、两只限位、夹爪舵机 | 8 组 | 19 芯，含滑台电机和舵机自带的线 |
| 可选本地停车按钮 | 1 组 | 2 芯 |

表格按接口分组。两台现有相机中，已测相机在 USB2 出图。USB1 需兼容 ESP32 USB Host 的相机。上装 12 V 电池可接圆形 DC 口或螺丝输入，两者择一。

## 一、当前联调

| 完成 | 标签 | 起点 → 终点 | 接法 | 数量 |
|---|---|---|---|---:|
| ☐ | BAT-A | 底盘 12 V 电池 → MD04 直流电源输入 | 核对实物电源口极性。MD04 资料规定输入不超过 13 V | 1 条电池线 |
| ☐ | CTRL-5V | 已确认合规的稳压 5 V → 主控板「5V 电源输入」`J_5V_IN` | Pin 1 接 5 V，Pin 2 接 GND。通过载板供电时装上 `JP_PWR_STM`、`JP_PWR_ESP` 两只跳帽 | 2 芯 |
| ☐ | DRIVE | 主控板「底盘驱动」`J_TB_CTRL` → MD04 控制端 | 12 路信号加一根信号地，详见下表 | 13 芯 |
| ☐ | M1～M4 | 四只 520 电机 → MD04 同编号六针电机口 | 左前 M1、左后 M2、右前 M3、右后 M4 | 4 条 × 6 针 |
| ☐ | TOF-1 | 主控板「测距1」`J_TOF0` → 树干测距模块 | 1→VIN，2→GND，3→SCL（PC0），4→SDA（PC1） | 4 芯 |
| ☐ | TOF-2 | 主控板「测距2」`J_TOF1` → 作业端前侧，靠车头 | 1→VIN，2→GND，3→SCL（PD11），4→SDA（PD12），网页 ToF 1 | 4 芯 |
| ☐ | TOF-3 | 主控板「测距3」`J_TOF2` → 作业端后侧，靠车尾 | 1→VIN，2→GND，3→SCL（PC4），4→SDA（PC5），网页 ToF 2 | 4 芯 |
| ☐ | UPPER | 主控板「上装控制」`J_UPPER` → 电源板「主控信号」`J_CTRL_LINK` | 双排 8 针同号相连。两端按针号查通断 | 8 芯 |
| ☐ | MIX-SIG | 电源板「搅拌 CH1」`J_IRF_CH1` → IRF540 第 1 路控制口 | 1→逻辑负极，2→逻辑正极 3.3 V，3→信号（PA4） | 3 芯 |
| ☐ | PUMP-SIG | 电源板「水泵 CH2」`J_IRF_CH2` → IRF540 第 2 路控制口 | 1→逻辑负极，2→逻辑正极 3.3 V，3→信号（PG12） | 3 芯 |
| ☐ | BAT-B | 上装 12 V 电池 → 电源板 `J_DC` 或 `J_BAT_IN` | 选一个。圆口 5.5×2.1 mm，内正外负。螺丝输入 Pin 1 正、Pin 2 负 | 1 条电池线 |
| ☐ | IRF-PWR | 电源板「12V 搅拌泵模块」`J_IRF_12` → IRF540 电源输入 | Pin 1→供电正极，Pin 2→供电负极 | 2 芯 |
| ☐ | MIX-LOAD | IRF540 CH1 负载输出 → 搅拌器 | 通道负载 +/− 对设备 +/− | 2 芯 |
| ☐ | PUMP-LOAD | IRF540 CH2 负载输出 → 水泵 | 通道负载 +/− 对设备 +/− | 2 芯 |

**主控板 5 V 输入**是两块开发板及载板低压电路的供电入口。MD04 厂家注明预留口约 5.3 V。在实测电压、确认开发板输入规格和带载波动前，先给 `J_5V_IN` 用已确认的稳压 5 V。若通过电脑 USB 给某块开发板供电，先拔掉该板的 5 V 跳帽，避免与载板电源并联。ESP32 和 STM32 的 UART 与信号地已在 PCB 内连接。

**三只 ToF** 的 Pin 1 都由载板供 3.3 V，INT 与 SHUT 均留空。三组 SCL/SDA 各自走自己的四芯线。面向 TOF050C 黑色测距窗口、排针向下时，模块从左到右为 `SHUT、INT、SCL、SDA、GND、VIN`。**泵和搅拌器**各自的两根负载线都回对应 IRF540 输出端。电源板的逻辑地与功率地之间 R7 在 BOM 中为 DNP，按控制口与负载口各自的回路走线。

### DRIVE 线束针号

| 车轮 | 主控板 `J_TB_CTRL` 按 PWM、IN1、IN2 顺序 | MD04 对应丝印 |
|---|---|---|
| 左前 M1 | 1 PE9、2 PE8、3 PE7 | 1P、1IN1、1IN2 |
| 左后 M2 | 4 PE11、5 PE10、6 PE12 | 2P、2IN1、2IN2 |
| 右前 M3 | 7 PE13、8 PE15、9 PD9 | 3P、3IN1、3IN2 |
| 右后 M4 | 10 PE14、11 PD8、12 PD10 | 4P、4IN1、4IN2 |
| 信号地 | 13 或 14，选其中一针 | GND |

四轮的六针接头整组插进 MD04。现有开环固件无需额外接编码器 A/B 到 STM32。首次架空单轮核对转向。

### UPPER 线束针号

[底板到上层电源板接线图](V4.2_底板到上层接线.png)

| 两端针号 | 信号 | 两端针号 | 信号 |
|---:|---|---:|---|
| 1 | 3V3 | 2 | 逻辑 GND |
| 3 | CH1 搅拌 / PA4 | 4 | 5V |
| 5 | CH3 预留 / PA5 | 6 | CH2 水泵 / PG12 |
| 7 | 逻辑 GND | 8 | CH4 预留 / PD13 |

电源板元件面朝向自己，DC 口在左上、绿色输出端子在右侧时，`J_CTRL_LINK` 位于左下。靠下板边一排是 **1、3、5、7**，靠板内一排是 **2、4、6、8**。方形焊盘是 Pin 1。主控板接口在底面，线束制作后逐针用万用表确认同号相通。

同一视角下，右侧四个绿色端子从上到下依次是 **24 V 滑台、12 V 滑台、12 V 搅拌泵模块、12 V 预留**。左侧绿色端子是 12 V 电池输入。

## 二、双 USB 相机与 SPI

原板接线的 ESP32 实车验证版本为 `0.5.4-dual-usb-spi`。交接源码 `0.5.5-dual-usb-spi` 沿用这套接线。USB2 由 STM32 采集并经 SPI 转发给 ESP32。USB1 接 ESP32，仅适用于其 USB Host 支持的相机。已测的 `0C45:6367` 用 USB2。原板载 UART 继续跑控制和 ToF 状态。

### 相机到各自的 USB Host

相机 1 插载板上面的 `J_CAM1`，相机 2 插下面的 `J_CAM2`。排针保留原丝印「Hub下行1/2」。载板插线面朝自己、USB 母口在左下时，两组四针都是从上往下编号 1～4。

| 转接排针 | 信号 | 相机 1 用 `J_HUB1` | 相机 2 用 `J_HUB2` |
|---|---|---|---|
| Pin 1 | VBUS | 经核对能带相机负载的稳压 5 V | 经核对能带相机负载的稳压 5 V |
| Pin 2 | D− | ESP32 扩展 J2 `IO19`（Pin 20） | STM32 扩展 JP2 `PA11`（Pin 36） |
| Pin 3 | D+ | ESP32 扩展 J2 `IO20`（Pin 19） | STM32 扩展 JP2 `PA12`（Pin 33） |
| Pin 4 | GND | 主控板公共 GND | 主控板公共 GND |

STM32F407 的第二个 USB 控制器使用片内全速 PHY 时，数据脚固定为 `PB14/PB15`。核心板把 `PB14` 用作板载 W25Q128 片选，并且未引到排针。现有排针可用的 STM32 USB 数据对是 `PA11/PA12`。

两组数据线各自用短 USB 双绞线。相机的稳压 5 V 电源负极接公共地。STM32 原生 OTG 插口和载板 USB2 共用 PA11/PA12，使用 USB2 时保持原生 OTG 口空置。调试经 USB 转串口或 SWD。 [STM32F407 引脚与复用表](https://www.st.com/resource/en/datasheet/stm32f407zg.pdf)

按上述视角，最上方框出的 STM32 扩展 JP2 内，下排从左第 8 针是 PA11，上排从左第 9 针是 PA12。USB2 的 D− 接前者，D+ 接后者。

### STM32 到 ESP32 的 SPI 线束

以下为按现板排针选定的接线。STM32 用 SPI2 主机，ESP32 用 SPI2 从机。信号电平 3.3 V。READY 由 ESP32 发给 STM32，表示 ESP32 已准备好接收下一段数据。

| 信号 | STM32 扩展 JP1 | ESP32 扩展 J1 |
|---|---|---|
| SCK | `PB13`（Pin 24） | `IO12`（Pin 18） |
| MOSI，图像数据方向 | `PC3`（Pin 8） | `IO11`（Pin 17） |
| MISO，从机回传 | `PC2`（Pin 9） | `IO13`（Pin 19） |
| CS | `PB12`（Pin 23） | `IO10`（Pin 16） |
| READY | `PF3`（Pin 45，输入） | `IO14`（Pin 20，输出） |
| GND | GND（Pin 2） | GND（Pin 22） |

在 `/sensors` 启用「相机 2」可测试已验证的 USB2。USB1 换用兼容相机后，先逐路采集，再同时启用两路并遮挡镜头核对编号。两只现有相机并发可交由有足够带宽的外部 USB Host 采集。 [两端固件联调说明](../firmware/remote_control/DUAL_CAMERA_SPI.md) · [ESP32-S3 SPI 从机握手说明](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/spi_slave.html)

## 三、滑台、限位和舵机

这部分可按表先备线。现有固件只初始化步进和舵机输出，滑台驱动器的 ENA 状态、限位停机及舵机动作须在对应功能实现和验证后联动。

| 完成 | 标签 | 起点 → 终点 | 接法 | 数量 |
|---|---|---|---|---:|
| ☐ | SLIDE-PWR | 电源板 `J_SLIDE_12` **或** `J_SLIDE_24` → 滑台驱动器 VCC/GND | 两口择一，Pin 1 正、Pin 2 负。先查驱动器允许输入范围 | 2 芯 |
| ☐ | STEP-PUL | 主控板 `J_STEP_PUL` → 驱动器 PUL+/PUL− | Pin 1→PUL+，Pin 2→PUL−，STM32 PC6 | 2 芯 |
| ☐ | STEP-DIR | 主控板 `J_STEP_DIR` → 驱动器 DIR+/DIR− | Pin 1→DIR+，Pin 2→DIR−，STM32 PC7 | 2 芯 |
| ☐ | STEP-ENA | 主控板 `J_STEP_ENA` → 驱动器 ENA+/ENA− | Pin 1→ENA+，Pin 2→ENA−，STM32 PG6 | 2 芯 |
| ☐ | SLIDE-M | 滑台驱动器 A+/A−/B+/B− → 28HS30-0604A-013 | 按电机图，黑/绿一相、红/蓝一相。断电测电阻确认绕组，再按黑→A+、绿→A−、红→B+、蓝→B−接。驱动电流按每相 0.6 A 及驱动器标注的电流定义设置 | 4 芯 |
| ☐ | LIMIT-L | 主控板 `J_LIMIT_LOW` → 下限位干接点 | Pin 1=PB7，Pin 2=GND。使用无源触点 | 2 芯 |
| ☐ | LIMIT-H | 主控板 `J_LIMIT_HIGH` → 上限位干接点 | Pin 1=PB4，Pin 2=GND。使用无源触点 | 2 芯 |
| ☐ | SERVO | 主控板 `J_SERVO` → 夹爪舵机 | 5264-3P，1=I/O、2=5 V、3=GND。5 V 来自主控板 SYS_5V，装前确认舵机峰值电流与 5 V 电源余量 | 3 芯 |
| ☐ | STOP | 主控板扩展 `J_EXT_STM_JP2` Pin 17（PE0）→ 常开停车按钮 → 同接口 Pin 1（GND） | 按下把 PE0 拉低，为软件停车输入。可选，单独统计 | 2 芯 |

滑台控制口为共阳接法，三组 Pin 1 都是 5 V，PUL−、DIR−、ENA− 各是独立的信号回路。驱动器的 VCC/GND 用 `SLIDE-PWR` 那一组单独供电。使用 24 V 支路时，断电给 `JP_BOOST_EN` 的 **1–2** 装跳帽使能，接驱动器前实测 `J_SLIDE_24` 电压。Pin 2–3 为停用升压，停用时该输出口仍可能有接近输入电压。使用 12 V 时接 `J_SLIDE_12`。 [TI TPS55340 数据手册](https://www.ti.com/lit/ds/symlink/tps55340.pdf)

## 接线顺序

1. 两组电池先断开。先做并标好 5 V、DRIVE、三条 ToF 和 UPPER 线束。核对两端针号及电源极性，再接 IRF540 控制线和负载线。
2. 用已确认的稳压 5 V 起主控，确认 UART 在线和三个 ToF 通道。用现有相机在 USB2 验证相机 2 和 SPI。USB1 接兼容相机时单独验证。
3. 分别接通底盘电池、上装电池，架空核对四轮，分别短时测试 CH1 搅拌、CH2 水泵。滑台和舵机留到对应程序联调。

线长按实车安装后的走向量。电池、泵、搅拌器和滑台的动力线使用按实测电流选型的线材，和 USB/ToF 信号线分开扎束。

依据：[V4.2 板级接线](../hardware/pcb/robot_electronics_v4_2_four_layer/WIRING.md) · [焊盘针号网络](../hardware/pcb/robot_electronics_v4_2_four_layer/PINOUT.csv) · [现板联合测试](../firmware/remote_control/BENCH_TEST_20261120.md) · [USB 配置](../firmware/remote_control/USB_PINMAP.md) · [pcb_v2 配置](../firmware/board_profiles/WIRING_PCB_V2.md) · [MD04 供电资料](03-power.md)。
