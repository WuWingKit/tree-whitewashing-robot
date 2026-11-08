# 硬件资料与示例代码核对记录

核对日期：2026-11-08。仅审阅与本次底盘遥控有关的电路、接口和源代码；未逐个阅读全部教学视频和无关例程，未执行厂商安装包。

|本地资料|实际读取内容|本次如何使用|
|---|---|---|
|酷点MD02/MD04，0-注意事项必看/0-驱动器使用注意事项～使用前必读.txt|PWM建议10～20kHz、方向普通IO、共地、≤13V、先架空、建议≤60%、5V口约5.3V警告|本版20kHz；上限35%；保持独立逻辑供电；先单轮测试|
|同目录/驱动器接线说明.pdf|M1～M4的IN/P与AB透传、先开环后闭环；M3 PWM处存在4P笔误|按实物3P分配M3；第一版不接AB输出|
|1-原理图/4驱版本/tb6612_driver2.pdf|两颗TB6612、稳压和接口连接，没有确认对外可用STBY|不设计虚构STBY接线，不照搬WHEELTEC电源结论|
|D305底盘/7-TB6612电机驱动器/5-STM32程序源码/4WD四驱小车源码-PID版本/codbot-4wd-Speed-pid-2023.7.25.zip|解包读取bsp_motor.c、bsp_timer.c、motor.c；工程依赖STM32F1xx HAL|沿用普通方向GPIO＋定时器比较值控制和左右侧差速结构；重新映射F407引脚，不直接烧F103工程，不复制未标定PID参数|
|正点原子/4，程序源码/3，标准例程-HAL库版本.zip，实验0-1 Template|main.c、sys.c、usart.c、Keil项目、HAL配置、启动与系统文件|沿用HAL初始化方式与8MHz HSE→168MHz配置；抽取ST的HAL/CMSIS及启动文件供本地构建|
|同ZIP/实验8 PWM输出实验/HARDWARE/TIMER/timer.c|HAL定时器初始化、PWM通道配置及比较寄存器更新|将单路LED PWM思路适配为TIM1四路20kHz电机PWM；不用例程的LED有效低极性|
|正点原子IO资源表及最小系统板开发指南|PG10 SRAM片选、PE9/11/13/14复用、UART与SWD|关闭FSMC/外部SRAM、PG10高，保留SWD，使用USART2|
|S3/esp32s3连接wifi示例.rar/esp32s3_wifi/esp32s3_wifi.ino|WiFi.h、Serial初始化、WiFi.begin与连接检测|保留Arduino框架和库调用方式，改为本地SoftAP，新增网页与UART转发；没有复制示例中的私人SSID/密码|
|S3/ESP32-S3-SCH.pdf|USB转串口与原生USB路径、GPIO19/20、板版本|串口下载口与相机Host口分开；本版不用原生USB|

## 代码来源边界

正点原子应用例程带学习用途等限制，因此不把其完整应用源码重新发布；本次自行编写板级适配和状态机，构建脚本从用户本地资料抽取ST库，保留库中原有版权头。所有厂商原始压缩包保持原位不修改。

Keil工程在本地由厂商模板生成并只保存在交付包中，公共仓库记录生成脚本，不把厂商整包传入Git。测试热点名/密码是新建的演示配置。

官方API核对：[乐鑫Arduino Wi-Fi](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html)、[串口](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html)、[官方WebServer示例](https://github.com/espressif/arduino-esp32/tree/master/libraries/WebServer/examples)。F407引脚也需结合ST数据手册与实物排针确认。
