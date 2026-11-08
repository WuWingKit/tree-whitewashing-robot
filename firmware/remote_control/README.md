# 电脑无线遥控底盘测试程序

2026-11-08首版：电脑浏览器 → ESP32-S3热点/HTTP → USART → F407 → 酷点四路TB6612 → 四轮差速底盘。

先看[完整接线表](WIRING.md)，再看[示例资料核对记录](SOURCE_REVIEW.md)。本版针对底盘低速**开环**遥控，不含滑台、喷涂、摄像头、ToF、自动避障和OTA。编码器不用接到STM32；显示的是PWM指令，不是实测车速。

## 提供的文件

|目录/文件|内容|
|---|---|
|`stm32/App`|F407 HAL主程序、固定引脚与轮向配置|
|`common`|两板共用的12字节CRC帧；可测试的运动/停车逻辑|
|`esp32/RemoteControl`|Arduino S3工程、嵌入式网页源文件|
|`tools/prepare.py`|从本地正点原子压缩包抽取ST HAL/CMSIS，生成Keil工程与网页头文件|
|`tools/build_stm32.py`|使用ARM Compiler 5命令行生成HEX、BIN|
|`tools/build_esp32.py`|使用已安装Arduino CLI在英文路径构建S3|
|`tools/test_core.py`、`tests`|共享C逻辑及浏览器行为测试|

原硬件资料不修改；抽取的第三方库放在被Git忽略的`.vendor`目录，不把限制用途的厂商应用源文件直接作为本项目公开代码。

## 1. 准备工程

在仓库根目录，用Python 3执行：

```powershell
python firmware/remote_control/tools/prepare.py
```

默认查找本仓库`硬件资料`目录中的`3，标准例程-HAL库版本.zip`；资料位于别处时：

```powershell
python firmware/remote_control/tools/prepare.py --materials "你的硬件资料目录"
```

生成`stm32/RemoteControl.uvprojx`、ST依赖和Arduino所需`src/rc_protocol.*`、`web_ui.h`。修改网页或公共协议后重跑。无需安装Python第三方包。

## 2. STM32编译与烧录

用Keil打开生成的`stm32/RemoteControl.uvprojx`，选择STM32F407ZGTx、ARM Compiler 5，编译。工程继承旧模板的设备包标识；若本机没有该版DFP，在Keil安装/选择可用STM32F4设备包即可。若只安装了Compiler 6，需要先适配旧CMSIS/启动文件；本次验证使用Compiler 5。

Keil的Debug选用ST-Link、Port选SW，按实际连接配置下载后复位运行。本机也可以直接命令行编译：

```powershell
python firmware/remote_control/tools/build_stm32.py --keil "E:/Keil_v5"
```

输出`stm32/build/remote_control.hex`及`.bin`，应用地址**0x08000000**，没有IAP偏移。使用ST-LINK下载HEX（地址自带）或BIN（手动地址0x08000000），下载后复位运行。程序会覆盖原有演示固件，首次可先备份原程序。

PF10板载灯约每500ms切换，PF9表示控制启用；若实物灯极性/板版不同，以网页状态为准。启动不自动转轮。先断开电机电池，确认程序在运行和状态串口接通，再接驱动电源。

## 3. ESP32编译与烧录

为尽量利用本地`esp32s3_wifi.ino`，本轮S3程序使用Arduino框架。后续相机工程仍可使用ESP-IDF，两者是分别编译烧录的工程，本版并未同时集成相机。

Arduino IDE安装Espressif的`esp32`开发板包**2.0.17**，打开`esp32/RemoteControl/RemoteControl.ino`。这是本次实际编译通过的版本；3.x尚未验证。旧GCC对中文工具链路径存在兼容问题，建议把工具链与S3工程放在简短英文路径。

|设置|值|
|---|---|
|Board|ESP32S3 Dev Module|
|Flash Size|16MB（N16）|
|Flash Mode|QIO 80MHz，不能选OPI Flash|
|Partition Scheme|Default 4MB with spiffs；本版仅使用默认布局，其余Flash未分配|
|PSRAM|OPI PSRAM（R8；本版不依赖其容量）|
|USB CDC On Boot|Disabled，日志走板载USB转串口|
|Upload Mode|UART0/硬件USB可选项中使用UART下载方式|
|端口|开发板USB转UART口对应COM|
|串口日志|115200|

