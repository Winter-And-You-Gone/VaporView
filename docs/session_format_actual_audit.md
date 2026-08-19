# VaporView 实际落盘格式全量清单

本文档记录真实会话目录的逐文件、逐字段审计结果。内容只来自文件字节、JSON、CSV 和 RAW DAT 实际扫描结果，不根据生产代码推测样例中不存在的字段。

> **样例边界：** 本文档早期章节中的 `data\\对比1` 是历史版本样例，只用于记录格式演进和历史问题，**不代表当前 `origin/main` 的实际输出行为**。当前代码的事实审计见末尾的 **Current origin/main actual session audit** 章节。

## 审计基线

- 样例目录：X:\Project\GPS\VaporView\data\对比1
- 对照规范：VaporView_离线会话数据格式与解析规范_v1.0.md
- 审计对象：session.json、config/device_config.json、4 个结构化 CSV、logs/event_log.csv、7 个 RAW DAT、raw/waveform_peaks.csv
- 伴随文件：raw_dat_format.md、logs/error_log.txt（存在，但不列入本次字段清单）
- RAW DAT 检查：逐文件读取 20 Byte 文件头、逐记录读取 36 Byte 记录头，检查 payload 边界、source ID、sequence 和 waveform 子 payload。
- CSV 检查：检查实际 UTF-8 字节、BOM、换行、字段序列和数据行数。

## 1. 实际文件清单

| 文件 | 字节数 | 实际结构/记录数 | 结果 |
|---|---:|---:|---|
| session.json | 3,284 | 1 个 JSON 对象 | 可解析；包含规范未列出的扩展字段 |
| config/device_config.json | 2,086 | 1 个 JSON 对象 | 可解析；包含共享配置扩展字段 |
| sensors/sensor_summary.csv | 3,742,434 | 77 列，6,148 行 | 表头逐项一致 |
| sensors/laser_temperature_controller.csv | 320 | 24 列，0 行 | 只有标准表头 |
| sensors/system_temperature_controller.csv | 444 | 27 列，0 行 | 只有标准表头 |
| sensors/waveform_features.csv | 164 | 14 列，0 行 | 只有标准表头 |
| logs/event_log.csv | 336 | 4 列，2 行 | 可解析；规范没有逐字段定义 |
| raw/navigation.dat | 15,911,058 | 147,856 records | RAW 外层检查通过 |
| raw/pressure.dat | 295,612 | 6,718 records | RAW 外层检查通过 |
| raw/temperature_humidity.dat | 253,399 | 5,171 records | RAW 外层检查通过 |
| raw/distance.dat | 20 | 0 records | 有效零记录文件 |
| raw/waveform.dat | 1,224,233,084 | 15,369 records | RAW 外层和 waveform 子 payload 检查通过 |
| raw/laser_temperature_controller.dat | 20 | 0 records | 有效零记录文件 |
| raw/system_temperature_controller.dat | 20 | 0 records | 有效零记录文件 |
| raw/waveform_peaks.csv | 659,178 | 6 列，15,369 行 | 表头一致；点数值与 waveform RAW 不一致 |

所有 CSV 均为 UTF-8、无 BOM、CRLF 换行。空结构化 CSV 保留标准表头。7 个 RAW DAT 零记录文件均保留有效 20 Byte 文件头。

## 2. session.json

### 2.1 顶层字段逐项对照

样例实际顶层字段为：

    session_format
    session_format_version
    recording_origin
    session_name
    state
    start_time_utc
    start_time_us
    end_time_utc
    end_time_us
    elapsed_ms
    software_version
    timestamp_unit
    raw_dat_format_version
    epsilon_schema_version
    sensor_export_rate_hz
    other_devices_export_rate_hz
    raw_export_mode
    waveform_export_rate_hz
    waveform_export_mode
    waveform_value_type
    waveform_timestamp_type
    waveform_points_per_frame
    waveform_file_count
    capture
    counts
    paths
    raw_files

| 字段 | 样例实际值 | 与 v1.0 规范 |
|---|---|---|
| session_format | vaporview.session | 一致 |
| session_format_version | 数字 1 | 一致 |
| recording_origin | ground | 一致 |
| session_name | session_2026-06-26_14-21-41 | 值符合会话命名；外层样例目录 对比1 是对比目录名 |
| state | complete | 一致 |
| start_time_utc | 06/26/2026 06:21:41 | 类型为字符串；规范未限定字符串布局 |
| start_time_us | 1782454901541000，字符串 | 类型和单位一致 |
| end_time_utc | 06/26/2026 06:26:48 | 类型为字符串；规范未限定字符串布局 |
| end_time_us | 1782426408000000，字符串 | 值早于 start 和所有实际记录 |
| elapsed_ms | -28493541，字符串 | 值为负，不通过规范时间完整性检查 |
| software_version | 1.0.0 | 一致 |
| timestamp_unit | microseconds | 一致 |
| raw_dat_format_version | 数字 2 | 一致 |
| epsilon_schema_version | 数字 1 | 一致 |
| sensor_export_rate_hz | 数字 20 | 一致 |
| other_devices_export_rate_hz | 数字 20 | 一致 |
| raw_export_mode | unified_raw_dat | 一致 |
| waveform_export_rate_hz | 数字 0 | 类型一致；规范没有要求非零 |
| waveform_export_mode | per_frame | 一致 |
| waveform_value_type | float32 | 一致 |
| waveform_timestamp_type | uint64 | 一致 |
| waveform_points_per_frame | 数字 50000 | 实际 waveform harmonic 点数为 9,903，样例元数据不一致 |
| waveform_file_count | 字符串 1 | 实际存在且正确；v1.0 顶层字段表未列出 |
| capture | object，4 个键均为 null | 实际存在；v1.0 顶层字段表未列出 |
| counts | 见 2.2 | 结构一致 |
| paths | 见 2.3 | 值全部为标准相对路径 |
| raw_files | 见 2.4 | 结构一致 |

实际 capture 对象包含以下四个字段，样例值均为 null：

```text
capture.telemetry_transport
capture.telemetry_endpoint
capture.telemetry_port
capture.telemetry_baud
```

### 2.2 counts 逐字段对照

| 字段 | JSON 值 | 实际读取值 | 结果 |
|---|---:|---:|---|
| sensor_rows | 6148 | sensor_summary 6,148 行 | 一致 |
| laser_temperature_controller_rows | 0 | 激光温控 CSV 0 行 | 一致 |
| system_temperature_controller_rows | 0 | 系统温控 CSV 0 行 | 一致 |
| waveform_frames | 15369 | waveform.dat 15,369 records | 一致 |
| waveform_feature_rows | 0 | waveform_features CSV 0 行 | 一致 |
| event_rows | 2 | event_log 2 行 | 一致 |
| error_rows | 0 | error_log 为空 | 一致 |

### 2.3 paths 逐字段对照

