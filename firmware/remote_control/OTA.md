# ESP32网页OTA：使用与验证

版本`0.2.0-ota`，2026-11-09。已在本项目ESP32-S3 N16R8与STM32F407实物上完成USB初始化、两板UART及一次真实Wi-Fi OTA。STM32的应用与启动地址不变，F407自定义IAP尚未实现。

## 使用步骤

1. 保持稳定供电，车辆停车。连接热点`TreeRobotTest`，Wi-Fi密码`TreeTest2026`。
2. 打开`http://192.168.4.1/`，点击“固件升级（OTA）”，或直接访问`http://192.168.4.1/update`。
3. 登录账号`admin`；**OTA密码与Wi-Fi密码不同**，在本地`esp32/ota_access.json`中。它由构建脚本生成，不进入Git或交付ZIP；后续构建保留同一文件才会保留密码。
4. 分别选择升级包中的`RemoteControl.ino.bin`及配套`RemoteControl.ota.json`，点击“停车并升级”。
5. 系统锁定遥控，确认STM32取消启用且左右输出为0，再上传、校验、重启。重新连接热点，回控制页查看版本；必须重新启用遥控，不会续接旧运动指令。

仅对ESP32独立台架升级且STM32未连接时，才勾选独立台架模式；它不会绕过一个在线但未停车的STM32。上传失败后保持维护锁定，可点“取消维护锁定”后重试。刷新页面丢失会话时，可重新选择文件准备一次，再取消。

**此处只能上传应用BIN，不能上传USB合并BIN、STM32 BIN或HEX。** 目前沿用两个各`0x140000`字节的OTA槽，地址分别为`0x10000`、`0x150000`，未重新分配16MB Flash其余空间。

## 实现与边界

- 复用Arduino-ESP32 2.0.17自带`WebServer`和`Update`。
- 独立密码与随机会话令牌；维护期间ARM/DRIVE返回HTTP 423，继续发送STOP。
- 校验目标、声明长度、分区容量、S3应用镜像头、应用描述头、项目兼容标记和SHA256；全部通过才激活备用槽。
- 中断/长度错误/校验失败会中止更新，保留当前运行槽并保持停车锁定。SHA256是完整性校验、项目标记是误刷检查，不等同发布者数字签名。
- 新应用初始化热点/服务并运行主循环后延迟确认有效状态；已验证正常切槽与启动确认，未覆盖全部断电、卡死和硬件故障场景，不能声称所有情况下都会自动回滚。
- 仅用于本地模型调试，没有公网访问、HTTPS或发布者数字签名。
- USB日志每约2秒报告版本、STM32在线/停车状态和启动槽。COM10是本机本次端口号，不是固定要求。

## 构建与命令行升级

```powershell
python firmware/remote_control/tools/build_esp32.py --work-dir "C:/espwork/tree_robot_rc"
python firmware/remote_control/tools/make_ota_manifest.py
python firmware/remote_control/tools/ota_upload.py --image firmware/remote_control/esp32/build/RemoteControl.ino.bin --manifest firmware/remote_control/esp32/build/RemoteControl.ota.json
```

预装Arduino CLI与ESP32核心2.0.17。构建会生成网页头文件、公共代码和本地密码配置。命令行工具从JSON读账号，不将密码放到进程参数；不指定`--image`只查询状态，不负责切换Wi-Fi。

详见[实机测试记录](HARDWARE_TEST_20261109.md)。当前速度范围仍为10%～30%、默认20%，两端硬上限35%；本次没有提高速度限制，也没有加入编码器闭环、自动行驶或STM32 OTA。
