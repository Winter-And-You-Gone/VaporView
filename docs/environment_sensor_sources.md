# 环境与内部环境设备的独立串口连接

环境气压固定使用 PTB210，环境温湿度固定使用 HMP3，session 的对应字段保持这两个设备的语义。BMP390 与 SHT45 是独立内部环境设备，两者均在直连与 FPGA 的设备配置中占独立行：BMP390 提供内部气压，SHT45 提供内部温度、内部湿度，显示在首页设备温控概览标题栏。

## USB 串口桥方案

当前直连路径采用 Arduino/ESP32 等开发板读取 I²C，再以 USB CDC 或开发板的 USB-UART 输出文本。每个传感器使用一个独立串口端点；不要让两行同时打开同一个 COM 口。普通 CH341、CP2112 等原始 USB-I²C 控制器的专用命令/HID 协议不在此文本接口范围内，购买时需确认输出方式，或者使用下面的桥程序。

- 默认串口：115200 baud、8 数据位、无校验、1 停止位（8N1）；这属于桥的串口设置，I²C 传感器本身没有 UART 波特率。原生 USB CDC 桥也保留同一串口配置入口。
- 默认桥输出：1 Hz，SHT45 使用高精度、关闭加热器；BMP390 由桥端做标定补偿并上传 Pa。
- 上位机“接收”项默认 1 Hz，可设整数 1..20 Hz，仅限制数据显示/回调更新节奏。桥决定物理采样率，上位机不发送未定义的轮询或改频命令；桥只输出 1 Hz 时，上位机设为 20 Hz 也不会产生额外样本。需要更快输出时先修改桥程序的 `intervalMs`，并核对传感器测量时间与功耗。
- 两个设备默认不勾选启用。选择实际串口并启用后可逐设备连接；连接期间串口、波特率和接收频率锁定，断开后可编辑，再连接使用新值。连接需实际收到对应文本格式；断开、无效或超过 4 秒未更新时显示“—”。

桥端示例：[internal_environment_bridge.ino](../examples/internal_environment_bridge/internal_environment_bridge.ino)。`BRIDGE_BMP390=1` 用于 BMP390，改成 `0` 用于 SHT45，各自上传到一个开发板。按开发板说明连接 SDA/SCL、共地及传感器允许的供电电压；安装对应 Adafruit BMP3XX 或 SHT4x 库及其依赖。BMP390 示例探测 `0x77` / `0x76`；SHT45 使用库支持的 `0x44`。

接口依据：[Adafruit SHT4x 示例](https://github.com/adafruit/Adafruit_SHT4X/blob/master/examples/SHT4test/SHT4test.ino)、[Adafruit BMP3XX API](https://adafruit.github.io/Adafruit_BMP3XX/html/class_adafruit___b_m_p3_x_x.html)。桥程序尚未在实体开发板编译/烧录或连传感器验证；软件测试不代替该项验收。

## 文本格式与数据归属

BMP390（Pa，换算为 hPa 显示）：

```text
T:25.32 deg C,P:100653.25 Pa
```

BMP 温度是气压补偿数据，不作为内部环境温度。SHT45 接收一组温度和湿度行：

```text
Temperature: 23.75 degrees C
Humidity: 48.20 % rH
```

本地与 SkyCore 复用相同的独立串口控制器和文本解析，不复用 PTB/HMP 采集槽位或 raw/session 回调。SkyConfig 增加可选 `bmp390` / `sht45` 节，分别保存 enabled、port、baud、frequency_hz；旧配置缺少这两节时保持禁用。远程独立设备 ID 为 9 / 10，内部数据使用 `InternalEnvironment`（消息类型 `0x0F`，沿用已有类型化传感器 JSON 载荷），经同一数传 TCP 链路传到地面；需要天空端运行同步更新的 SkyCore。它不代表 VLP 固件新增了 source。

FPGA 模式继续读取 source `0x43` / `0x44` 和各自 CONTROL、I²C 轮询寄存器，直连串口预设独立保存。当前自动识别串口仍以原有设备探测为主，这两个独立桥端口使用各自的串口下拉框选择。

实机验收：分别确认桥程序识别传感器、115200 输出格式和单位；验证同时连接 PTB/HMP/BMP/SHT 时互不覆盖；逐一拔掉桥并等待过期；核对本地与 SkyCore 远程显示一致、切换路径保留各自预设、session 环境字段仍来自 PTB/HMP。