样例 16 个路径键全部存在，值均为规范标准相对路径：

    sensor_summary_csv                 = sensors/sensor_summary.csv
    laser_temperature_controller_csv  = sensors/laser_temperature_controller.csv
    system_temperature_controller_csv = sensors/system_temperature_controller.csv
    waveform_features_csv             = sensors/waveform_features.csv
    navigation_raw                    = raw/navigation.dat
    pressure_raw                      = raw/pressure.dat
    temperature_humidity_raw          = raw/temperature_humidity.dat
    distance_raw                      = raw/distance.dat
    waveform_raw                      = raw/waveform.dat
    laser_temperature_controller_raw  = raw/laser_temperature_controller.dat
    system_temperature_controller_raw = raw/system_temperature_controller.dat
    waveform_peaks_csv                = raw/waveform_peaks.csv
    event_log                         = logs/event_log.csv
    error_log                         = logs/error_log.txt
    device_config                     = config/device_config.json
    raw_format_document               = raw_dat_format.md

### 2.4 raw_files 逐字段对照

7 个条目均包含规范要求的 path、source_id、format_version、records，且 records 与扫描结果一致。

| 条目 | source_id | format_version | JSON records | 实际 records |
|---|---:|---:|---:|---:|
| navigation | 1 | 2 | 147856 | 147,856 |
| pressure | 2 | 2 | 6718 | 6,718 |
| temperature_humidity | 3 | 2 | 5171 | 5,171 |
| distance | 4 | 2 | 0 | 0 |
| waveform | 5 | 2 | 15369 | 15,369 |
| laser_temperature_controller | 6 | 2 | 0 | 0 |
| system_temperature_controller | 7 | 2 | 0 | 0 |

## 3. device_config.json

### 3.1 顶层字段

实际字段为：

    device_config_format
    device_config_format_version
    recording_origin
    recording_directory
    session_directory
    epsilon_schema_version
    sensor_export_rate_hz
    other_devices_export_rate_hz
    raw_export_mode
    raw_dat_format_version
    waveform_export_rate_hz
    waveform_export_mode
    telemetry
    waveform
    raw_dat
    sensors

前 10 个规范示例字段均存在且类型和值符合样例约定。实际还存在以下规范示例未列出的字段：

    waveform_export_rate_hz
    waveform_export_mode
    telemetry.transport
    telemetry.endpoint
    telemetry.port
    telemetry.baud
    raw_dat.directory
    raw_dat.format_doc
    raw_dat.mode

实际值为：waveform_export_rate_hz=0、waveform_export_mode=per_frame；telemetry 四字段均为 null；raw_dat 为 raw、raw_dat_format.md、per_verified_raw_frame_or_response。

### 3.2 waveform 逐字段对照

| 字段 | 实际值 | 与规范 |
|---|---:|---|
| host | 127.0.0.1 | 一致 |
| port | 8888 | 一致 |
| frame_rate_hz | 0 | 类型一致 |
| frame_rate_mode | per_frame | 类型一致 |
| points_per_frame | 50000 | 实际 harmonic payload 为 9,903 点 |
| value_type | float32 | 一致 |
| timestamp_type | uint64 | 一致 |

### 3.3 sensors 逐字段对照

6 个 sensor 对象都实际包含以下字段：

    configured, enabled, port, baud, rate_hz, slave_address

规范示例只列出 port、baud、rate_hz；configured、enabled、slave_address 是实际存在但规范未定义的扩展字段。

| sensor | configured | enabled | port | baud | rate_hz | slave_address |
|---|---:|---:|---|---|---|---|
| epsilon | true | true | COM7 | 921600 | null | null |
| ptb | true | true | COM23 | 9600 | 50 | null |
| hmp | true | true | COM22 | 19200 | 500 | null |
| lidar | false | false | null | 500000 | 20 | null |
| laser_temperature_controller | false | false | null | null | null | null |
| system_temperature_controller | false | false | null | null | null | null |

## 4. 四个结构化 CSV

### 4.1 sensors/sensor_summary.csv

实际 77 列、6,148 行；与规范 77 列表头逐项同名同序：

    record_timestamp_us,epsilon_host_timestamp_us,epsilon_device_timestamp_us,epsilon_utc_unix_s,epsilon_utc_microseconds,nav_lat_deg,nav_lon_deg,nav_height_m,ecef_x_m,ecef_y_m,ecef_z_m,ned_n_m,ned_e_m,ned_d_m,vel_n_mps,vel_e_mps,vel_d_mps,body_vel_x_mps,body_vel_y_mps,body_vel_z_mps,body_acc_x_mps2,body_acc_y_mps2,body_acc_z_mps2,roll_deg,pitch_deg,yaw_deg,quat_w,quat_x,quat_y,quat_z,attitude_source_count,attitude_delta_max_deg,attitude_delta_ahrs_euler_deg,attitude_delta_ahrs_quat_deg,attitude_delta_euler_quat_deg,ang_vel_x_radps,ang_vel_y_radps,ang_vel_z_radps,imu_acc_x_mps2,imu_acc_y_mps2,imu_acc_z_mps2,imu_gyr_x_radps,imu_gyr_y_radps,imu_gyr_z_radps,mag_x_mg,mag_y_mg,mag_z_mg,gnss_fix,gnss_satellites,hdop,vdop,hacc_m,vacc_m,lat_std_m,lon_std_m,height_std_m,diff_age_s,heading_valid,system_status_bits,filter_status_bits,update_status_bits,epsilon_imu_packet_rate_hz,epsilon_ahrs_packet_rate_hz,epsilon_insgps_packet_rate_hz,epsilon_sys_state_packet_rate_hz,epsilon_raw_gnss_packet_rate_hz,epsilon_satellite_packet_rate_hz,epsilon_geodetic_packet_rate_hz,epsilon_ecef_packet_rate_hz,epsilon_valid,epsilon_error_message,hmp_temperature_c,hmp_humidity_rh,ptb_pressure_hpa,lidar_distance_m,lidar_signal_strength,lidar_valid

### 4.2 sensors/laser_temperature_controller.csv

实际 24 列、0 行；字段序列逐项一致：

    host_time_us,valid,internal_temperature_c,error_code,ch1_target_c,ch1_measured_c,ch1_output_percent,ch1_output_current_a,ch1_enabled,ch1_mode,ch1_max_output_percent,ch1_kp,ch1_ki,ch1_kd,ch2_target_c,ch2_measured_c,ch2_output_percent,ch2_output_current_a,ch2_enabled,ch2_mode,ch2_max_output_percent,ch2_kp,ch2_ki,ch2_kd

样例没有有效激光温控数据行，不能从样例验证数值换算。

### 4.3 sensors/system_temperature_controller.csv

