# 测试与构建记录

日期：2026-11-08。以下仅为软件编译和主机测试；没有连接真实F407、S3或底盘，不能称已完成硬件联调。

## 已完成

|项目|工具与结果|
|---|---|
|F407真实目标编译与链接|ARM Compiler 5.06 update 7，Cortex-M4/FPv4-SP，ST HAL/CMSIS来自本地正点原子HAL模板；成功生成HEX/BIN|
|F407固件大小|ROM合计8452字节（Code 7912、RO Data 528、RW初值12）；ZI 1948字节；不含任何上车性能判断|
|CRC与协议|直接编译运行共享C源码；校验已知CRC向量、负数、96种单比特损坏、错帧后恢复和连续噪声，全部通过|
|控制状态机|启用前禁止运动、范围拒绝、软启动、300ms超时、超时后新DRIVE不得恢复、STOP、PE0逻辑、计时器回绕，全部通过|
|换向|先降至零、100ms停留、松键立即零输出后的反向延迟，全部通过|
|浏览器实际脚本|Node执行网页中的脚本并模拟DOM/网络；未启用抑制、保持/松开、转向、空格、失焦、网络失败、未完成启用请求取消，全部通过|

主机C测试使用本机MSVC编译器，测试的是与F407链接的同一份`common`源码；浏览器测试不是实机Wi-Fi测试。

## ESP32目标构建状态

**已通过真实ESP32-S3目标编译与链接。** 使用Arduino CLI 1.5.1、Arduino-ESP32 2.0.17、官方Xtensa ESP32-S3 GCC 8.4.0（esp-2021r2-patch5）。官方核心ZIP已按包索引SHA-256校验，构建依赖放在隔离目录中，未改动系统开发环境或原硬件资料。

设置：ESP32S3 Dev Module、16MB Flash、OPI PSRAM、USB CDC关闭、QIO80MHz菜单、默认4MB分区。默认分区不使用全部16MB空间，不影响本测试运行。

- 编译程序存储占用718801字节，默认应用分区上限1310720字节。
- 静态全局变量44856字节；编译器报告剩余动态内存282824字节，不等于运行时峰值内存实测。
- 应用BIN文件719168字节；合并镜像已通过esptool 4.5.1生成，烧录地址0x0。
- 本次未出现编译警告/错误。旧版GCC位于中文路径时曾报CreateProcess错误，将工具链与构建目录放到ASCII路径后完成编译，见复现说明。

编译通过不证明Wi-Fi、UART与电机实物已联调。S3的3.x核心版本、原生USB摄像头采集没有在本次验证。

## 复现命令

```powershell
python firmware/remote_control/tools/prepare.py
python firmware/remote_control/tools/build_stm32.py --keil "E:/Keil_v5"
python firmware/remote_control/tools/test_core.py
node firmware/remote_control/tests/test_web.mjs
```

非Windows主机安装GCC后可尝试`python tools/test_core.py --cc gcc`，Windows主机测试默认需要MSVC；这些测试工具不是运行车端固件的必需条件。

本地HAL源压缩包SHA-256：`269c736163a8add0a9e244b20e8c15a92d7a373371f4ca3aa5324a1bc3b0998c`。

## 必须在实物完成的项目

1. 电源极性、电压、USB重复供电和三板共地检查；实物驱动板版本核对。
2. 架空逐轮确认通道、前进极性和PWM频率，之后才低速落地。
3. 按键松开、网页关闭、Wi-Fi断开、S3重启、UART断开与PE0停车输入的实际效果。
4. 启动/转向负载电流、驱动温升、供电压降、重启和车辆惯性。
5. 编码器闭环、摄像头、传感器、上装和喷涂不属于本次测试范围。

当前没有实测速度、行驶精度、RF距离或停车距离数据。
