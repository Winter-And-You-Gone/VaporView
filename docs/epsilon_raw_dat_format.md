# EPSILON 原始数据记录格式说明

本文档说明 `VaporView` 当前写入 `session_*/sensors/epsilon_raw.dat` 的二进制格式，以及它与 EPSILON `FDILink` 串口协议之间的对应关系。

本文档目标：
- 明确 `epsilon_raw.dat` 的文件头、记录头和 payload 语义
- 说明记录链路保存的是“已校验通过的完整 FDILink 原始帧”
- 方便后处理模块离线解析 `epsilon_raw.dat`，用于重建三维轨迹、姿态和地图输入

## 1. 范围与约定

当前项目使用 EPSILON 组合导航一体机作为单一导航设备，主链路走 `FDILink` 串口协议。

记录侧只负责：
- 保存主机接收时间
- 保存报文 `packet id`
- 保存报文流水号 `serial number`
- 保存完整 FDILink 原始帧

记录侧不负责：
- 重采样
- 再次打包为其他私有格式
- 在记录阶段生成 KF-GINS 最终输入文本
- 对原始帧做不可逆裁剪

## 2. 参考资料

本说明的协议细节以本地 EPSILON 手册和厂商参考代码为准：

- [组合导航EPSILON使用手册V1.2_20250424.pdf](D:/Project/GPS/说明书_文档/组合导航EPSILON使用手册V1.2_20250424.pdf)
- `说明书_文档` 下随手册附带的 FDILink / EPSILON 厂商参考实现

当前与记录格式直接相关的关键结论有：
- 主串口默认参数为 `921600 N81`
- 协议为 `FDILink`
- 当前运行时会记录这些已校验报文：
  - `0x40` IMU
  - `0x41` AHRS
  - `0x42` INS/GPS
  - `0x50` SYS_STATE
  - `0x59` RAW_GNSS
  - `0x5A` SATELLITE
  - `0x5C` GEODETIC_POS
  - `0x5D` ECEF_POS

## 3. FDILink 串口帧

当前实现按完整 FDILink 帧落盘。一个标准 FDILink 帧由以下部分组成：

- 帧头：`0xFC`
- `packet id`
- payload 长度
- `serial number`
- `CRC8`
- `CRC16`
- payload
- 帧尾：`0xFD`

`VaporView` 仅在以下条件全部满足时才写入 `epsilon_raw.dat`：
- 帧头和帧尾正确
- `CRC8` 校验通过
- `CRC16` 校验通过
- 帧长完整

因此，`epsilon_raw.dat` 里的 payload 不是解码后的字段，而是完整、可复验的官方原始帧。

## 4. `epsilon_raw.dat` 文件格式

### 4.1 文件头

每个 `epsilon_raw.dat` 文件开头先写一个固定文件头：

```c
struct ImuRawFileHeader
{
    char    magic[8];     // "VVEPSRAW"
    uint32  version;      // 当前为 1
    uint32  header_size;  // 当前结构体大小，16
};
```

字段说明：
- `magic`：固定 ASCII `"VVEPSRAW"`，用于快速识别 EPSILON 原始帧文件
- `version`：格式版本号，当前为 `1`
- `header_size`：文件头自身大小，便于后续格式扩展

### 4.2 记录头

文件头后面跟随若干条原始帧记录。每条记录先写固定大小的记录头，再写完整 FDILink 帧字节：

```c
struct ImuRawRecordHeader
{
    uint32 marker;            // 固定 0x524D5549
    uint32 payload_size;      // 完整 FDILink 帧长度（字节）
    uint64 host_timestamp_us; // 主机接收时间，Unix epoch 微秒
    uint8  frame_tag;         // packet id
    uint8  reserved[3];       // reserved[0] 当前写 serial number
};
```

字段说明：
- `marker`
  - 当前固定值为 `0x524D5549`
  - 用于离线扫描和容错恢复
- `payload_size`
  - 紧随其后的完整 FDILink 帧长度，单位字节
- `host_timestamp_us`
  - 主机在收到并校验完整 FDILink 帧后记录的 Unix epoch 微秒时间
- `frame_tag`
  - 当前帧的 `packet id`
- `reserved[0]`
  - 当前写入 FDILink 的 `serial number`
- `reserved[1..2]`
  - 预留，当前写 `0`

### 4.3 payload

每个记录头之后，紧跟 `payload_size` 字节原始 FDILink 帧。

这里保存的是完整串口帧，而不是单独的 payload 字段，因此离线模块可以：
- 重新跑 CRC 校验
- 重新做丢包统计
- 重新解码任意已支持的 `packet id`
- 在以后扩展解析规则时复用同一份原始数据

## 5. 当前记录格式对后处理的意义

按当前设计，`epsilon_raw.dat` 已保存后处理需要的核心原始信息：

- 主机接收时间 `host_timestamp_us`
- 报文类型 `frame_tag`
- 报文流水号 `serial number`
- 完整原始 FDILink 帧

这意味着后处理模块可以离线完成：
- 三维坐标重建
- 姿态解算结果复核
- 地图轨迹重放
- 新字段二次解析
- 协议升级后的兼容导出

## 6. 与 `devices.csv` 的职责边界

当前项目约定如下：

- `devices.csv`
  - 保存实时解析后的 EPSILON 快照字段
  - 用于界面展示、轨迹查看和 KF-GINS 导出
- `epsilon_raw.dat`
  - 保存完整、可复验的 EPSILON 原始 FDILink 帧
  - 用于离线重解析、调试和更高保真后处理

也就是说：
- `devices.csv` 负责“方便使用”
- `epsilon_raw.dat` 负责“完整保真”

## 7. 解析建议

离线模块建议按以下步骤读取 `epsilon_raw.dat`：

1. 读取并校验文件头
2. 循环读取记录头
3. 按 `payload_size` 读取完整 FDILink 帧
4. 使用 `frame_tag` 快速分流报文类型
5. 使用 `reserved[0]` 恢复流水号并统计丢包
6. 重新执行 CRC8 / CRC16 校验
7. 再按对应 `packet id` 解码 payload

## 8. 当前结论

当前 `epsilon_raw.dat` 的设计目标是：完整保留已校验通过的 EPSILON `FDILink` 原始帧，并保留足够的时间与流水号信息，支持后续三维重建、地图展示和离线二次解析。它不是最终算法输入的替代品，而是算法和工具链可长期复用的高保真原始档案。
