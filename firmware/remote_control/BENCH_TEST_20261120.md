# V4.2 现板联合测试：摄像头、搅拌器、水泵与三路 TOF

版本 `0.4.0-upper-usb-tof`，2026-11-20。使用 `pcb_v2`，不是 legacy 或 pcb_v1。

这是一对配套的 STM32F407 / ESP32-S3 程序。STM32 采集三只 TOF050C、执行底盘控制与搅拌器/水泵开关；ESP32 提供热点、网页、USB UVC 图像测试及 OTA。电源板是电源和信号转接板，没有第三份需要烧录的程序。此次只生成文件，没有烧录设备，摄像头与负载的实物结果仍待接线验证。

> 2026-11-22补充：ESP32已增加固定USB引脚配置及诊断显示，版本0.4.1-usb-pinmap，详见[USB连接说明](USB_PINMAP.md)。STM32程序不变。

## 1. 摄像头的四根线必须怎样接

PCB USB 母座到 J_HUB1/J_HUB2 已有数据走线，但没有在板上自动接到 ESP32，也没有板载 USB Hub 芯片。每个 J_HUB 都需要手动接到外置 Hub 的对应下行口，不能只接 5V/GND。

|PCB 转接口|Hub 对应下行口|
|---|---|
|J_HUB1/2 Pin1：5V / VBUS|同一路 VBUS / 5V|
|Pin2：D−|同一路 D−|
|Pin3：D+|同一路 D+|
|Pin4：GND|同一路 GND|

完整链路：`相机 → PCB USB母座 → PCB走线 → J_HUB四线 → Hub下行口 → Hub上行口 → ESP32-S3原生USB/OTG`。

两台相机各接一组，Hub 上行口接一次 ESP32。原生 USB 数据脚为 GPIO19=D−、GPIO20=D+；优先使用开发板原生 OTG 插口。COM/CH343 是烧录和日志口，不接摄像头。如果相机自身已经通过 USB 插头接入，就不需要再把相机裸露的四针重复并接；裸线四针和 USB 插头是同一路 USB 的两种接法。

J_HUB 的 VBUS 属于 Hub 下行口供电，与主控板 SYS_5V 分开。USB 只能接 5V，不能接 12V。Hub 供电、Type-C 主机角色/CC 及防反灌必须按实物接线确认；软件不能产生 USB VBUS 电源。D+/D−使用短 USB 数据线，不使用长散线。

## 2. 搅拌器和水泵

用户已确认：**高电平通电、低电平断电**。CH1=搅拌电机=PA4；CH2=水泵=PG12。CH3/4（PA5/PD13）本版固定 LOW，不提供网页动作。滑台和舵机保持原有初始化，没有新增它们的运动程序。

主控板 J_UPPER 与电源板 J_CTRL_LINK 同号连接：

|针号|作用|
|---|---|
|1|3V3|
|2|GND|
|3|CH1 / PA4 / 搅拌电机|
|4|5V|
|5|CH3 / PA5 / 预留|
|6|CH2 / PG12 / 水泵|
|7|GND|
|8|CH4 / PD13 / 预留|

电源板元件面朝向观察者，DC 口在左上、四个绿色输出端子在右侧时，左下角 J_CTRL_LINK 排列：

```text
                朝板内
          2 GND   4 5V   6 CH2   8 CH4
          1 3V3   3 CH1  5 CH3   7 GND
                靠下板边
```

左下方的方形焊盘为 Pin1。从另一面看会左右反转，不能按照片左右代替针号。

电源板 J_IRF_CH1 接外置 IRF540 模块的第1路控制口，J_IRF_CH2 接第2路控制口。每组三针 Pin1=逻辑负极/GND、Pin2=逻辑正极/3V3、Pin3=控制信号。电源板 J_IRF_12（“12V搅拌泵模块”）正负两线接 IRF540 的供电正负。搅拌器正负接模块 CH1 负载输出，水泵正负接 CH2 负载输出；负载负极不要另外直连电池负极绕过开关。

初始化后四路输出均 LOW。停止、急停输入、租约到期或 UART 异常会清零并锁定，重新启用不会恢复上次 ON。复位到 GPIO 初始化之前的电平由硬件决定，PCB 可选下拉电阻是否已装需按实板确认。

## 3. 三只 TOF050C / VL6180X

三个接口均接四线，Pin1=3V3、Pin2=GND、Pin3=SCL、Pin4=SDA，INT/SHUT 不接。

|板上丝印|STM32 SCL|STM32 SDA|网页 ID|
|---|---|---|---|
|测距1 / J_TOF0|PC0|PC1|0|
|测距2 / J_TOF1|PD11|PD12|1|
|测距3 / J_TOF2|PC4|PC5|2|

