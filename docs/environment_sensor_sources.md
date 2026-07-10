# 环境监测设备来源

设备配置页现在支持两组可切换来源：

- 气压：`PTB210` 或 `BMP390`
- 温湿度：`HMP3` 或 `SHT45`

PTB210/HMP3 继续使用原有串口协议。BMP390/SHT45 是 I2C 器件，需要通过 Arduino、ESP32、Pico 或 Raspberry Pi 等主机读取后，再通过串口把示例文本发给 VaporView。

## BMP390

- 推荐串口：`115200 8N1`
- 兼容微雪 Arduino/ESP32 示例输出：

```text
T:25.32 deg C,P:100653.25 Pa
```

软件读取 `P:`/`Pressure:` 字段，并把 Pa 转换为 hPa。模块默认 I2C 地址为 `0x77`，焊接 ADDR 电阻后为 `0x76`。

## SHT45

- 推荐串口：`115200 8N1`
- 兼容 Adafruit SHT4x 示例的分行输出：

```text
Temperature: 23.75 degrees C
Humidity: 48.20 % rH
```

软件会配对最近一组温度和湿度行。SHT45 固定 I2C 地址为 `0x44`。

选择 BMP390 或 SHT45 时，设备配置页会自动把对应波特率切换为 115200；串口号仍需选择 MCU/单板机实际枚举出的端口。
