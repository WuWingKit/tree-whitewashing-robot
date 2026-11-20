# USB 实测诊断 · 2026-11-10

用户确认将Hub和一台相机接回ESP32原生USB口，COM口继续接电脑后，已通过OTA安装 `0.3.2-usbdiag` 并读取真实控制器状态。

```json
{
  "host_query": "ESP_OK",
  "enumerated_devices": 0,
  "host_clients": 2,
  "root_connected": false,
  "root_enabled": false,
  "controller_power_flag": true,
  "vbus_voltage": null,
  "devices": []
}
```

确定的结果：USB Host服务和UVC/诊断客户端已安装，但根端口没有报告设备连接，没有设备完成枚举。故障发生在USB连接/枚举阶段，尚未进入相机UVC格式匹配和MJPEG采集阶段。

不能由这些数据推出“USB端口一定没有5V”。`controller_power_flag`只是控制器端口控制位，不是VBUS电压；此板未配置USB VBUS测量ADC，用户目前也没有测量工具。

参考原理图显示：USB2的VBUS经过D2向板上5V供电，USB-OTG 0Ω电阻作为并联旁路；是否可反向输出电源取决于实物装配。USB2的CC1/CC2经5.1kΩ下拉，按该参考图不是完整的Type-C Host源端配置。实物是否与参考图一致尚未通过板面照片确认。

因此剩余待确认项为：实际VBUS输出、Type-C角色/转接、数据线路或Hub与根端口兼容性。不能凭软件状态进一步把它们归为某一个具体原因。

诊断接口为 `GET http://192.168.4.1/api/usb/status`；它会显示当前根端口状态和枚举设备信息，设备成功枚举后可读取地址、VID/PID及是否含Video/Hub接口。观察客户端只读取缓存描述符，不认领视频接口、不替代UVC驱动。

本次诊断版OTA上传、重启、启动确认通过，两板UART在线，保持未启用和零输出；电脑原Wi-Fi已恢复。原始日志位于本地 `.hardware_review/integrated_flash/usbdiag_result.json`。
