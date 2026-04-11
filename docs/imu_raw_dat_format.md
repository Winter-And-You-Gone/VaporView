# IMU 原始数据记录格式说明

本文档说明 `VaporView` 当前写入 `session_*/sensors/imu_raw.dat` 的二进制格式，并给出与 HiPNUC `HI91` 官方协议对应的字段说明。

文档目标：
- 明确 `imu_raw.dat` 的文件头、记录头、时间戳和 payload 语义
- 说明当前记录侧的职责边界：完整保存原始 IMU 包，不在记录阶段生成 KF-GINS 输入
- 方便后处理模块离线解析 `imu_raw.dat`，自行导出 `dtheta/dvel`

## 1. 范围与约定

当前项目按 `HI91` 作为 IMU 默认输出格式记录原始数据。

记录侧只负责：
- 保存主机接收时间
- 保存帧类型标记
- 保存模块原始二进制包

记录侧不负责：
- 重采样
- 生成 KF-GINS 文本 IMU 文件
- 计算 `dtheta/dvel`
- 对原始包做不可逆裁剪

## 2. 参考资料

本说明的协议细节主要参考以下官方文档：

- [H14-1.7.0.pdf](C:/WorkSpace/NAV/H14-1.7.0.pdf)
  关键章节：
  - `3.5.5 SAVECONFIG`
  - `3.5.6 SERIALCONFIG`
  - `3.5.8.10 设置数据帧输出类型及频率`
  - `4.6 数据帧格式`
  - `4.7 出厂默认输出`
  - `4.8.10 浮点型IMU数据帧（HI91）`
  - `4.10 数据帧结构示例（以HI91为例）`
- [HI14_DataSheet_1.7_CN.pdf](C:/WorkSpace/NAV/HI14_DataSheet_1.7_CN.pdf)

根据官方文档，和当前记录链路直接相关的结论有：
- 模块二进制默认输出为 `HI91`
- `HI91` 可通过 `LOG HI91 ONTIME <period>` 配置输出周期
- 常用输出频率支持 `1/2/5/10/20/50/100/200/250/500/1000 Hz`
- 高频输出时默认 `115200` 波特率可能不足，较高频率通常应配合 `921600`
- `HI91` 数据包内包含 `system_time` 字段，单位为 `ms`
  - GPS 时间未同步成功时，为模块本地开机累加时间
  - GPS 时间同步成功时，为 UTC 时间

## 3. 官方 HiPNUC 串口二进制帧

官方文档定义的串口二进制帧格式如下：

- 帧头：`0x5A 0xA5`
- 数据域长度：`uint16 little-endian`
- CRC16：`uint16 little-endian`
- 数据域：一个或多个子数据包

在当前 `VaporView` 解析链路中，写入 `imu_raw.dat` 的 payload 是**一整帧原始 HiPNUC 二进制包**，也就是：

- 帧头 `0x5A 0xA5`
- 长度
- CRC
- 数据域

不是仅保存 `HI91` 的 76 字节数据域，也不是保存解码后的浮点结果。

## 4. HI91 官方数据域摘要

根据 [H14-1.7.0.pdf](C:/WorkSpace/NAV/H14-1.7.0.pdf) 中 `4.8.10 浮点型IMU数据帧（HI91）`：

- `HI91` 数据域长度为 `76` 字节
- `tag = 0x91`
- 主要字段包括：
  - `main_status`：状态字
  - `temperature`：模块温度，单位 `°C`
  - `air_pressure`：气压，单位 `Pa`
  - `system_time`：时间戳，单位 `ms`
  - `acc_b[3]`：三轴加速度，单位 `G`
  - `gyr_b[3]`：三轴角速度，单位 `deg/s`
  - `mag_b[3]`：三轴磁场，单位 `uT`
  - `roll/pitch/yaw`：姿态角，单位 `deg`
  - `quat[4]`：四元数，顺序 `WXYZ`

注意：
- 官方 `HI91` 中加速度单位是 `G`
- 官方 `HI91` 中角速度单位是 `deg/s`
- 后处理模块若要导出给 KF-GINS，需要自行做单位转换和时间积分

## 5. VaporView 的 `imu_raw.dat` 文件格式

### 5.1 文件头

每个 `imu_raw.dat` 文件开头先写一个固定文件头：

```c
struct ImuRawFileHeader
{
    char    magic[8];     // "VVIMURAW"
    uint32  version;      // 当前为 1
    uint32  header_size;  // 当前结构体大小，16
};
```

当前实现位于 [MainWindow.cpp](C:/WorkSpace/NAV/VaporView/src/MainWindow.cpp)。

