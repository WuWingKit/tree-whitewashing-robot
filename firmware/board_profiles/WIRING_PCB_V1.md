# PCB V1接线与固件配置（样板配置）

接线配置建立于2026-11-10，PCB于2026-11-11完成原生导入、布线及规则检查。由`pinmaps.json`统一生成；现有车使用`legacy`，新PCB对应`pcb_v1`；构建时 `--profile` 为必填。尚未完成实物装配与供电/USB验证，不能给现有杜邦接线车直接烧录PCB配置。

|功能|现有杜邦接线 legacy|新载板 pcb_v1|
|---|---|---|
|M1 / 1P|PE9|PE9|
|M1 / 1IN1|PG11|PE8|
|M1 / 1IN2|PG12|PE7|
|M2 / 2P|PE11|PE11|
|M2 / 2IN1|PG13|PE10|
|M2 / 2IN2|PF8|PE12|
|M3 / 3P|PE13|PE13|
|M3 / 3IN1|PD3|PE15|
|M3 / 3IN2|PD6|PD9|
|M4 / 4P|PE14|PE14|
|M4 / 4IN1|PD7|PD8|
|M4 / 4IN2|PE2|PD10|
|ToF0 SCL|PF4|PC0|
|ToF0 SDA|PF5|PC1|
|ToF1 SCL|PF11|PD11|
|ToF1 SDA|PF12|PD12|
|ToF2 SCL|PC4|PC4|
|ToF2 SDA|PC5|PC5|

UART均保持PA2 TX→GPIO18 RX，GPIO17 TX→PA3 RX，115200/8N1/3.3V TTL；原生USB仍为GPIO19/20。PE0软件停车输入保留。PWM仍为PE9/11/13/14的TIM1四通道。

新增电机方向输出及PD11/PD12与板载SRAM/FSMC复用；两种固件均保持PG10高、禁止FSMC/SRAM和TFT初始化。三个软件I²C总线互不并联，INT/SHUT不接。

PCB上的AUX_3V3是独立传感器稳压输出，不接STM32或ESP32的板载3V3输出。两块主控的5V跳线在电脑USB供电时断开。相机VBUS来自各自Hub下行口，不并接SYS_5V。

## 编译选择

```powershell
python firmware/board_profiles/generate.py --check
python firmware/remote_control/tools/build_stm32.py --profile legacy
python firmware/remote_control/tools/build_stm32.py --profile pcb_v1
python firmware/remote_control/tools/build_integrated.py --profile pcb_v1
python firmware/sensor_test/tools/build_esp32.py --profile pcb_v1
```

STM32新PCB输出在`remote_control/stm32/build/pcb_v1/`；ESP32整合版输出在`remote_control/esp32/build/integrated/pcb_v1/`。当前杜邦接线车继续使用原目录中的legacy版本。只编译，不自动烧录。

手动Keil/Arduino构建：`ROBOT_WIRING_PROFILE=0`为现有接线，`=1`为新PCB，`=2`为V4.x四层板。**必须显式指定**——未定义时头文件会直接`#error`，不再静默退回legacy，避免插错板子却编译通过。所有生成的`robot_pinmap.h`必须来自同一份JSON，不能逐份修改。
