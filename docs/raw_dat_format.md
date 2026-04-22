# VaporView Unified Raw DAT Format

本文档说明 `session_*/raw/*.dat` 的统一原始数据记录格式。该格式只保存设备返回的原始帧或原始响应字节，不保存解析后的字段；解析摘要仍在 `sensors/devices.csv` 中。

## 文件位置

每个记录会话会创建 `raw/` 目录，并写入以下文件：

| 文件 | source_id | payload 内容 |
| --- | ---: | --- |
| `raw/epsilon.dat` | 1 | 已校验通过的完整 EPSILON FDILink 帧 |
| `raw/ptb.dat` | 2 | PTB210 有效压力响应原始行字节，包含行结束符 |
| `raw/hmp.dat` | 3 | HMP3 完整 Modbus 数据响应帧 |
| `raw/lidar.dat` | 4 | 已识别协议且校验通过的完整测距帧 |
| `raw/tcp_wave.dat` | 5 | TCP 原始信号 payload 和二次谐波 payload |

新记录会话只写这些统一 raw DAT 文件，不再额外生成旧版 `sensors/epsilon_raw.dat` 或 `waveform/*.dat`。数据查看器仍保留对旧会话 `waveform/*.dat` 的读取兼容。

## 字节序

除 `magic` 和 payload 外，所有多字节整数字段均为 little-endian。payload 保持设备或 TCP 流的原始字节顺序。

## 文件头

每个 `.dat` 文件以固定文件头开头：

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | char[8] | magic | 固定为 `VVRAWDAT` |
| 8 | uint32 | version | 当前为 `1` |
| 12 | uint32 | header_size | 当前文件头大小，当前为 `20` |
| 16 | uint16 | source_id | 数据源编号 |
| 18 | uint16 | reserved | 保留，当前为 `0` |

## 记录头

文件头后是连续记录，每条记录由记录头和 payload 组成：

| 偏移 | 类型 | 字段 | 说明 |
| ---: | --- | --- | --- |
| 0 | uint32 | marker | 固定为 `0x44525756` |
| 4 | uint32 | header_size | 当前记录头大小，当前为 `36` |
| 8 | uint64 | host_timestamp_us | 主机 UTC 时间戳，单位微秒 |
| 16 | uint32 | payload_size | payload 字节数 |
| 20 | uint16 | source_id | 数据源编号，和文件头一致 |
| 22 | uint16 | record_type | 源内记录类型 |
| 24 | uint32 | flags | 附加标志 |
| 28 | uint64 | sequence | 当前文件内从 0 开始递增的记录序号 |

## record_type 与 flags

- EPSILON：`record_type` 为 FDILink packet id，例如 `0x40 / 0x41 / 0x42 / 0x50`；`flags` 低 8 位保存 FDILink serial number。
- PTB：`record_type = 1`，`flags = 0`。
- HMP：`record_type = 0x03`，对应 Modbus function code。
- LIDAR：`record_type` 使用程序内 `LidarProtocol` 枚举值：`1=TF03`，`2=TFA1500DistanceFrame`，`3=TFA1500LowFrequencyFrame`，`4=TFA1500HighFrequency`，`5=ObservedAaB7Frame`。
- TCP 波形：`record_type = 1`，`flags` bit0 表示 payload 内含两个 length-prefixed 子 payload。

## TCP 波形 payload

`raw/tcp_wave.dat` 的每条 payload 由下面几段顺序拼接：

| 偏移 | 类型 | 字段 |
| ---: | --- | --- |
| 0 | uint32 | 原始信号 payload 字节数 |
| 4 | uint32 | 二次谐波 payload 字节数 |
| 8 | byte[] | 原始信号 payload |
| 8 + raw_size | byte[] | 二次谐波 payload |

这两个子 payload 是 TCP 帧长度头之后的原始负载，不包含 4 字节长度头；浮点字节序保持接收时的原始编码。

## 与 CSV 对齐

CSV 中的 `record_timestamp_us` 和 raw DAT 记录头中的 `host_timestamp_us` 使用同一主机 UTC 微秒时间基准，可用于离线对齐。CSV 保留解析后的摘要字段，raw DAT 保留原始帧字节。
