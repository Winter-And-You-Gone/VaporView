# EPSILON 原始轨迹解析脚本说明

`scripts/epsilon_raw_route.py` 用于从 VaporView 记录的 EPSILON 原始数据中解析轨迹点，计算沿记录轨迹的路线距离，导出 CSV，并生成可在浏览器打开的交互式 HTML 地图。

脚本只依赖 Python 标准库。

## 支持的输入

可以直接传入以下任一输入：

- 记录会话目录：脚本会优先查找 `raw/epsilon.dat`，也兼容旧版 `sensors/epsilon_raw.dat`
- `raw/epsilon.dat`：统一 raw DAT，文件头 `VVRAWDAT`
- `sensors/epsilon_raw.dat`：旧版 EPSILON raw DAT，文件头 `VVEPSRAW`
- 裸 FDILink 字节流：连续的 `0xFC ... 0xFD` 帧

## 快速使用

导出 CSV 和地图，并计算第 1 点到第 300 点之间的路线距离：

```powershell
python scripts/epsilon_raw_route.py "X:\path\to\session_2026-xx-xx_xx-xx-xx" `
  --output-csv exports\epsilon_route.csv `
  --map-html exports\epsilon_route.html `
  --start-index 1 `
  --end-index 300
```

过滤指定点，例如过滤第 12 点以及第 20 到 25 点：

```powershell
python scripts/epsilon_raw_route.py raw\epsilon.dat `
  --output-csv exports\epsilon_route_filtered.csv `
  --map-html exports\epsilon_route_filtered.html `
  --exclude-point 12,20-25
```

按质量过滤，例如只保留 RTK_FLOAT 及以上 fix，且水平精度不超过 0.2 m：

```powershell
python scripts/epsilon_raw_route.py raw\epsilon.dat `
  --min-fix RTK_FLOAT `
  --max-hacc 0.2 `
  --output-csv exports\epsilon_route_rtk.csv
```

模拟轨迹查看器的跳点过滤，可加：

```powershell
--max-segment-m 20
```

## 轨迹点来源

默认 `--position-source auto`：

1. 如果原始数据中存在 `0x5C GEODETIC_POS`，使用它作为轨迹点来源；
2. 如果没有 `0x5C`，回退到 `0x50 SYS_STATE` 中的经纬高字段。

也可以手动指定：

- `--position-source geodetic`：只用 `0x5C GEODETIC_POS`
- `--position-source system`：只用 `0x50 SYS_STATE`
- `--position-source both`：两类坐标帧都导出；注意如果两个包同时开启，距离可能因重复近邻点略有膨胀

## 距离计算说明

路线距离不是起终点直线距离。脚本按过滤后轨迹点顺序逐段计算相邻点的 haversine 球面距离，再从 `start-index` 累加到 `end-index`。

这代表“设备实际记录轨迹的折线长度”，不是道路网络导航距离，也不会进行地图匹配。

## 地图输出说明

`--map-html` 会生成一个自带轨迹点数据和计算逻辑的交互式 HTML：

- 灰线：过滤后的完整轨迹
- 橙线：`--start-index` 到 `--end-index` 之间参与距离计算的路线段
- 绿色点：起点
- 红色点：终点
- 右侧面板可选择起点、终点、过滤点，并实时重算路线距离
- 可在天地图矢量、天地图卫星和 OpenStreetMap 之间切换底图

默认底图是天地图矢量，默认 Key 为 `5a0d3293c900281a37417b2e4d4d3676`。如需替换 Key：

```powershell
python scripts/epsilon_raw_route.py raw\epsilon.dat `
  --map-html exports\epsilon_route.html `
  --tianditu-key your_tianditu_key
```

HTML 打开后不需要 Python 或后端服务；CSV 和距离计算数据都已经写入 HTML。地图底图瓦片需要联网加载，天地图需要访问 `tianditu.gov.cn`，OpenStreetMap 需要访问 `tile.openstreetmap.org`。

## CSV 字段说明