实际 27 列、0 行；字段序列逐项一致：

    host_time_us,valid,ch1_measured_c,ch2_measured_c,ch3_measured_c,ch4_measured_c,ch5_measured_c,ch6_measured_c,ch7_measured_c,ch8_measured_c,control_states_valid,ch1_control_state,ch2_control_state,ch3_control_state,ch4_control_state,ch5_control_state,ch6_control_state,ch7_control_state,ch8_control_state,alarm_status_valid,alarm_status_reg1,alarm_status_reg2,alarm_status_reg3,alarm_status_reg4,main_status_valid,main_status_raw,error_message

样例没有有效系统温控数据行，不能从样例验证 8 路温度和寄存器数据行。

### 4.4 sensors/waveform_features.csv

实际 14 列、0 行；字段序列逐项一致：

    host_time_us,epsilon_time_us,original_point_count,search_start_index,search_end_index,channel_id,peak,mean,rms,peak_index,peak_x,min_value,max_value,quality_flags

## 5. logs/event_log.csv

实际表头为：

    timestamp_utc,timestamp_us,level,message

| 字段 | 样例实际内容 | v1.0 规范 |
|---|---|---|
| timestamp_utc | 2026-06-26T06:21:41.546Z 形式 | 规范只说明文件用途，未定义字段 |
| timestamp_us | UTC 微秒时间戳文本 | 未定义 |
| level | 小写 info | 未定义枚举 |
| message | 开始/结束记录中文消息 | 未定义字段和转义示例 |

实际两行消息中的设备行数 6148、波形帧数 15369 与 session.json.counts 一致。event log 结构可读，但规范缺少逐字段定义；本文补充的是实际观察，不把缺项误判成样例结构错误。

## 6. 七个 RAW DAT

### 6.1 公共文件头和记录头

7 个文件的 20 Byte 文件头全部为：

| Offset | 字段 | 实际检查 | 与规范 |
|---:|---|---|---|
| 0 | magic | ASCII VVRAWDAT | 一致 |
| 8 | version | 2 | 一致 |
| 12 | header_size | 20 | 一致 |
| 16 | source_id | 1~7，各文件唯一 | 一致 |
| 18 | reserved | 0 | 一致 |

所有非空文件的每条记录头均满足：marker 0x44525756、header_size 36、source_id 与文件头一致、payload 边界正确；sequence 均从 0 连续递增，无缺号。

记录头字段逐项为：

| Offset | 字段 | 实际检查 |
|---:|---|---|
| 0 | marker | 每条均为 0x44525756 |
| 4 | header_size | 每条均为 36 |
| 8 | host_timestamp_us | 64 bit little-endian 时间戳，可读 |
| 16 | payload_size | 与实际 payload 边界一致 |
| 20 | source_id | 与文件头一致 |
| 22 | record_type | 按数据源分布见 6.2 |
| 24 | flags | 按数据源分布见 6.2 |
| 28 | sequence | 每个文件从 0 连续递增 |

### 6.2 分源实际记录

| 文件 | source_id | records | record_type | flags | payload_size | 与规范 |
|---|---:|---:|---|---|---|---|
| navigation.dat | 1 | 147,856 | 0x40,0x41,0x42,0x50,0x59,0x5A,0x5C,0x5D,0xF0 | 低 8 bit 实际承载设备序号值 | 17~110 Byte | 规范列举常见值并保留省略号；样例补充出完整观察集合 |
| pressure.dat | 2 | 6,718 | 1 | 0 | 8 Byte | 与规范一致 |
| temperature_humidity.dat | 3 | 5,171 | 3 | 0 | 13 Byte | 与规范一致 |
| distance.dat | 4 | 0 | 无 | 无 | 无 | 有效零记录文件；无法由样例验证非空距离帧 |
| waveform.dat | 5 | 15,369 | 1 | 513 (0x201) | 79,620 Byte | bit0 combined、bits8~9=1 Little Endian Float32，与规范一致 |
| laser_temperature_controller.dat | 6 | 0 | 无 | 无 | 无 | 有效零记录文件；无法由样例验证 Modbus payload |
| system_temperature_controller.dat | 7 | 0 | 无 | 无 | 无 | 有效零记录文件；无法由样例验证四种 record type |

### 6.3 waveform.dat 子 payload

15,369 条 waveform 记录全部满足：

    payload_size = 79620
    raw_size = 40000 Byte
    harmonic_size = 39612 Byte
    8 + raw_size + harmonic_size = 79620
    raw_point_count = 40000 / 4 = 10000
    harmonic_point_count = 39612 / 4 = 9903

两段长度均为 4 的整数倍，所有子 payload 长度校验通过；flags=0x201 表示 combined payload 和 Little Endian Float32。RAW DAT 二进制布局与规范一致。

但样例元数据仍为：

    session.json.waveform_points_per_frame = 50000
    device_config.json.waveform.points_per_frame = 50000

这属于元数据内容不一致，不是 RAW DAT 二进制结构不一致。

## 7. raw/waveform_peaks.csv

实际表头与规范逐项一致：

    host_time_us,peak_value,peak_index,point_count,search_start_index,search_end_index

实际 15,369 行全部为：

    point_count = 50000
    peak_index = -1
    search_start_index = 0
    search_end_index = 0

因此 point_count=50000 与 RAW waveform 的 harmonic 实际点数 9903 不一致。规范定义了字段名，但没有明确 point_count 应取 raw signal 还是 normalized 2f；该样例的 RAW、peaks CSV 和 JSON 元数据没有形成一致的点数语义。

## 8. 跨文件结论

### 8.1 通过项

- 7 个 RAW DAT 均存在，source ID 1~7 与文件名对应。
- RAW 文件头、记录头、payload 边界和 sequence 均通过实际扫描。
- session.json.raw_files[*].records 与实际 DAT records 逐项一致。
- session.json.counts 与 CSV、event log、waveform RAW 实际行数/记录数逐项一致。
- 4 个结构化 CSV 的字段序列与规范逐项一致。
- waveform raw/harmonic 子 payload 长度和 Float32 对齐通过。

### 8.2 样例内容不一致项

1. session.json.end_time_us < start_time_us，elapsed_ms 为负值；结束时间也早于所有实际 RAW 记录。
2. session.json.waveform_points_per_frame=50000，实际 harmonic 点数为 9903。
3. device_config.json.waveform.points_per_frame=50000，实际 harmonic 点数为 9903。
4. raw/waveform_peaks.csv.point_count=50000，实际 harmonic 点数为 9903。
5. 所有 waveform peaks 行的 peak_index=-1、搜索范围 0,0，样例没有有效峰值索引。
6. event message 含历史路径 C:/WorkSpace/NewVaporView/...，与当前样例目录不同；规范没有定义 message 正文路径语义，仅记录为内容观察。

### 8.3 规范未逐字段定义但实际存在的内容

