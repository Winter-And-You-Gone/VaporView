# VaporView 实际落盘格式全量清单

本文档记录真实会话目录的逐文件、逐字段审计结果。内容只来自文件字节、JSON、CSV 和 RAW DAT 实际扫描结果，不根据生产代码推测样例中不存在的字段。

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
