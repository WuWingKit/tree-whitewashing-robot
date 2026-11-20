# 官方双摄像头示例与实购S3板复核

复核日期：2026-11-10。结论：ESP32-S3方案具备软件基础，但不能把芯片支持USB Host等同于实购开发板的Type-C口已具备完整的Host供电和CC源端配置。当前实测停在根端口连接阶段，尚未到UVC格式匹配。

## 1. 本地硬件资料检查范围

已检查 `硬件资料/ESP32-S3开发板N16R8焊接` 中的：

- `ESP32-S3-SCH.pdf`；
- 原始RAR内 `ESP32-S3-SCH-V1.4.pdf`；
- `ESP32-S3-Metric.pdf`、`ESP32-S3-inch.pdf`；
- `封面图.jpg`、型号说明与安装说明；
- 原始RAR文件目录及随附LED、Wi-Fi、BLE示例。

两份原理图文件哈希不同，但提取并去除空白后的电路标识文本相同；两图均为同一V1.4电路内容。随附示例是LED、Wi-Fi、BLE等演示，未找到USB Host双摄像头或板上Host供电控制示例。随附MicroPython旧固件不是本项目当前运行的固件，不能用它的功能证明或否定当前IDF UVC能力。

板面商品图说明为S3 N16R8、CH343下载；电脑烧录时已实际识别16MB Flash与8MB OPI PSRAM，与本项目使用的存储配置相符。

## 2. 原理图能确认的电路

|位置|电路|含义|
|---|---|---|
|USB1|D+/D−接CH343P的UD+/UD−|下载/串口口，不能直接作为S3相机数据口|
|USB2|D+接GPIO20，D−接GPIO19|S3原生USB数据口，支持本项目Host驱动使用|
|D1|USB1 VBUS经二极管进入板上5V|COM供电存在二极管压降，未给出相机负载能力保证|
|D2|USB2 VBUS经二极管进入板上5V|单独这条二极管路径不允许板上5V反向供到USB2|
|USB-OTG，0Ω/0603|与D2并联的旁路|接通可建立板上5V到USB2 VBUS的通路；图纸不证明实物已安装|
|R9/R10，5.1kΩ|USB2 CC1/CC2分别下拉至GND|符合Type-C UFP/Sink的Rd配置，不是源端Host的Rp配置|
|J1第21脚、D3、IN-OUT|5Vin经D3进入板上5V，另有0Ω旁路|尺寸图上可见5Vin标识；不能把这个脚默认当作保证能向外供电的接口|
|U3 CJ6107A33GW|板上5V转3.3V|用于3.3V系统，不给相机生成5V|
|USB-JTAG、R11、GPIO3|GPIO3上拉配置|不是VBUS供电开关，不能与USB-OTG旁路混淆|

尺寸图提取的器件标识中有IN-OUT，但没有找到USB-OTG、USB-JTAG、R11的同名标识；因此不能直接根据尺寸图指定实物上的短接位置，也不能认定实购PCB与V1.4所有可选器件的装配完全一致。需要实物照片或测量才能进一步确认。

参考原理图中未看到通过普通GPIO控制上述0Ω旁路或CC电阻的开关电路。启用软件Host不会自动接通这个旁路，也不会将板上Rd电阻变成Rp。

## 3. 官方示例如何实现