- session.json.capture 四字段；
- session.json.waveform_file_count；
- device_config.json 顶层 waveform_export_rate_hz、waveform_export_mode、telemetry、raw_dat；
- device_config.json.sensors.*.configured、enabled、slave_address；
- event_log.csv 的四列、level 枚举和 message 语义；
- Navigation 实际观察到的完整 Packet ID 集合；
- waveform_peaks.csv.point_count 与 waveform RAW raw/harmonic 点数的对应关系。

## 9. 结论

data\对比1 的容器和二进制/CSV 结构大部分符合 v1.0：路径、CSV 表头、RAW DAT 外层格式、source ID、记录头、sequence 和记录计数均可通过实际解析。该样例不能作为元数据全部一致的样板，尤其是结束时间和 waveform 点数元数据存在明确矛盾。

本文档只补充实际落盘事实和规范差异，不修改样例文件、RAW DAT、生产代码或兼容解析策略。

---

## Current origin/main actual session audit

本章节是本轮基于当前 `origin/main` 重新构建、重新录制并逐字节扫描的新样例。它不使用 `data\\对比1` 的字段值来判断当前行为。

### C.1 代码基线和生成方式

| 项目 | 实际值 |
|---|---|
| `HEAD` | `346e91fa705c4c7a4381196773f6b3e0db8b8b07` |
| `origin/main` | `346e91fa705c4c7a4381196773f6b3e0db8b8b07` |
| 最近提交 | `346e91fa Document actual session package audit`；前一提交 `3ae7bb46 Align waveform point count metadata` |
| Release 构建目录 | `X:\Project\GPS\VaporView\build\Release` |
| `VaporViewSkyCore.exe` 构建时间 | 2026-08-20 02:21:41（本机时间） |
| `VaporViewSky.exe` 构建时间 | 2026-08-20 02:23:44（本机时间） |
| 构建命令 | `cmd.exe /d /s /c 'chcp 65001 >NUL && call "F:\\VisualStudio\\2022\\BuildTools\\Common7\\Tools\\VsDevCmd.bat" -arch=x64 -host_arch=x64 >NUL && cmake --build build\\Release --config Release --target VaporViewSkyCore VaporViewSky VaporView --clean-first -- -j1 > build\\Release\\audit_current_clean_build.log 2>&1'` |
| 构建结果 | 成功，242 steps，只有 third-party `hipnuc_dec.c` 的既有 C4244/C4701 warning |
| 录制模式 | Sky `--sky-simulate-data`，使用当前 Release 的 `VaporViewSkyCore.exe` |
| 配置 | `build\\Release\\audit_current_sky_config.json`；7 个 Sky 模拟设备均 enabled，wave TCP 使用 50,000 点模拟帧 |
| 生命周期 | IPC `ConnectAllDevices` -> `StartRecording` -> 约 8 秒数据 -> `StopRecording` -> `ShutdownCore`；使用正常 Stop，没有强制结束生成 session |
| 新样例 | `X:\Project\GPS\VaporView\data\session_2026-08-20_02-32-55` |
| 生成时间 | `start_time_utc=2026-08-19T18:32:55.635Z`；`end_time_utc=2026-08-19T18:33:04.376Z` |

本轮只生成了 Sky 样例。Sky 模拟器同时产生了导航、压力、温湿度、距离、waveform、激光温控和系统温控的结构化遥测，因此没有再重复生成 Ground 样例；六类协议 RAW 的实际回调能力边界见 C.8。

### C.2 新 session 完整目录树

```text
session_2026-08-20_02-32-55/
|-- session.json                                      3,598 bytes
|-- raw_dat_format.md                                  972 bytes
|-- config/
|   `-- device_config.json                             2,389 bytes
|-- logs/
|   |-- event_log.csv                                    212 bytes
|   `-- error_log.txt                                      0 bytes
|-- raw/
|   |-- navigation.dat                                   20 bytes (0 records)
|   |-- pressure.dat                                     20 bytes (0 records)
|   |-- temperature_humidity.dat                         20 bytes (0 records)
|   |-- distance.dat                                     20 bytes (0 records)
|   |-- waveform.dat                              3,600,416 bytes (9 records)
|   |-- laser_temperature_controller.dat                 20 bytes (0 records)
|   |-- system_temperature_controller.dat                20 bytes (0 records)
|   `-- waveform_peaks.csv                              494 bytes (9 rows)
`-- sensors/
    |-- sensor_summary.csv                           61,296 bytes (88 rows)
    |-- laser_temperature_controller.csv             12,400 bytes (80 rows)
    |-- system_temperature_controller.csv            12,044 bytes (80 rows)
    `-- waveform_features.csv                        10,164 bytes (80 rows)
```

六个零记录协议 RAW 不是伪造的空文件：每个文件均保留了合法的 20-byte `VVRAWDAT` 文件头，manifest `raw_files[*].records` 也为字符串 `"0"`。Sky 模拟路径只模拟已解析的结构化设备状态和 waveform，不向 `SkyRuntime` 的六个 `recordRaw*` 回调注入协议原始帧，因此本样例不能声称覆盖这些 payload 的实际字节内容。

### C.3 `session.json` 完整字段树

当前文件的完整顶层字段（按实际 JSON 输出顺序）如下。JSON 中计数和时间整数的类型并不统一：manifest writer 对大整数和计数使用十进制字符串，协议/版本/速率使用 JSON number。

```text
session_format                         string  "vaporview.session"
session_format_version                 number  1
recording_origin                       string  "sky"
session_name                           string  "session_2026-08-20_02-32-55"
state                                  string  "complete"
start_time_utc                         string  "2026-08-19T18:32:55.635Z"
start_time_us                          string  "1787164375635000"
end_time_utc                           string  "2026-08-19T18:33:04.376Z"
end_time_us                            string  "1787164384376000"
elapsed_ms                             string  "8741"
software_version                       string  "1.0.22"
timestamp_unit                         string  "microseconds"
raw_dat_format_version                 number  2
epsilon_schema_version                 number  1
sensor_export_rate_hz                  number  0
other_devices_export_rate_hz           number  0
raw_export_mode                        string  "unified_raw_dat"
waveform_export_rate_hz                number  0
waveform_export_mode                   string  "per_frame"
waveform_value_type                    string  "float32"
waveform_timestamp_type                string  "uint64"
waveform_points_per_frame              number  50000
waveform_file_count                    string  "1"
capture                                object
counts                                 object
paths                                  object
raw_files                              object
```

嵌套字段和值也全部实际读取如下：

```text
capture.telemetry_baud                 null
capture.telemetry_endpoint             string  "tcp://127.0.0.1:39191"
capture.telemetry_port                 string  "tcp://127.0.0.1:39191"
capture.telemetry_transport            string  "tcp"

counts.error_rows                      string  "0"
counts.event_rows                      string  "2"
counts.laser_temperature_controller_rows string "80"
counts.sensor_rows                     string  "88"
counts.system_temperature_controller_rows string "80"
counts.waveform_feature_rows           string  "80"
counts.waveform_frames                 string  "9"

