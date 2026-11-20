# VL6180X 初始化序列来源

`tof.c` 的寄存器初始化值及测量顺序参考本地供应商提供的 `Adafruit_VL6180X.zip` 中 `Adafruit_VL6180X.cpp`。底层 I²C 与有超时状态机为本工程重新编写。

保留随附源文件的说明：

```text
@file Adafruit_VL6180X.cpp
@mainpage Adafruit VL6180X ToF sensor driver

@section intro_sec Introduction
This is the documentation for Adafruit's VL6180X driver for the
Arduino platform. It is designed specifically to work with the
Adafruit VL6180X breakout: http://www.adafruit.com/products/3316

These sensors use I2C to communicate, 2 pins (SCL+SDA) are required
to interface with the breakout.

Adafruit invests time and resources providing this open source code,
please support Adafruit and open-source hardware by purchasing
products from Adafruit!

@section author Author
Written by ladyada for Adafruit Industries.

@section license License
BSD license, all text here must be included in any redistribution.
```