官方目录：[`usb_hub_dual_camera`](https://github.com/espressif/esp-iot-solution/tree/e5b43f56afdeb0114d9f1a95b583ff7f7cd6a76a/examples/usb/host/usb_hub_dual_camera)。本次将该目录最近提交 `e5b43f56afdeb0114d9f1a95b583ff7f7cd6a76a` 的README、公共/S3配置、app_uvc.c、app_https.c和组件清单下载到本地复核。

### 硬件前提

[官方README](https://github.com/espressif/esp-iot-solution/blob/e5b43f56afdeb0114d9f1a95b583ff7f7cd6a76a/examples/usb/host/usb_hub_dual_camera/README.md)明确要求有USB OTG Host能力的开发板、正确的USB连接及需要时为USB设备提供5V；连接图画出5V、GND、D+、D−四条连接。它没有指定本项目的双Type-C核心板，也没有承诺该核心板通过原生Type-C口即可直接给任意Hub供电。

### 软件流程

1. `app_wifi_init()`启动网络。
2. `app_uvc_init()`安装USB Host，默认由IDF初始化内部PHY为OTG Host，并创建处理USB事件的任务。
3. `uvc_host_install()`注册UVC类驱动，后台处理相机接入。
4. 驱动读取USB/UVC描述符，示例筛选MJPEG格式并保存相机支持的分辨率。
5. 每台相机有自己的对象、工作任务、事件组和帧队列；选择后open/format-select/start。
6. PSRAM保存帧缓冲，接收回调将帧交给队列；HTTP任务发送完归还帧，避免一直占用驱动缓冲。
7. 浏览器使用HTTP multipart MJPEG预览；连接断开时停止对应流。

参考代码：[app_uvc.c](https://github.com/espressif/esp-iot-solution/blob/e5b43f56afdeb0114d9f1a95b583ff7f7cd6a76a/examples/usb/host/usb_hub_dual_camera/main/app_uvc.c)、[app_https.c](https://github.com/espressif/esp-iot-solution/blob/e5b43f56afdeb0114d9f1a95b583ff7f7cd6a76a/examples/usb/host/usb_hub_dual_camera/main/app_https.c)。示例可管理多台相机，但同时打开数量仍受主机资源与带宽限制；不能从示例名称推导出S3保证双路720P/30fps。

## 4. 与当前项目的实际差异

|项目|官方示例|本项目|是否解释当前设备数0|
|---|---|---|---|
|Host安装、内部PHY|usb_host_install|相同入口，skip_phy_setup=false|未发现这里缺失|
|外部Hub支持|开启|已开启|未发现这里缺失|
|控制传输缓冲|4096字节|4096字节|相同|
|USB接收FIFO偏置|IN优先|IN优先|相同|
|MJPEG|筛选并采集|筛选并采集，电脑已验证两台支持|尚未到此阶段|
|缓冲|PSRAM，按帧大小/可用内存动态分配|PSRAM，两帧，每帧上限256KiB|影响采集，不解释根端口未连接|
|同时运行|每相机独立任务，可请求多路|两台枚举、一路采集、切换|不会禁止第二台枚举|
|视频网页|连续multipart MJPEG|独立81端口轮询JPEG，80端口保留遥控/OTA|影响传输方式，不解释枚举失败|
|UVC EOH/ERR检查|关闭|开启|可能影响不规范相机的帧处理；连接/枚举成功后才相关|
|PSRAM取指/只读数据|S3配置开启|未开启|性能/缓存策略差异，不解释没有物理连接报告|
|Host供电|作为外部硬件前提|实际VBUS及CC接法未确认|需要优先补齐验证|

官方默认分区表偏移0x9000、8MB Flash；本项目保持0x8000、16MB Flash及原OTA分区。这些是按实际板子和既有OTA布局调整，不能为“照抄”而覆盖成官方分区。

## 5. 现有实测与可执行方向

电脑端已确认两台SHUNCCM均可使用MJPEG 1280×720/30fps，短时同时采集通过。S3诊断版确认Host查询ESP_OK、两个客户端已注册、root_connected=false、enumerated_devices=0。前者证明电脑端相机工作，后者证明本次S3接法尚未建立USB连接。

下一步应先完成Host硬件适配：

- 若保留原Type-C口，需要确认USB-OTG旁路、5V来源/负载能力、CC源端角色及回灌路径；不能只盲焊旁路。
- 若不改板，可用完整的USB Host转接方案，将GPIO20接D+、GPIO19接D−、GND共地，USB设备的VBUS由独立稳定5V支路提供。若使用USB-A母口，配USB-A上行Hub；若保留Type-C Hub，上游Host端仍须正确实现CC源端配置。
- 不能仅因“Hub有独立电源”就认定数据角色与接线正确，也不能把外部5V直接与电脑USB5V并联。
- 先一台相机成功枚举并在低分辨率下出图，再测第二台和双路资源占用；当前不需要为了root_connected=false去关闭MJPEG帧头检查。

源端Rp与设备端5.1kΩ Rd的区别，参考[Infineon Type-C终端电阻说明](https://community.infineon.com/t5/Knowledge-Base-Articles/USB-Type-C-connector-Rp-Rd-and-Ra-termination-resistors/ta-p/253544)。[乐鑫专用ESP32-S3-USB-OTG开发板](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-usb-otg/user_guide.html)提供专用Type-A Host口、接口切换和供电电路，不能将其板级GPIO控制照搬到本项目核心板。

用户当前没有电压测量工具，因此本报告不声称“实物VBUS确定为0V”，也不指定未经确认的焊接位置。