paths.device_config                    "config/device_config.json"
paths.distance_raw                     "raw/distance.dat"
paths.error_log                        "logs/error_log.txt"
paths.event_log                        "logs/event_log.csv"
paths.laser_temperature_controller_csv "sensors/laser_temperature_controller.csv"
paths.laser_temperature_controller_raw "raw/laser_temperature_controller.dat"
paths.navigation_raw                   "raw/navigation.dat"
paths.pressure_raw                     "raw/pressure.dat"
paths.raw_format_document              "raw_dat_format.md"
paths.sensor_summary_csv               "sensors/sensor_summary.csv"
paths.system_temperature_controller_csv "sensors/system_temperature_controller.csv"
paths.system_temperature_controller_raw "raw/system_temperature_controller.dat"
paths.temperature_humidity_raw         "raw/temperature_humidity.dat"
paths.waveform_features_csv            "sensors/waveform_features.csv"
paths.waveform_peaks_csv               "raw/waveform_peaks.csv"
paths.waveform_raw                     "raw/waveform.dat"

raw_files.navigation                   source_id=1 format_version=2 records="0" path="raw/navigation.dat"
raw_files.pressure                     source_id=2 format_version=2 records="0" path="raw/pressure.dat"
raw_files.temperature_humidity         source_id=3 format_version=2 records="0" path="raw/temperature_humidity.dat"
raw_files.distance                     source_id=4 format_version=2 records="0" path="raw/distance.dat"
raw_files.waveform                     source_id=5 format_version=2 records="9" path="raw/waveform.dat"
raw_files.laser_temperature_controller source_id=6 format_version=2 records="0" path="raw/laser_temperature_controller.dat"
raw_files.system_temperature_controller source_id=7 format_version=2 records="0" path="raw/system_temperature_controller.dat"
```

时间检查结果：

- `end_time_us - start_time_us = 8,741,000 us`，除以 1,000 为 `8,741 ms`，与 `elapsed_ms` 完全一致。
- `end_time_us >= start_time_us`，且 `elapsed_ms >= 0`。
- 两个 ISO UTC 字符串反解析后的 Unix microseconds 分别等于 `start_time_us` 和 `end_time_us`。
- `waveform_points_per_frame=50000` 等于最后一条合法 waveform RAW record 的 harmonic `200000 / 4 = 50000`；本次模拟器恰好使用 50,000 点，不应把这个结果泛化为固定默认值。`waveform_points_per_frame_test` 另外验证了 Ground/Sky 的 40,000 点帧和空 session。

### C.4 `config/device_config.json` 完整字段树和语义

当前文件实际字段树为：

```text
device_config_format                     string  "vaporview.device_config"
device_config_format_version             number  1
epsilon_schema_version                   number  1
other_devices_export_rate_hz             number  0
raw_dat                                 object
raw_dat.directory                        string  "raw"
raw_dat.format_doc                       string  "raw_dat_format.md"
raw_dat.mode                             string  "per_verified_raw_frame_or_response"
raw_dat_format_version                   number  2
raw_export_mode                          string  "unified_raw_dat"
recording_directory                      string  "X:/Project/GPS/VaporView/data"
recording_origin                         string  "sky"
sensor_export_rate_hz                    number  0
session_directory                       string  "X:/Project/GPS/VaporView/data/session_2026-08-20_02-32-55"
telemetry                               object
telemetry.baud                           null
telemetry.endpoint                       string  "tcp://127.0.0.1:39191"
telemetry.port                           string  "tcp://127.0.0.1:39191"
telemetry.transport                      string  "tcp"
waveform                                object
waveform.frame_rate_hz                   number  0
waveform.frame_rate_mode                 string  "per_frame"
waveform.host                            null
waveform.points_per_frame                number  0
waveform.port                            null
waveform.timestamp_type                  string  "uint64"
waveform.value_type                      string  "float32"
waveform_export_mode                     string  "per_frame"
waveform_export_rate_hz                  number  0
```

六类 sensor 的完整输出均有同一组键；本样例实际值如下（`null` 是 JSON null，不是字符串）：

| sensor | configured | enabled | port | baud | rate_hz | slave_address |
|---|---:|---:|---|---|---|---|
| epsilon | `false` | `false` | null | null | null | null |
| ptb | `false` | `false` | null | null | null | null |
| hmp | `false` | `false` | null | null | null | null |
| lidar | `false` | `false` | null | null | null | null |
| laser_temperature_controller | `false` | `false` | null | null | null | null |
| system_temperature_controller | `false` | `false` | null | null | null | null |

这不是把本轮配置 JSON 误读成“未产生数据”：结构化 CSV 确实有六类模拟设备数据。源码语义是 Sky `SkySessionRecorder::start()`（`src/sky/recording/SkySessionRecorder.cpp:185-227`）只向共享初始化器传入 telemetry 元数据，没有传入 `SkyConfig`；`SessionDeviceConfig` 因而保持默认连接配置，且 `connectionToJson()`（`src/shared/session/SessionDeviceConfig.cpp:19-28`）把 `enabled` 定义为 `configured` 的镜像。`waveform.points_per_frame=0` 也表示开始记录前的配置值；运行时实际最近一帧点数只写入 `session.json`。这是需要明确写入规范的字段语义/快照能力边界，不在本轮直接改代码。

当前 writer 会固定输出上面列出的所有顶层、`telemetry`、`waveform`、`raw_dat` 和六类 `sensors.*` 键，因此它们都是“当前 emitted schema 必选键”；可用性不足时使用 JSON `null`，不是省略键。字段语义清单如下：

| 字段/模式 | JSON 类型 | 本样例值 | 语义 | 当前 writer 是否必选 |
|---|---|---|---|---|
| `device_config_format`, `device_config_format_version` | string/number | `vaporview.device_config` / `1` | 配置对象 schema 标识 | 是 |
| `recording_origin`, `recording_directory`, `session_directory` | string | `sky`, 绝对路径 | 记录来源和目录定位 | 是；路径值可为空 |
| `epsilon_schema_version`, `raw_dat_format_version` | number | `1`, `2` | 协议/schema 版本 | 是 |
| `sensor_export_rate_hz`, `other_devices_export_rate_hz`, `waveform_export_rate_hz` | number | `0` | session 导出速率元数据；不是模拟器设备实际采样率 | 是 |
| `raw_export_mode`, `waveform_export_mode` | string | `unified_raw_dat`, `per_frame` | RAW 和 waveform 落盘模式 | 是 |
| `telemetry.*` | string/null | endpoint/port 为 TCP URL，其余 null | Sky telemetry 捕获端点；不可用传输参数为 null | `telemetry` 和四键均是 |
| `waveform.host`, `waveform.port` | string/number/null | null/null | 外部 waveform TCP 配置；Sky recorder 未接收时为 null | 键是；值可 null |
| `waveform.frame_rate_hz`, `waveform.points_per_frame` | number | `0`, `0` | 开始记录前配置值；不是实际最近帧结果 | 是 |
| `waveform.value_type`, `waveform.timestamp_type` | string | `float32`, `uint64` | waveform 数据类型 | 是 |
| `sensors.<name>.configured`, `.enabled` | boolean | `false`, `false` | 当前 session writer 可见的连接配置状态；`enabled` 镜像 `configured` | 键是 |
| `sensors.<name>.port`, `.baud`, `.rate_hz`, `.slave_address` | string/null | 全部 null | 连接配置；Sky recorder 不可见时为 null | 键是；值可 null |

### C.5 四个结构化 CSV 的实际完整检查

所有本轮结构化 CSV 均为 UTF-8、无 BOM、CRLF；writer 使用 `QTextStream` UTF-8。`SessionSensorCsv::escape()`（`src/shared/session/SessionSensorCsv.cpp:192-201`）会把 `"` 加倍，并在值包含逗号、引号、CR 或 LF 时用双引号包裹。本样例没有需要转义的非空字段（escaped cell count 为 0）。空值均为两个分隔符之间的空字段；bool 均为小写 `true`/`false`。