三条软件 I²C 独立，不能把三组 SCL/SDA 并在一起；同为 0x29 地址可分别工作。GPIO 来自当前 PCB 配置，不能使用早期 legacy 的 PF4/PF5/PF11/PF12 接线表。本版保留逐路初始化、测量超时、错误码、重试及过期处理。先用白色目标在 30–100mm 测试；尚未实现商家标称 50cm 的扩展量程。

板间 UART 已由 PCB 连接：ESP GPIO17 TX→STM PA3 RX，STM PA2 TX→ESP GPIO18 RX，115200/8N1，共地。

## 4. 烧录与打开网页

STM32：`stm32/build/pcb_v2/remote_control.hex`。用 ST-Link / Keil 的 `RemoteControl_PCB_V2` 目标下载；BIN 的起始地址为 `0x08000000`。

ESP32：`esp32/build/integrated/pcb_v2/`，首次用完整四个 BIN，不能混用旧分区或旧 APP：

|文件|写入地址|
|---|---|
|bootloader.bin|0x0|
|partitions.bin|0x8000|
|reset_otadata.bin|0xe000|
|app.bin|0x10000|

使用开发板 COM/USB-UART 口，按实际 COM 号执行（不会由编译脚本自动烧录）：

```powershell
python firmware/remote_control/tools/flash_integrated.py --profile pcb_v2 --port COM实际编号
```

可用 `--esptool` 指定已有 esptool.exe；保留 N16R8 与本工程分区设置。OTA 仍在 `/update`，沿用工程本地登录配置；上传本版 app.bin 和对应 ota.json。升级准备会等待底盘、上装输出及相机停止。

两端刷入后：

1. 连接 Wi-Fi **TreeRobotTest**，密码 **TreeTest2026**。
2. 打开 **http://192.168.4.1/**。确认 STM32 在线，点击“启用遥控”，分别点“开启搅拌电机”“开启水泵”；再次点击关闭。测试泵时无需按底盘方向键，零速心跳仍会续约。
3. “停止并锁定”或空格会同时关闭两路负载并停车。网页失焦、关闭、断网后也会关闭。需要重新启用后手动开启，不会自动恢复。
4. 打开 **http://192.168.4.1/sensors** 查看三路 TOF 和 USB 线路诊断。相机可同时接入，但只选一路 MJPEG 采集，逐台切换测试。
5. 先确认 USB 设备列表有 Hub/摄像头，再选相机和低分辨率格式，最后确认 `frames` 增加且画面随遮挡/场景改变。能枚举不等于已经收到图像；`controller_power_flag` 不等于实测 VBUS 电压。

网页显示的上装状态是 STM32 执行的 GPIO 指令，不是实际电流、搅拌转速或水流反馈。`tof.valid=false` 时距离不可用。ESP32-S3 是 USB Full Speed，当前只提供双相机接入、单路采集切换，不承诺双路同时 720p。

## 5. 程序接口与复现

- `GET /api/status`：原遥控状态 + 三路 TOF + `upper`（请求状态、MCU 输出报告、报告序号/年龄）。
- `GET /api/sensors/status`：三路 TOF、相机格式/帧计数、USB 枚举诊断。
- `GET /api/usb/status`：USB 连接状态、VID/PID 和设备类别。
- `POST /api/drive`：原 token/seq/left/right，加 `mixer=0或1`、`pump=0或1`；省略两项会关闭两路，避免旧网页保留旧 ON。
- `POST /api/upper`：token/seq/mixer/pump，必须已有启用会话并以小于250ms间隔续约；202 只代表已转发，实际输出报告看 status。网页使用 drive 接口同时续约，因此不需要手工发心跳。
- `POST /api/stop`：无 token 也可全部停止。升级期间拒绝启用/动作。

UART 保留12字节CRC16协议；新增 `0x04`，left=bit0搅拌/bit1水泵，right=0；新增 `0x83` 状态，left=MCU输出位图，right=1（高电平有效协议标识）。三路测距仍为 `0x90/91/92`。MCU 的 DRIVE 和已启用的 UPPER 心跳分别检查300ms超时，任一到期均全部关闭；ESP网页租约为250ms。相机图像不经过这条 UART。

```powershell
python firmware/board_profiles/generate.py --check
python firmware/remote_control/tools/test_core.py
node firmware/remote_control/tests/test_web.mjs
python firmware/remote_control/tools/build_stm32.py --profile pcb_v2
python firmware/remote_control/tools/build_integrated.py --profile pcb_v2
```

官方参考：[ESP32-S3 USB 硬件引脚](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/schematic-checklist.html)、[USB Host 与 Hub 支持](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_host.html)、[本工程使用的 UVC 2.6.0](https://components.espressif.com/components/espressif/usb_host_uvc/versions/2.6.0/readme)。
