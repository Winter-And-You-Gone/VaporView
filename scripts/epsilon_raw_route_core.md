# EPSILON 原始轨迹解析——核心逻辑

本文档描述 `scripts/epsilon_raw_route.py` 中“把一段字节流变成一条带距离的轨迹”的核心解析链路。仅说明解析逻辑，不含 CLI 用法与地图渲染细节（用法见 `epsilon_raw_route_readme.md`）。

解析链路一览：

```text
文件 / 字节流
  └─ 识别魔数 → 选择记录迭代器
       └─ 统一 raw DAT (VVRAWDAT)        → iter_unified_raw_dat
       └─ 旧版 EPSILON raw (VVEPSRAW)    → iter_legacy_epsilon_raw_dat
       └─ 裸 FDILink 字节流              → iter_raw_fdilink_stream
                 ↓ 逐条产出 RawRecord（payload = 一帧 FDILink 字节）
       FDILink 帧校验 (parse_fdilink_frame / pf)
                 ↓ 校验 head / size / tail / crc8 / crc16
       按 packet_id 更新跨帧状态机 DecodeState
                 ↓ 仅 0x50 / 0x5C 产出坐标点
       来源选择 (select_position_source)
                 ↓ auto / geodetic / system / both
       过滤 (apply_filters)
                 ↓ 索引 / fix / hacc / vacc / 跳点
       距离累加 (assign_route_distances)
                 ↓ haversine
       一条轨迹
```

## 1. 数据结构与常量

### 1.1 文件与记录格式

| 常量 | 值 | 含义 |
| --- | --- | --- |
| `FRAME_HEAD` | `0xFC` | FDILink 帧头 |
| `FRAME_TAIL` | `0xFD` | FDILink 帧尾 |
| `UNIFIED_RAW_MAGIC` | `b"VVRAWDAT"` | 统一 raw DAT 文件头魔数 |
| `UNIFIED_RAW_RECORD_MARKER` | `0x44525756` | 统一记录头标记（`"VVWD"`） |
| `UNIFIED_RAW_HEADER_SIZE` | `20` | 文件头长度 |
| `UNIFIED_RAW_RECORD_HEADER_SIZE` | `36` | 记录头长度 |
| `RAW_SOURCE_EPSILON` | `1` | 统一格式中的 EPSILON source id |
| `LEGACY_EPSILON_MAGIC` | `b"VVEPSRAW"` | 旧版 EPSILON raw 文件头魔数 |
| `LEGACY_EPSILON_RECORD_MARKER` | `0x524D5549` | 旧版记录头标记（`"IUMR"`） |
| `LEGACY_EPSILON_RECORD_HEADER_SIZE` | `20` | 旧版记录头长度 |

### 1.2 packet id

```python
PACKET_NAMES = {
    0x40: "IMU",
    0x41: "AHRS",
    0x42: "INS_GPS",
    0x50: "SYS_STATE",
    0x51: "UNIX_TIME",
    0x52: "FORMATTED_TIME",
    0x59: "RAW_GNSS",
    0x5A: "SATELLITES",
    0x5C: "GEODETIC_POS",
    0x5D: "ECEF_POS",
    0xF0: "MAVLINK_TUNNEL",
}
```

### 1.3 GNSS fix

```python
FIX_NAMES = {
    0: "NO_GPS", 1: "NO_FIX", 2: "2D", 3: "3D",
    4: "DGPS", 5: "RTK_FLOAT", 6: "RTK_FIXED",
    7: "STATIC", 8: "PPP", 9: "RTK_DUAL",
}
```

### 1.4 核心数据类

```python
@dataclass
class RawRecord:
    source_file: Path
    raw_format: str          # "unified_v2" / "legacy_epsilon_v1" / "fdilink_stream"
    sequence: int
    host_timestamp_us: int   # 裸流为 0
    record_type: int         # packet id
    flags: int
    payload: bytes           # 对统一/旧版是整帧字节；对裸流也是整帧字节
    record_offset: int = -1
    payload_offset: int = -1

@dataclass
class FrameInfo:
    ok: bool
    packet_id: int
    serial_number: int
    payload_size: int
    payload: bytes            # 帧内 payload（不含帧头尾）
    crc8_ok: bool
    crc16_ok: bool
    tail_ok: bool
    size_ok: bool
    error: str = ""

@dataclass
class DecodeState:
    # 跨帧缓存最近一次各字段的值，供后续坐标点读取
    utc_unix_s / utc_microseconds
    gnss_fix_code / gnss_satellites
    hdop / vdop / hacc_m / vacc_m
    lat_std_m / lon_std_m / height_std_m / diff_age_s
    vel_n_mps / vel_e_mps / vel_d_mps
    ned_n_m / ned_e_m / ned_d_m
    roll_deg / pitch_deg / yaw_deg
    system_status_bits / filter_status_bits / update_status_bits
```

## 2. 格式识别与分发

`iter_records` 按文件前 8 字节魔数选择迭代器：

