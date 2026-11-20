# 资料核对与验证记录

2026-11-10。先核对本地硬件资料，再参考乐鑫官方驱动；没有把供应商 Arduino 例程原样当成 STM32 工程，也没有把单摄像头例程声称为双路视频实测。

## 本地资料

- `硬件资料/TOF050C测距传感器模块/封面图.jpg`：型号 TOF050C、I²C、VL6180、3.0–5V、最大电流 40mA、标称盲区 0–2cm。
- `例程源码/arduino/vl6180x.ino`、`Adafruit_VL6180X.zip`：采用 Adafruit VL6180X 库；初始化 ID=0xB4；16 位寄存器地址、默认 I²C 0x29、8 位毫米结果；必须同时检查 range status。
- `例程源码/arduino/接线说明.png`：图中标题写 TOF400C，属于通用接线模板；没有把这个标题作为实购型号。该目录未找到可直接使用的 STM32 例程。
- `vl6180.pdf`：ST DocID024986 Rev13，73 页。核对 I²C、异步单次测量、interrupt status 与错误检查；资料的器件长距离能力不等于随附例程默认范围。
- F407 最小系统板资料：现有 legacy 接线选择 PF4/PF5、PF11/PF12、PC4/PC5。另有独立 pcb_v1 载板候选，ToF0 改 PC0/PC1、ToF1 改 PD11/PD12、ToF2 不变；统一配置及全部新旧信号见 [PCB 接线表](../board_profiles/WIRING_PCB_V1.md)，均保持 SRAM/FSMC 禁用。
- S3 资料中的 COREBOARD V1.4 参考原理图：原生 USB D-/D+ 为 GPIO19/20；USB-UART 是另一条下载链路。参考图含 VBUS 配置和 CC 下拉，实购板 Host 供电配置仍需实物核对。
- 本地相机资料不足以证明 Full Speed 下 MJPEG 的具体模式；由新固件枚举实际描述符并记录错误。

## 官方驱动与构建版本

- [ESP-IDF v5.4.2](https://github.com/espressif/esp-idf/tree/v5.4.2)，提交 `f5c3654a1c2d2a01f7f67def7a0dc48e691f63c0`。
- [Espressif esp-usb / usb_host_uvc](https://github.com/espressif/esp-usb/tree/51fcfb96417f8634f0117591d76b1aca645ef185/host/class/uvc/usb_host_uvc)，提交 `51fcfb96417f8634f0117591d76b1aca645ef185`，manifest 版本 2.6.0，Apache-2.0。
- 参考 [usb_hub_dual_camera 官方示例](https://github.com/espressif/esp-iot-solution/tree/master/examples/usb/host/usb_hub_dual_camera) 的 Hub 配置、驱动安装、MJPEG 格式枚举、PSRAM 帧缓冲及 stop/close 顺序。应用层另外实现固定两相机、单 worker 管理、先关闭再切换、有界缓存、HTTP/串口隔离；未复制示例的动态链表并发访问结构。
- [ESP32-S3 USB Host 文档](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/api-reference/peripherals/usb_host.html)：原生 Full Speed，不因 Hub 标注 USB3.0 而升级链路速率。
- 编译工具：Xtensa `esp-14.2.0_20241119`；本机 ARMCC 5 构建 F407 HAL。
- 编译使用本地独立目录 `E:/espwork/tree_robot_v3`；没有升级或替换已有 Arduino 2.0.17 环境。

源码依赖获取：

```powershell
git clone --branch v5.4.2 --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git
git clone https://github.com/espressif/esp-usb.git
git -C esp-usb checkout 51fcfb96417f8634f0117591d76b1aca645ef185
```

按 IDF 官方安装说明安装 S3 工具链与 Python 环境，然后设置 `TREE_UVC_PATH` 指向 `esp-usb/host/class/uvc/usb_host_uvc`，在英文路径构建。随附 `tools/build_esp32.py` 将工程复制到英文路径；参数可覆盖本机路径。为避免 IDF6 才需要的外部 usb 依赖和无关组件在线下载，本工程对固定 IDF5.4.2 使用本地驱动，构建脚本关闭 Component Manager，USB 核心使用 IDF 自带组件。

若自行在 IDF 命令行构建，设置 `IDF_COMPONENT_MANAGER=0`、`TREE_UVC_PATH` 后可运行 `idf.py set-target esp32s3`、`idf.py build`。不要改用旧 Arduino 构建脚本编译本原生 IDF 工程。

## 已执行验证

- F407 完整源代码 ARMCC 编译、链接、生成 HEX/BIN/AXF。
- ESP32-S3 完整 ESP-IDF 编译、链接、生成启动程序/分区/APP，并通过应用分区大小检查及 esptool 镜像校验。APP 862112 字节，factory 分区 3MiB。
- 原运动控制测试及新增三路传感器帧测试：有效/无效距离、CRC、传感器帧不能控制电机或延长运动租约。
- PC 接收程序通过真实本机 HTTP 测试：JSON、CSV 中无效距离留空、JPEG 接收、相同帧序号去重、启动/停止请求。
- 网页 JavaScript 语法检查。

尚未执行：摄像头/Hub 枚举实测、相机帧率、测距准确度、插拔恢复、USB 供电负载、Wi-Fi 稳定性、整机电磁干扰。构建时缺少可选 `ESP_ROM_ELF_DIR` 产生的是调试符号 gdbinit 警告，不影响上述镜像生成；不是相机实测结论。

本次没有打开串口、没有改变电脑 Wi-Fi、没有下载至开发板。