字段说明：
- `magic`：固定为 ASCII `"VVIMURAW"`，用于快速识别文件类型
- `version`：格式版本号，当前为 `1`
- `header_size`：文件头自身大小，便于后续格式扩展

### 5.2 记录头

文件头后面跟随若干条原始 IMU 记录。每条记录先写固定大小的记录头，再写原始包字节：

```c
struct ImuRawRecordHeader
{
    uint32 marker;            // 固定 0x524D5549
    uint32 payload_size;      // 原始包长度（字节）
    uint64 host_timestamp_us; // 主机接收时间，Unix epoch 微秒
    uint8  frame_tag;         // 当前帧类型，如 0x91
    uint8  reserved[3];       // 保留
};
```

字段说明：
- `marker`
  - 当前固定值为 `0x524D5549`
  - 这是记录起始标记，用于离线扫描和容错恢复
- `payload_size`
  - 紧随其后的原始包长度，单位字节
- `host_timestamp_us`
  - 由主机在收到完整 IMU 帧后立刻记录
  - 来源是 `system_clock` 的 Unix epoch 微秒值
  - 这是主机时间，不是模块内部 `system_time`
- `frame_tag`
  - 记录该包的帧类型
  - 当前常见值是 `0x91`
- `reserved[3]`
  - 保留位，当前写 `0`

### 5.3 payload

每个记录头之后，紧跟 `payload_size` 字节原始 IMU 包。

当前实现直接写入解析器里的 `raw.buf`，长度为：

```text
raw.len + 6
```

其中 `6` 字节对应官方协议中的：
- `0x5A 0xA5`
- 长度字段
- CRC 字段

因此，`payload` 是**完整官方串口包**，后处理模块可以直接重新跑校验、重新解码，而不依赖采集时的中间显示结果。

## 6. 当前记录格式对后处理的意义

按当前设计，`imu_raw.dat` 已经保存了后处理需要的关键原始信息：

- 主机接收时间 `host_timestamp_us`
- 帧类型 `frame_tag`
- 完整原始串口包

这意味着后处理模块可以：
- 重新校验 CRC
- 重新解码 `HI91`
- 取出官方定义的 `system_time`
- 自行做时间对齐
- 自行将 `acc/gyr` 转换为 KF-GINS 需要的 `dtheta/dvel`

## 7. 与 KF-GINS 的职责边界

当前项目约定如下：

- `VaporView`
  - 负责完整记录 `HI91` 原始包
  - 不负责生成 KF-GINS 输入文件
- `KF` 模块
  - 负责读取 `imu_raw.dat`
  - 负责解析 `HI91`
  - 负责将 `G / deg/s` 转为目标单位
  - 负责根据时间差生成 `dtheta/dvel`
  - 负责导出 KF-GINS 所需文本格式

也就是说，记录侧的正确性标准是：

- 不丢包
- 不降频
- 不篡改原始 payload
- 能让后处理模块完整复原官方 `HI91` 帧

而不是在采集时直接生成最终组合导航输入。

## 8. 当前实现中的时间语义

后处理时请区分两类时间：

### 8.1 `host_timestamp_us`

- 来自主机系统时钟
- 记录的是“主机收到完整 IMU 帧”的时间
- 适合做跨设备对齐、日志关联、接收延迟分析

### 8.2 `HI91.system_time`

- 来自 IMU 包内部
- 单位 `ms`
- 官方说明：
  - GPS 时间未同步成功时，为模块本地运行时间
  - GPS 时间同步成功时，为 UTC 时间

若后处理模块需要更稳的高频时间轴，应优先结合 `HI91.system_time` 使用，并将 `host_timestamp_us` 作为辅助参考。

## 9. 解析建议

后处理模块建议按以下步骤读取 `imu_raw.dat`：

1. 读取并校验 `ImuRawFileHeader`
2. 循环读取 `ImuRawRecordHeader`
3. 按 `payload_size` 读取整包原始数据
4. 校验 `marker`、`payload_size` 与实际读到的包长
5. 对 payload 重新执行 HiPNUC 协议解析
6. 仅处理 `frame_tag == 0x91` 的记录
7. 从 `HI91` 取出 `system_time`、`acc_b`、`gyr_b`
8. 自行生成后续算法所需格式

## 10. 当前结论

在不考虑 `HI92` 的前提下，当前 `imu_raw.dat` 设计已经满足“完整保留 H91 原始数据”的目标。

它适合作为后处理模块的原始输入，但不应被视为已经完成了 KF-GINS 输入准备。  
KF-GINS 所需的 `GNSS seconds of week + dtheta + dvel` 仍应由后处理模块自行生成。
