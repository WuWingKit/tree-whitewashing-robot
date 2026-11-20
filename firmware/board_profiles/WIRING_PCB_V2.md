# PCB V4.2 程序与接线配置（pcb_v2）

统一源 `pinmaps.json` 的 `pcb_v2`，编译宏 `ROBOT_WIRING_PROFILE=2`。**必须显式指定**：三个构建脚本的 `--profile` 已改为必填，头文件在宏未定义时直接 `#error`，不再静默退回 legacy；Keil 三个目标各自带 `ROBOT_WIRING_PROFILE=0/1/2`。主控底盘电机、串口及三个 ToF 的 V1 管脚保持不变；新增以下引脚。

|用途|STM32|接口处理|
|---|---|---|
|滑台脉冲|PC6|经 NPN 下拉 PUL−，PUL+ 为5V|
|滑台方向|PC7|经 NPN 下拉 DIR−|
|滑台 ENA|PG6|经 NPN 下拉 ENA−，实际使能极性按驱动器|
|舵机信号|PB6|AHCT 缓冲到5V，5264 Pin1|
|IRF CH1 搅拌器|PA4|3.3V 逻辑控制|
|IRF CH2 水泵|PG12|3.3V 逻辑控制|
|IRF CH3/4|PA5/PD13|备用|
|下/上限位|PB7/PB4|干接点到地，上拉输入|

2026-11-20：用户已确认 IRF 模块高电平通电。四路 IRF 初始化为 LOW，网页可独立控制 CH1 搅拌与 CH2 水泵，CH3/4 保持 LOW。停止、失联、急停和 OTA 时关闭；新增 UART 命令与输出报告。步进/舵机仍仅初始化 LOW，本次未新增它们的运动功能，限位为上拉输入。ENA 为低不保证实际驱动器失能。详见 [现板联合测试说明](../remote_control/BENCH_TEST_20261120.md)。

FSMC/SRAM/TFT 保持禁用，PG10 保持高，避免控制信号与板载扩展总线争用。

```powershell
python firmware/board_profiles/generate.py --check
python firmware/remote_control/tools/build_stm32.py --profile pcb_v2
python firmware/remote_control/tools/build_integrated.py --profile pcb_v2
```

已编译输出位于 STM32 `stm32/build/pcb_v2/` 与 ESP32 `esp32/build/integrated/pcb_v2/`；没有烧录设备。Keil 的 RemoteControl_PCB_V2 目标已加入同一宏。当前四层 PCB 针号见 `hardware/pcb/robot_electronics_v4_2_four_layer/PINOUT.csv`。

高电平有效由用户于2026-11-20确认。程序初始化后四路IRF由高阻改为推挽LOW；可选外部下拉电阻的实际贴装情况仍需核对。程序尚未烧入设备，本次编译及主机测试不能替代实物接线验证。


## 2026-11-13 立创商城实料修订

控制板与独立电源板已改用可采购器件及真实库封装。GPIO 分配保持不变，未更改程序逻辑或烧录固件。排针的库焊盘编号存在变化，连接请按 `hardware/pcb/robot_electronics_v2/PINOUT.csv` 和 `WIRING.md`，不要沿用旧的排针脚号。原六位步进控制端子改为三只两位端子：J_STEP_PUL、J_STEP_DIR、J_STEP_ENA。

## V4.1 修订（2026-11-14）

上限位改为已从主控插座引出的 PB4；原 PB8 不在本项目 STM32 的两组插座针脚中。PCB、原理图和 `pcb_v2` 的配置已同步。V4.1 工程在 `hardware/pcb/robot_electronics_v4_1_bottom_ports/`。12组3V3/5V排针和4个绿色端子装在底面。控制板与电源板各接口的用途、电压和极性已补中文丝印。IRF CH1 约定为搅拌，CH2 为水泵，CH3/4 预留；这仅约定接口分工，不表示已实现这些负载的动作流程。

V4.1修正四路IRF控制：原PG0/PG1/PG2/PG3未引至模块插座，依据实际可布线位置改为PA4/PG12/PA5/PD13。对应CH1搅拌、CH2水泵、CH3/4预留，PCB及原理图同步。