**`sensors/sensor_summary.csv`：77 列，88 行，440 个空字段**

```text
record_timestamp_us,epsilon_host_timestamp_us,epsilon_device_timestamp_us,epsilon_utc_unix_s,epsilon_utc_microseconds,nav_lat_deg,nav_lon_deg,nav_height_m,ecef_x_m,ecef_y_m,ecef_z_m,ned_n_m,ned_e_m,ned_d_m,vel_n_mps,vel_e_mps,vel_d_mps,body_vel_x_mps,body_vel_y_mps,body_vel_z_mps,body_acc_x_mps2,body_acc_y_mps2,body_acc_z_mps2,roll_deg,pitch_deg,yaw_deg,quat_w,quat_x,quat_y,quat_z,attitude_source_count,attitude_delta_max_deg,attitude_delta_ahrs_euler_deg,attitude_delta_ahrs_quat_deg,attitude_delta_euler_quat_deg,ang_vel_x_radps,ang_vel_y_radps,ang_vel_z_radps,imu_acc_x_mps2,imu_acc_y_mps2,imu_acc_z_mps2,imu_gyr_x_radps,imu_gyr_y_radps,imu_gyr_z_radps,mag_x_mg,mag_y_mg,mag_z_mg,gnss_fix,gnss_satellites,hdop,vdop,hacc_m,vacc_m,lat_std_m,lon_std_m,height_std_m,diff_age_s,heading_valid,system_status_bits,filter_status_bits,update_status_bits,epsilon_imu_packet_rate_hz,epsilon_ahrs_packet_rate_hz,epsilon_insgps_packet_rate_hz,epsilon_sys_state_packet_rate_hz,epsilon_raw_gnss_packet_rate_hz,epsilon_satellite_packet_rate_hz,epsilon_geodetic_packet_rate_hz,epsilon_ecef_packet_rate_hz,epsilon_valid,epsilon_error_message,hmp_temperature_c,hmp_humidity_rh,ptb_pressure_hpa,lidar_distance_m,lidar_signal_strength,lidar_valid
```

实际类型按列为：前 5 列 integer；导航/姿态/IMU/磁场等数值列 number；`attitude_delta_*` 和 `epsilon_error_message` 在本样例全部为空；`gnss_fix` string；`gnss_satellites`、`attitude_source_count`、状态 bit 和 lidar strength integer；`heading_valid`、`epsilon_valid`、`lidar_valid` boolean；HMP/PTB/lidar 数值列 number。

按实际列名展开的特殊类型为：integer=`record_timestamp_us,epsilon_host_timestamp_us,epsilon_device_timestamp_us,epsilon_utc_unix_s,epsilon_utc_microseconds,attitude_source_count,gnss_satellites,system_status_bits,filter_status_bits,update_status_bits,lidar_signal_strength`；string=`gnss_fix`；boolean=`heading_valid,epsilon_valid,lidar_valid`；empty-only=`attitude_delta_max_deg,attitude_delta_ahrs_euler_deg,attitude_delta_ahrs_quat_deg,attitude_delta_euler_quat_deg,epsilon_error_message`；其余 57 个数值列为 number。

**`sensors/laser_temperature_controller.csv`：24 列，80 行**

```text
host_time_us,valid,internal_temperature_c,error_code,ch1_target_c,ch1_measured_c,ch1_output_percent,ch1_output_current_a,ch1_enabled,ch1_mode,ch1_max_output_percent,ch1_kp,ch1_ki,ch1_kd,ch2_target_c,ch2_measured_c,ch2_output_percent,ch2_output_current_a,ch2_enabled,ch2_mode,ch2_max_output_percent,ch2_kp,ch2_ki,ch2_kd
```

`host_time_us` integer；`valid`、`ch1_enabled`、`ch2_enabled` boolean；温度/输出/PID 浮点字段 number；错误码、mode、上限和 PID 整数参数 integer；无空字段。

**`sensors/system_temperature_controller.csv`：27 列，80 行**

```text
host_time_us,valid,ch1_measured_c,ch2_measured_c,ch3_measured_c,ch4_measured_c,ch5_measured_c,ch6_measured_c,ch7_measured_c,ch8_measured_c,control_states_valid,ch1_control_state,ch2_control_state,ch3_control_state,ch4_control_state,ch5_control_state,ch6_control_state,ch7_control_state,ch8_control_state,alarm_status_valid,alarm_status_reg1,alarm_status_reg2,alarm_status_reg3,alarm_status_reg4,main_status_valid,main_status_raw,error_message
```

三个 `*_valid` 字段和 `valid` 为 boolean；测量值 number；控制状态、alarm registers 和 main status integer；`error_message` string，本次 80 行均为空。CSV 空字段不是 `null`、`NaN` 或文字 `empty`。

**`sensors/waveform_features.csv`：14 列，80 行**

```text
host_time_us,epsilon_time_us,original_point_count,search_start_index,search_end_index,channel_id,peak,mean,rms,peak_index,peak_x,min_value,max_value,quality_flags
```

前两列时间、点数、搜索范围、channel、quality flags 为 integer；统计和峰值坐标为 number。当前首行实际为 `host_time_us=1787164375605000, epsilon_time_us=1787164375602000, original_point_count=50000, search_start_index=0, search_end_index=50000, channel_id=4`。

### C.6 `event_log.csv` 和 `error_log.txt`

`logs/event_log.csv` 实际表头只有：

```text
timestamp_utc,timestamp_us,level,message
```

当前样例 2 行，均为 UTF-8、无 BOM、CRLF；`timestamp_utc` 是 Qt `ISODateWithMs` 的 UTC 字符串，`timestamp_us` 是 UTC Unix microseconds，`level` 当前样例为 `Info`。所有可能的 writer level 名为 `Debug`、`Info`、`Warning`、`Error`、`Critical`（`include/LogRecord.h`、`src/shared/logging/LogService.cpp:908-925`）。示例行：