```python
def iter_records(path: Path) -> Iterator[RawRecord]:
    magic = path.open("rb").read(8)
    if magic == UNIFIED_RAW_MAGIC:        # VVRAWDAT
        yield from iter_unified_raw_dat(path)
    elif magic == LEGACY_EPSILON_MAGIC:   # VVEPSRAW
        yield from iter_legacy_epsilon_raw_dat(path)
    else:                                 # 没有 magic，按裸 FDILink 流扫描
        yield from iter_raw_fdilink_stream(path)
```

`resolve_input_files` 把“会话目录”映射到具体文件，按优先级查找：

```text
raw/epsilon.dat → sensors/epsilon_raw.dat → epsilon.dat → epsilon_raw.dat
```

## 3. 三种记录迭代器

### 3.1 统一 raw DAT（VVRAWDAT）

`iter_unified_raw_dat`：

- 文件头 `<8sIIHH>`：`magic, version, header_size, source_id, reserved`
- 校验 `magic == VVRAWDAT`、`version >= 1`、`header_size >= 20`、`source_id == 1`
- 循环读取记录头 `<IIQIHHIQ>`：
  - `marker`（必须等于 `UNIFIED_RAW_RECORD_MARKER`）
  - `header_size`（必须 `>= 36`）
  - `host_timestamp_us`
  - `payload_size`
  - `record_source_id`
  - `record_type`
  - `flags`
  - `sequence`
- 只 `yield` `record_source_id == RAW_SOURCE_EPSILON` 的记录
- `payload` 为紧跟记录头之后的 `payload_size` 字节（即一帧 FDILink 帧）

### 3.2 旧版 EPSILON raw（VVEPSRAW）

`iter_legacy_epsilon_raw_dat`：

- 文件头 `<8sII>`：`magic, version, header_size`
- 循环读取记录头 `<IIQB3s>`：
  - `marker`（必须等于 `LEGACY_EPSILON_RECORD_MARKER`）
  - `payload_size`
  - `host_timestamp_us`
  - `frame_tag`（3 字节）
  - `reserved`（3 字节）
- `record_type = frame_tag[0]`、`flags = reserved[0]`
- `sequence` 由本地计数器生成（旧版格式不含序号）
- `payload` 为紧跟记录头之后的 `payload_size` 字节

### 3.3 裸 FDILink 字节流（fallback）

`iter_raw_fdilink_stream`：

- 逐字节扫描 `0xFC` 帧头
- `payload_size = data[head_offset + 2]`、`frame_size = payload_size + 8`
- `frame_size` 越界（`> 4096` 或不足 8 字节）或末字节不是 `0xFD` → 跳过继续往后找
- `host_timestamp_us = 0`（裸流没有主机时间戳）
- `record_type = frame[1]`、`flags = frame[3]`、`payload = 整帧字节`

## 4. FDILink 帧校验

所有迭代器产出的 `payload` 本质是一帧 FDILink 帧，由 `parse_fdilink_frame` 统一校验。

### 4.1 帧结构

```text
偏移   长度   字段
[0]    1      0xFC head
[1]    1      packet_id
[2]    1      payload_size
[3]    1      serial_number
[4]    1      crc8（覆盖 [0:4]）
[5]    2      crc16（big-endian，覆盖 payload）
[7]    N      payload（N = payload_size）
[7+N]  1      0xFD tail
总长 = payload_size + 8
```

### 4.2 校验项

`ok = head_ok and size_ok and tail_ok and crc8_ok and crc16_ok`

- `head_ok`：`frame[0] == 0xFC`
- `size_ok`：`len(frame) == payload_size + 8`
- `tail_ok`：`frame[-1] == 0xFD`
- `crc8_ok`：`fdilink_crc8(frame[:4]) == frame[4]`
- `crc16_ok`：`fdilink_crc16(payload) == (frame[5] << 8) | frame[6]`

### 4.3 CRC 算法

- `fdilink_crc8`：Dallas CRC-8，多项式 `0x8C`，初始值 `0`
- `fdilink_crc16`：CRC-16-CCITT，多项式 `0x1021`，初始值 `0`

`allow_bad_crc=False`（默认）时丢弃 CRC 不通过的帧；`True` 时仍解码，但计入 `bad_frames`。

## 5. 坐标点解码（核心）

`decode_coordinate_candidates` 维护一个跨帧的 `DecodeState`，按 `packet_id` 分支解码。**只有携带经纬度的两个 packet 才会产出坐标点**，其余 packet 只更新状态机。

### 5.1 各 packet 处理

