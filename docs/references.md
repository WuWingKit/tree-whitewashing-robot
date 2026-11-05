# 资料来源与核对范围

## 本地资料索引（不随仓库分发）

|资料|已用于核对的内容|公开处理|
|---|---|---|
|用户最新《已买到的宝贝.pdf》|采购SKU、数量、行金额、订单时点状态|仅发布脱敏CSV，不发布原件|
|酷点MD02/MD04附送资料：0-注意事项必看/0-驱动器使用注意事项～使用前必读.txt|5V口约5.3V警告、PWM/电压/电流注意|仅列结论，原件本地保存|
|同目录：1-原理图/4驱版本/tb6612_driver2.pdf|TMI3255S、5V/3A标注、双TB6612与接口|不上传整包|
|同目录：0-注意事项必看/驱动器接线说明.pdf|PWM与方向、编码器透传|以实物丝印复核|
|正点原子STM32F407最小系统板旧资料|板载连接、外设占用、引脚分配草案|旧版本须与实物核对，不上传整包|
|ESP32-S3开发板附送ESP32-S3-SCH.pdf，标题COREBOARD V1.4|GPIO19/20、USB桥CH343P、CC和零欧供电路径|须对照到货版本|
|用户CAD及选型截图|环抱、升降、搅拌、喷嘴流量、舵机、电机和电池参数|截图仅证明设计或商家描述|

厂商参考代码不等于本项目已开发固件。仓库未上传视频、安装包、第三方压缩包或驱动程序。

## 官方技术资料

- [STM32F407ZG产品资料](https://www.st.com/en/microcontrollers-microprocessors/stm32f407zg.html)：芯片资源和参考手册入口；引脚须结合封装与实际板图。
- [乐鑫ESP32-S3 USB Host](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/usb_host.html)：USB Full Speed及Host软件架构；需锁定实际使用的IDF版本。
- [乐鑫ESP-IoT-Solution USB资料](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/index.html)：USB外围方案入口，具体UVC组件兼容性需要实验。
- [乐鑫ESP32-S3-DevKitC-1](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/index.html)：官方开发板资料；已购兼容板不是当然同一电路。
- [东芝TB6612FNG官方产品与数据手册](https://toshiba.semicon-storage.com/ap-en/semiconductor/product/motor-driver-ics/brushed-dc-motor-driver-ics/detail.TB6612FNG.html)：芯片电压、电流与额定条件；不代替具体成品板指标。
- [ST VL6180X](https://www.st.com/en/imaging-and-photonics-solutions/vl6180x.html)：芯片测距资料；尚未证明订单TOF050C必为该芯片或完全相同扩展算法。

文档中的几何公式、分时采集、状态机和供电建议是本项目设计推导，尚未成为实测结论。官方网页访问核对日期2026-11-05。