```text
2026-08-19T18:32:55.652Z,1787164375653380,Info,天空端会话记录已开始。
2026-08-19T18:33:04.375Z,1787164384376825,Info,天空端会话记录已请求停止。
```

`message` 使用和其他 CSV 相同的逗号/引号/换行 escaping；当前两行无需包引号。`logs/error_log.txt` 为 UTF-8 无 BOM 的自由文本 sink；writer 格式为 `[<ISODateWithMs UTC>] <message>\\n`，Warning 及以上日志才写入。初始化器会创建空文件，因此空文件合法；本次文件为 0 bytes。**error_log.txt 为诊断文本，不作为结构化机器接口。**

### C.7 七个 RAW DAT 的全量扫描

所有文件均实际通过 `UnifiedRawDat::scan()` 等价的二进制检查：magic=`VVRAWDAT`，version=2，file header=20 bytes，record header=36 bytes，小端字段；文件头 `source_id` 分别为 1..7，reserved=0。所有非空文件的 marker=`0x44525756`，payload 完整落在文件边界内，payload 不超过 256 MiB；sequence 从 0 连续递增且无重复。

| 文件 | source_id | records | record_type 集合 | flags 集合 | payload_size 集合 | 时间/子 payload |
|---|---:|---:|---|---|---|---|
| `navigation.dat` | 1 | 0 | 空 | 空 | 空 | 本样例无 FDILink frame 可解码 |
| `pressure.dat` | 2 | 0 | 空 | 空 | 空 | 本样例无 PTB/BMP390 ASCII response |
| `temperature_humidity.dat` | 3 | 0 | 空 | 空 | 空 | 本样例无 SHT45/HMP response |
| `distance.dat` | 4 | 0 | 空 | 空 | 空 | 本样例无 TFA/AA-B7 距离帧 |
| `waveform.dat` | 5 | 9 | `{1}` | `{257 (0x101)}` | `{400008}` | 每条 raw=200000 bytes/50000 点，harmonic=200000 bytes/50000 点 |
| `laser_temperature_controller.dat` | 6 | 0 | 空 | 空 | 空 | 本样例无 Modbus response |
| `system_temperature_controller.dat` | 7 | 0 | 空 | 空 | 空 | 本样例无 4 类 AI-8288 response |

waveform 的 `flags=0x101` 是 `kWaveformCombinedPayloadFlag=0x001` 加上 bits 8..9 的 Little-Endian Float32 编码 `0x100`。每条 payload 的前 8 bytes 是 `raw_size=200000` 和 `harmonic_size=200000`，总长度 `8+200000+200000=400008`，两段均为 Float32 对齐。RAW 时间范围为 `1787164375656000..1787164383633000`，处于 session 时间范围内。

RAW 外层格式的代码常量位于 `src/shared/session/UnifiedRawDat.h:16-70`；各 writer 的 record type 来源位于 `src/sky/recording/SkySessionRecorder.cpp:728-807`。六个零记录文件无法从本次样例验证 payload 的 FDILink/ASCII/Modbus/距离 checksum 字节；没有伪造记录来填充这一缺口。

### C.8 RAW payload 能力边界和自动化验证

实际 Sky 进程的 raw 回调链路是：`SkyDeviceManager` collector 验证后发出 raw callback -> `SkyRuntime` 转接（`src/sky/core/SkyRuntime.cpp:190-224`）-> `SkySessionRecorder::recordRaw*()`。因此当前样例 0 records 的六类只能说明模拟器没有提供这些协议原始帧，不说明真实硬件 writer 会接受未验证帧。

为覆盖不能从样例得到的格式/解析约束，本轮在同一 `build/Release` 构建并运行了以下 focused tests，全部 exit 0：

```text
session_format_test
sky_session_recorder_test
ground_recording_service_test
waveform_points_per_frame_test
epsilon_protocol_test
temperature_controller_protocol_test
ai8_temperature_controller_protocol_test
sky_device_manager_simulation_test
tcp_wave_encoding_test
```

其中 `temperature_controller_protocol_test` 验证 Modbus request/response、CRC rejection 以及 BMP390 `T:... deg C,P:... Pa` 和 SHT45 温湿度文本解析；`ai8_temperature_controller_protocol_test` 覆盖 AI-8288 PV/control/alarm/main register 定义和 Modbus request；`epsilon_protocol_test` 覆盖 FDILink packet payload 解码；`sky_device_manager_simulation_test` 覆盖七类模拟结构化状态；`session_format_test`、`sky_session_recorder_test`、`ground_recording_service_test` 和 `waveform_points_per_frame_test` 覆盖共享 package、RAW header/record、waveform 子 payload 和跨 Ground/Sky 点数语义。当前仓库没有一个不接硬件就把六类非空协议 payload 通过 Sky collector 到最终 session 的端到端 fixture，这属于测试/文档缺项，不由本轮伪造数据补齐。

按当前 collector/writer 代码，非空时应验证的 payload 语义如下；“本次观察”明确区分了实际 0 records：

| source | 当前 writer/collector 约定 | 本次观察 |
|---:|---|---|
| 1 Navigation | `record_type` 直接取 FDILink packet id（1..255），payload 是 collector 回调提供的完整 FDILink frame；本次无 frame | 0 records，无法验证头尾/packet id 字节 |
| 2 Pressure | `record_type=1`；PTB210 payload 保留完整 ASCII 行（含 `\\r`/`\\n`），BMP390 解析 `T:... deg C,P:... Pa` 后也把完整原始行交给 callback | 0 records，无法验证 ASCII 保留和浮点气压 |
| 3 Temperature/Humidity | `record_type=0x03`；HMP Modbus 是完整 response，SHT45 是完整 ASCII 行（含行尾） | 0 records，无法验证 slave/function/byte-count/CRC 或 SHT45 行内容 |
| 4 Distance | `record_type` 取当前 Lidar protocol；TFA1500 固定 5-byte complement-checksum frame，低频帧为 `0x55` + length/payload/XOR，另支持 AA-B7 10-byte frame | 0 records，无法验证 checksum 或 distance 解码 |
| 5 Waveform | `record_type=1`；payload 为 8-byte size prefix + raw Float32 + harmonic Float32，flags 编码 combined 和 float byte order | 9 records，全部通过，见 C.7/C.9 |
| 6 Laser temperature controller | payload 是 collector 已通过 CRC 的完整 Modbus RTU response；`record_type` 是起始寄存器地址 | 0 records；结构化 CSV 有 80 行模拟状态 |
| 7 System temperature controller | payload 是完整 Modbus response；`record_type=1 PV, 2 Alarm Status, 3 Main Status, 4 Control Status`，每类由对应 register/byte count/CRC 解析 | 0 records；结构化 CSV 有 80 行模拟状态 |