| 字段 | 含义 |
| --- | --- |
| `point_index` | 过滤后的轨迹点编号，默认 1 基；`--start-index` 和 `--end-index` 使用这个编号 |
| `source_point_index` | 选定坐标包来源中的原始点编号，在 `--exclude-point` / `--include-point` 中使用 |
| `source_file` | 原始数据文件路径 |
| `raw_format` | 解析到的原始文件格式：`unified_v*`、`legacy_epsilon_v*` 或 `fdilink_stream` |
| `record_sequence` | raw DAT 记录序号；裸 FDILink 流为脚本扫描序号 |
| `host_timestamp_us` | VaporView 主机接收时间，Unix epoch 微秒 |
| `host_time_utc` | `host_timestamp_us` 的 UTC ISO-8601 文本 |
| `packet_id_hex` | 提供该坐标点的 FDILink packet id |
| `packet_name` | packet id 名称，例如 `0x5C GEODETIC_POS` |
| `position_source` | 坐标来源：`geodetic_pos` 或 `system_state` |
| `serial_number` | FDILink 帧流水号 |
| `utc_unix_s` | EPSILON UTC 秒，来自 `0x50` / `0x51` / `0x59` 等状态包 |
| `utc_microseconds` | EPSILON UTC 微秒部分 |
| `epsilon_time_utc` | EPSILON UTC 时间的 ISO-8601 文本 |
| `latitude_deg` | 纬度，单位度 |
| `longitude_deg` | 经度，单位度 |
| `height_m` | 高程，单位米 |
| `segment_distance_m` | 当前点到上一保留点的轨迹段距离，单位米 |
| `cumulative_distance_m` | 从第一个保留点累计到当前点的路线距离，单位米 |
| `gnss_fix_code` | GNSS fix 数字码，沿用 EPSILON/VaporView 解析 |
| `gnss_fix` | GNSS fix 文本，例如 `3D`、`RTK_FLOAT`、`RTK_FIXED` |
| `gnss_satellites` | 卫星数，通常来自 `0x5A SATELLITES` |
| `hdop` | 水平精度因子，来自 `0x5A` |
| `vdop` | 垂直精度因子，来自 `0x5A` |
| `hacc_m` | 水平精度估计，单位米，通常来自 `0x5C` |
| `vacc_m` | 垂直精度估计，单位米，通常来自 `0x5C` |
| `lat_std_m` | 纬度标准差，单位米，来自 `0x50` 或 `0x59` |
| `lon_std_m` | 经度标准差，单位米，来自 `0x50` 或 `0x59` |
| `height_std_m` | 高度标准差，单位米，来自 `0x50` 或 `0x59` |
| `diff_age_s` | 差分龄期，单位秒，来自 `0x59 RAW_GNSS` |
| `vel_n_mps` | 北向速度，单位 m/s |
| `vel_e_mps` | 东向速度，单位 m/s |
| `vel_d_mps` | 地向速度，单位 m/s |
| `ned_n_m` | 本地 NED 北向位置，单位米，来自 `0x42 INS_GPS` |
| `ned_e_m` | 本地 NED 东向位置，单位米，来自 `0x42 INS_GPS` |
| `ned_d_m` | 本地 NED 地向位置，单位米，来自 `0x42 INS_GPS` |
| `roll_deg` | 横滚角，单位度 |
| `pitch_deg` | 俯仰角，单位度 |
| `yaw_deg` | 航向角，单位度 |
| `system_status_bits` | EPSILON system status 原始位字段 |
| `filter_status_bits` | EPSILON filter status 原始位字段 |
| `update_status_bits` | EPSILON update status 原始位字段 |
| `record_offset` | raw 文件中记录头偏移，单位字节 |
| `payload_offset` | raw 文件中 raw payload 起始偏移；对 EPSILON 记录来说这里是完整 FDILink 帧的起始偏移，单位字节 |

## 自检

可以运行：

```powershell
python scripts/epsilon_raw_route.py --self-test
```

自检会生成临时统一 raw DAT、解析 3 个模拟 `0x5C` 点、导出 CSV 和 HTML，并确认路线距离为正。