`WiFi.h`、`WebServer.h`、`HardwareSerial`随ESP32核心提供，不额外装第三方网页库。上传失败可按住BOOT、按一下RESET、松开BOOT再重试；驱动按实物CH343P/CH340版本安装。不要把原生USB口接Hub后还期待通过同一口串口下载。

交付ZIP中也提供了现成S3合并BIN，可以直接使用厂家Flash Download Tool，**烧录地址0x0**，详见[烧录说明](esp32/FLASHING.md)。有Arduino CLI的电脑可执行：

```powershell
python firmware/remote_control/tools/build_esp32.py --work-dir "C:/espwork/tree_robot_rc"
```

此脚本要求预先安装上述核心和Arduino CLI，不会自动下载整套SDK；需要时用`--arduino-cli`指定可执行文件路径。

## 4. 电脑操作

1. 接好两板UART与共地，USB独立供电。先将四轮悬空。
2. 电脑连接Wi-Fi **TreeRobotTest**，密码 **TreeTest2026**。这是测试热点密码，不是家庭网络凭据；最多允许1个Wi-Fi终端接入。提示“无互联网”属正常，本系统只用局域网。
3. 浏览器打开 **http://192.168.4.1/**。实际IP也会在S3串口日志打印。
4. 确认显示STM32在线，点击“启用遥控”，等待状态为已启用，再按住按钮或按键。

|操作|动作|
|---|---|
|W / ↑|前进|
|S / ↓|后退|
|A / ←|原地左转，左侧反转、右侧前转|
|D / →|原地右转|
|Q / E|向前左弧/右弧转，一侧停止、一侧前转|
|松开运动按键/按钮|立即下发两侧零输出，仍保持启用|
|空格或红色停止按钮|停车并锁定，需重新启用|
|切换窗口、隐藏网页、网络断开|请求停车；会话超时后锁定|

不叠加多个方向键，以最后按下的单个方向为准。滑条10%～30%，默认20%；低占空比可能无法克服静摩擦，不能把百分比当车速。首轮不要加载上装、不要让轮子堵转。

## 5. 失联与异常处理

- 网页约80ms发送一次当前意图，按钮松开会额外请求零输出；请求失败锁定页面。
- S3仅接受当前控制会话和递增网页序号；电脑250ms未续约、STM32状态超时或停车按钮触发时发送STOP并销毁会话。
- S3约50ms向F407续发最新有效意图；**电脑过期后不继续续发旧运动命令**。
- F407仅允许显式ARM后运动；300ms无有效DRIVE则清零并取消启用。过期后新到的DRIVE不能自动恢复运动。
- F407每10ms最多调整10‰输出，反向必须经过零点并停留100ms；零输出和STOP直接清零。硬上限35%，不是电流保护。
- UART有CRC16、固定长度、噪声重同步；溢出或串口错误会停机。独立看门狗约1秒复位，实际时间受LSI误差影响；复位后保持未启用。

上面是软件时间预算，不是已测停车距离；浏览器/网络/驱动/车辆惯性均有影响。测试时保留可触及的断电开关。PE0按钮未接时不具备硬件急停；当前无障碍自动检测。

## 6. 协议和状态

UART 115200 8N1、3.3V TTL：`AA 55 | version=1 | type | seq_u16 | left_i16 | right_i16 | crc_u16`，所有16位数小端。左右以车身前进为正，单位‰，范围-350～350。CRC-16/CCITT-FALSE覆盖前10字节，poly=0x1021，init=0xffff，测试串123456789结果0x29b1。

类型1 DRIVE、2 STOP、3 ARM（左右必须为0）；状态0x80未启用、0x81已启用、0x82停车输入触发。STM32每100ms发状态，含最后接受序号与两侧当前软件输出。该反馈不能确认电机实际在转，应用层HTTP accepted只代表收到并转发请求。

热点和会话用于模型单人测试，不提供互联网远程接入或生产级权限管理。

## 7. 复现与尚未验证

见[测试记录](TEST_REPORT.md)。软件编译和主机逻辑测试不能替代上车验证：真实轮向、GPIO排针、供电、启动电流、RF连接和实际停车效果仍须逐项检查。