| Packet | 最小 payload 长度 | 更新 DecodeState 的字段 | 是否产出坐标点 |
| --- | --- | --- | --- |
| `0x40` IMU | — | 不处理 | ❌ |
| `0x41` AHRS | — | 不处理 | ❌ |
| `0x42` INS_GPS | ≥72B | `ned_n/e/d_m`@24/28/32, `vel_n/e/d_mps`@36/40/44 | ❌ |
| `0x50` SYS_STATE | ≥14B | status bits@0/2/4, `utc_unix_s`@6, `utc_microseconds`@10, `fix_code = (filter_status>>4)&0x0F`；≥50B 更新 vel；≥78B 更新 roll/pitch/yaw；≥102B 更新 std | ✅ 当 ≥38B 时（见下） |
| `0x51` UNIX_TIME | ≥8B | `utc_unix_s`@0, `utc_microseconds`@4 | ❌ |
| `0x52` FORMATTED_TIME | ≥14B | 解析年月日时分秒+微秒 → `utc_unix_s` / `utc_microseconds` | ❌ |
| `0x59` RAW_GNSS | ≥74B | `utc_unix_s`@0, `utc_microseconds`@4, `lat/lon/height_std`@44/48/52, `diff_age_s`@64, `fix_code = raw_gnss_status & 0x0F`@72 | ❌ |
| `0x5A` SATELLITES | ≥9B | `hdop`@0, `vdop`@4, `gnss_satellites`@8 | ❌ |
| `0x5C` GEODETIC_POS | ≥32B | `hacc_m`@24, `vacc_m`@28 | ✅（见下） |
| `0x5D` ECEF_POS | ≥24B | — | ❌ 直接 continue |
| `0xF0` MAVLINK_TUNNEL | — | 不处理 | ❌ |

### 5.2 坐标点字段解码

**`0x50` SYS_STATE**（payload ≥ 38B 才产出）：

```text
latitude_deg  = rad_to_deg(read_double_le(payload, 14))
longitude_deg = rad_to_deg(read_double_le(payload, 22))
height_m      = read_double_le(payload, 30)
position_source = "system_state"
```

**`0x5C` GEODETIC_POS**（payload ≥ 32B）：

```text
latitude_deg  = rad_to_deg(read_double_le(payload, 0))
longitude_deg = rad_to_deg(read_double_le(payload, 8))
height_m      = read_double_le(payload, 16)
hacc_m        = read_float_le(payload, 24)
vacc_m        = read_float_le(payload, 28)
position_source = "geodetic_pos"
```

注意：

- 经纬度在 payload 中以**弧度的 double** 存储，需 `* 180 / π` 转度。
- `0x50` 即使更新了 status/utc/vel/attitude 字段，如果 payload 不足 38B 也**不产出**坐标点（因为没有完整经纬高）。
- 每个坐标点在创建时都**继承当前 DecodeState 的所有缓存字段**（time、fix、std、vel、attitude 等），即使这些字段来自更早的 packet。

## 6. 来源选择

`select_position_source`：

```python
if requested == "auto":
    # 数据中有 geodetic_pos 点 → 选 geodetic
    # 否则 → 选 system
if requested == "geodetic":  selected = [p for p in points if p.position_source == "geodetic_pos"]
if requested == "system":    selected = [p for p in points if p.position_source == "system_state"]
if requested == "both":      selected = list(points)
# 然后按选中顺序重新编号 source_point_index（1 基）
```

`auto` 的判定结果会写入 `stats.auto_position_source`，CLI 输出会显示实际选择。

## 7. 过滤

`apply_filters` 按顺序应用以下过滤：

1. **include / exclude 索引**：`--include-point` / `--exclude-point`，支持 `5,8-10` 这种逗号与范围语法（`parse_index_ranges`）。索引基由 `--index-base` 决定（0 基会自动 +1 转为 1 基）。
2. **经纬度有效性**：`valid_lat_lon` 要求纬度 ∈ [-90, 90]、经度 ∈ [-180, 180]，且排除 `(0, 0)`。
3. **`--min-fix`**：`gnss_fix_code` 小于阈值的点丢弃。阈值可用数字或名称（`RTK_FLOAT` 等）。
4. **`--max-hacc` / `--max-vacc`**：`hacc_m` / `vacc_m` 超阈值的点丢弃（仅当该字段为有限值时才比较，缺失字段不过滤）。
5. **`--max-segment-m`**：跳点抑制。从第一个保留点开始，逐点与“上一次保留点”做 haversine，距离大于阈值的点丢弃。这模拟轨迹查看器常用 20 m 跳点过滤。

`stats.filtered_points` = 选中数量 − 过滤后数量。

## 8. 距离累加

`assign_route_distances`：

```python
cumulative = 0.0
previous = None
for index, point in enumerate(points, start=1):
    point.point_index = index
    if previous is None:
        point.segment_distance_m = 0.0
    else:
        point.segment_distance_m = haversine_distance_m(prev, point)
    cumulative += point.segment_distance_m
    point.cumulative_distance_m = cumulative
    previous = point
```

`route_distance_between(points, start_index, end_index)`（1 基）：

```python
lo = min(start_index, end_index); hi = max(...)
return points[hi-1].cumulative_distance_m - points[lo-1].cumulative_distance_m
```

`haversine_distance_m` 使用球面模型，地球半径 `6_371_000 m`。这表示“设备实际记录轨迹的折线长度”，不是道路网络导航距离，也不做地图匹配。

## 9. 一句话总结

核心解析链路：**识别魔数 → 按记录头迭代出”帧字节” → FDILink 帧校验（head / size / tail / crc8 / crc16）→ 按 packet_id 更新跨帧状态机 DecodeState → 仅对 0x50 / 0x5C 产出经纬度点 → 来源选择 → 索引 / fix / 精度 / 跳点过滤 → haversine 累加距离。**