这张表是“代码约定 + 本次能力边界”，不是把未生成的协议帧当成当前 session 数据。实际硬件验证仍需要接入对应设备或增加明确的 protocol-to-session fixture。

### C.9 `waveform_peaks.csv` 和 waveform 点数

表头为：

```text
host_time_us,peak_value,peak_index,point_count,search_start_index,search_end_index
```

本次 9 行的共同事实：

- `point_count=50000`，与每一条 `waveform.dat` harmonic 点数 50000 完全一致；
- `peak_index` 范围为 `23377..24632`，全部满足 `0 <= peak_index < 50000`；
- 每行 `search_start_index=0, search_end_index=0`；
- writer 的 `summarizeTcpWavePeakSamples()` 实际扫描整个 harmonic 数组并把其点数写入 `point_count`（`src/sky/recording/SkySessionRecorder.cpp:115-160,914-930`）；
- SkyDeviceManager 的 feature 算法把 `peak_search_end_index<=0` 展开为 `sampleCount`（`src/sky/devices/SkyDeviceManager.cpp:2735-2785`），因此当前配置 `0,0` 的有效搜索区间是 `[0,50000)`，并不是空区间。

结论是：本次 `point_count` 已经是实际 harmonic 输入数组点数，没有重采样/裁剪导致的差异；`0,0` 是“配置为默认全帧”的 sentinel，但 CSV 没有直接写出展开后的有效 end index，属于需要在文档中说明的语义缺项。本轮不把它标记为 A 类 RAW 数据 bug。

### C.10 CSV/RAW 时间交叉检查

所有 `*_time_us`/`host_time_us` 值均为 UTC Unix microseconds，数量级和 session/RAW 一致：

| 来源 | 最小值 | 最大值 | 关系 |
|---|---:|---:|---|
| `sensor_summary.record_timestamp_us` | 1787164375656000 | 1787164384329000 | 在 manifest 记录区间内 |
| `laser_temperature_controller.host_time_us` | 1787164375710000 | 1787164384360000 | 在 manifest 记录区间内 |
| `system_temperature_controller.host_time_us` | 1787164375710000 | 1787164384360000 | 在 manifest 记录区间内 |
| `waveform_features.host_time_us` | 1787164375605000 | 1787164384267000 | 首行早于 manifest start 30,000 us，见 A-1 |
| `waveform_peaks.host_time_us` | 1787164375656000 | 1787164383633000 | 在 manifest 记录区间内，并与 waveform RAW 时间相同 |
| `waveform.dat.host_timestamp_us` | 1787164375656000 | 1787164383633000 | 在 manifest 记录区间内 |
| `event_log.timestamp_us` | 1787164375653380 | 1787164384376825 | 生命周期日志；stop 日志可略晚于 manifest end 写入时刻 |

不同采样率没有被要求逐行一一对应；本次 waveform peaks 与 waveform RAW 逐帧一一对应，其他 CSV 仅做时间范围和单位一致性检查。

### C.11 问题分类（本轮只记录，不修复）

#### A. 当前代码真实数据一致性问题

**A-1：waveform feature CSV 有一条记录早于 session start。**

- 证据：`session.json.start_time_us=1787164375635000`，而 `sensors/waveform_features.csv` 首行 `host_time_us=1787164375605000`，早 30,000 us；其余该 CSV 行和 RAW 均在记录区间内。
- 定位：`src/sky/core/SkyRuntime.cpp:397-399` 在 runtime 启动时就启动 feature timer；`src/sky/core/SkyRuntime.cpp:843-866` 记录缓存的 `latestWaveformFeature()`，只按 `host_time_us` 去重，没有与本次 recorder 的 `recording_start_time_us` 比较。
- 原因：Start Recording 后发送的第一条 feature 可能是 Start 前已经计算并缓存的 feature，最终落入新 session。
- 推荐方向（待确认后实施）：在 recording start 时清除/标记缓存 feature，或在 recorder sink 丢弃 `host_time_us < recording_start_time_us` 的 feature；应补一个边界回归测试。

本次没有发现 `end_time_us < start_time_us`、负 `elapsed_ms`、RAW 截断、sequence 断裂、payload 越界、waveform harmonic 点数与 manifest 不一致或 peak_index 越界。

#### B. 文档/测试缺项

- 正式 v1.0 文本未完整定义 `capture.*`、`waveform_file_count`、`counts.*` 的字符串计数类型、`raw_files.*` 的 records 类型和七个零记录文件的合法 20-byte header 语义。
- 正式文本未完整定义 `event_log.csv` 四列、UTC 格式、level 枚举和 CSV escaping。
- `error_log.txt` 的自由文本性质、空文件合法性、`[timestamp] message` 行布局未被正式机器接口章节明确标注。
- `device_config.json` 的 Sky “默认/不可用配置”与运行时 SkyConfig snapshot 边界未定义；本轮 focused tests 也没有六类非空协议 RAW 的最终 session 端到端 fixture。
- `waveform_peaks.csv` 的 `search_start_index=0,search_end_index=0` sentinel 及“有效 end 展开为 harmonic 点数”的语义未定义。

#### C. 仅需解释、代码当前按既有语义工作的字段

- `device_config.waveform.points_per_frame=0` 是初始化时配置值；`session.waveform_points_per_frame=50000` 是 Stop 时最近一条合法 harmonic frame 的实际点数。二者不是同一概念，不能仅因不同就判 bug。
- Sky `device_config.sensors.*.enabled` 是 `configured` 的 JSON 镜像；Sky recorder 当前没有接收 SkyConfig，因此六组 `configured/enabled/port/baud/rate_hz/slave_address` 为默认 false/null。结构化模拟数据存在不改变这个 writer 语义。
- `waveform_peaks.point_count` 取 harmonic 搜索输入数组点数；本次为 50000，与 RAW harmonic 一致。`0,0` 表示使用完整数组的默认搜索范围，而非实际空区间。
- 零 records 的 RAW DAT 文件仍是合法 session 成员，必须保留 file header；不能把“没有硬件原始帧”伪造成一条 payload。
- CSV optional numeric/string 值以空字段落盘，bool 以 `true/false` 落盘；`error_log.txt` 是诊断文本而不是结构化接口。

### C.12 是否可以冻结 v1.0

**当前不建议冻结完整 v1.0 数据格式。** RAW DAT 外层、waveform 子 payload、时间字段的 manifest 算术、CSV 表头和跨 Ground/Sky 的实际 waveform 点数规则已经有较强证据；但至少 A-1 的录制边界问题仍是真实 session 数据一致性问题，且 B 类字段/日志/零记录和 Sky 配置快照语义尚未正式定义。待确认 A-1 的修复方向、决定 Sky `device_config` 是否需要真实配置快照，并补齐六类协议 RAW 的端到端验证或明确 capability boundary 后，再冻结完整规范更稳妥。
