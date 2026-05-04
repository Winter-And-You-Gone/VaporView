import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    function peakFilterModeText(mode) {
        switch (mode) {
            case 0: return "无"
            case 1: return "IQR 离群值"
            case 2: return "保留范围"
            case 3: return "排除范围"
            default: return "未知"
        }
    }

    component FieldRow: Rectangle {
        id: fieldRow
        property string labelText: ""
        property string fieldValue: "---"
        property string fieldUnit: ""
        property string keyName: ""

        width: parent ? parent.width : 900
        height: 28
        implicitHeight: 28
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 6

            Text {
                text: fieldRow.keyName
                color: ApplicationWindow.window.muted
                font.pixelSize: 8
                font.family: "Consolas"
                visible: fieldRow.keyName.length > 0
                Layout.preferredWidth: 70
                Layout.minimumWidth: 70
                Layout.maximumWidth: 70
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                text: fieldRow.labelText
                color: ApplicationWindow.window.text
                font.pixelSize: 12
                Layout.preferredWidth: 130
                Layout.minimumWidth: 130
                Layout.maximumWidth: 130
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                text: fieldRow.fieldValue
                color: ApplicationWindow.window.text
                font.pixelSize: 12
                font.weight: Font.Bold
                font.family: "Consolas"
                Layout.fillWidth: true
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                text: fieldRow.fieldUnit
                color: ApplicationWindow.window.muted
                font.pixelSize: 10
                Layout.preferredWidth: 60
                Layout.minimumWidth: 60
                Layout.maximumWidth: 60
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    component SectionHeader: Rectangle {
        property string sectionTitle: ""

        width: parent ? parent.width : 900
        height: 24
        implicitHeight: 24
        color: ApplicationWindow.window.cardHeader

        Text {
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: sectionTitle
            color: ApplicationWindow.window.text
            font.pixelSize: 10
            font.bold: true
            opacity: 0.8
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 12
        Column {
            width: Math.max(parent.width - 24, 900)
            spacing: 12

            Card {
                width: parent.width
                height: 32 + 12 * 24 + (deviceBackend.allDeviceFields.epsilon || []).length * 28 + 20
                title: "EPSILON " + ApplicationWindow.window.t("detailed.gnssGroup")

                Column {
                    width: parent.width

                    SectionHeader { sectionTitle: "坐标" }
                    Repeater {
                        model: (deviceBackend.allDeviceFields.epsilon || []).filter(function(f) {
                            return ["latitude_deg","longitude_deg","height_m","ecef_x_m","ecef_y_m","ecef_z_m","ned_n_m","ned_e_m","ned_d_m"].indexOf(f.key) >= 0;
                        })
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }

                    SectionHeader { sectionTitle: "速度" }
                    Repeater {
                        model: (deviceBackend.allDeviceFields.epsilon || []).filter(function(f) {
                            return ["vel_n_mps","vel_e_mps","vel_d_mps","body_vel_x_mps","body_vel_y_mps","body_vel_z_mps"].indexOf(f.key) >= 0;
                        })
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }

                    SectionHeader { sectionTitle: "加速度" }
                    Repeater {
                        model: (deviceBackend.allDeviceFields.epsilon || []).filter(function(f) {
                            return ["body_acc_x_mps2","body_acc_y_mps2","body_acc_z_mps2","imu_acc_x_mps2","imu_acc_y_mps2","imu_acc_z_mps2"].indexOf(f.key) >= 0;
                        })
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }

                    SectionHeader { sectionTitle: "角速度" }
                    Repeater {
                        model: (deviceBackend.allDeviceFields.epsilon || []).filter(function(f) {
                            return ["imu_gyr_x_radps","imu_gyr_y_radps","imu_gyr_z_radps","ang_vel_x_radps","ang_vel_y_radps","ang_vel_z_radps"].indexOf(f.key) >= 0;
                        })
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }

                    SectionHeader { sectionTitle: "磁场" }
                    Repeater {
                        model: (deviceBackend.allDeviceFields.epsilon || []).filter(function(f) {
                            return ["mag_x_mg","mag_y_mg","mag_z_mg"].indexOf(f.key) >= 0;
                        })
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }

                    SectionHeader { sectionTitle: "姿态" }
                    Repeater {
                        model: (deviceBackend.allDeviceFields.epsilon || []).filter(function(f) {
                            return ["roll_deg","pitch_deg","yaw_deg","quat_w","quat_x","quat_y","quat_z"].indexOf(f.key) >= 0;
                        })
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }

                    SectionHeader { sectionTitle: "温压" }
                    Repeater {
                        model: (deviceBackend.allDeviceFields.epsilon || []).filter(function(f) {
                            return ["imu_temp_c","pressure_pa","pressure_temp_c","pressure_altitude_m"].indexOf(f.key) >= 0;
                        })
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }

                    SectionHeader { sectionTitle: "精度" }
                    Repeater {
                        model: (deviceBackend.allDeviceFields.epsilon || []).filter(function(f) {
                            return ["hdop","vdop","hacc_m","vacc_m","lat_std_m","lon_std_m","height_std_m","diff_age_s"].indexOf(f.key) >= 0;
                        })
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }

                    SectionHeader { sectionTitle: "时间" }
                    Repeater {
                        model: (deviceBackend.allDeviceFields.epsilon || []).filter(function(f) {
                            return ["device_timestamp_us","utc_unix_s","utc_microseconds"].indexOf(f.key) >= 0;
                        })
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }

                    SectionHeader { sectionTitle: "状态" }
                    Repeater {
                        model: (deviceBackend.allDeviceFields.epsilon || []).filter(function(f) {
                            return ["system_status_bits","filter_status_bits","update_status_bits","gnss_fix_code","gnss_fix_text","gnss_satellites","heading_valid","valid","error_message"].indexOf(f.key) >= 0;
                        })
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }

                    SectionHeader { sectionTitle: "计数" }
                    Repeater {
                        model: (deviceBackend.allDeviceFields.epsilon || []).filter(function(f) {
                            return ["raw_frame_count","dropped_frame_count","last_packet_id","last_serial_number"].indexOf(f.key) >= 0;
                        })
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }

                    SectionHeader { sectionTitle: "包频率" }
                    Repeater {
                        model: (deviceBackend.allDeviceFields.epsilon || []).filter(function(f) {
                            return ["imu_packet_rate_hz","ahrs_packet_rate_hz","insgps_packet_rate_hz","sys_state_packet_rate_hz","raw_gnss_packet_rate_hz","satellite_packet_rate_hz","geodetic_packet_rate_hz","ecef_packet_rate_hz"].indexOf(f.key) >= 0;
                        })
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }
                }
            }

            Card {
                width: parent.width
                height: 32 + (deviceBackend.allDeviceFields.ptb || []).length * 28 + 20
                title: "PTB210 " + ApplicationWindow.window.t("detailed.envGroup")

                Column {
                    width: parent.width
                    Repeater {
                        model: deviceBackend.allDeviceFields.ptb || []
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }
                }
            }

            Card {
                width: parent.width
                height: 32 + (deviceBackend.allDeviceFields.hmp || []).length * 28 + 20
                title: "HMP 温湿度"

                Column {
                    width: parent.width
                    Repeater {
                        model: deviceBackend.allDeviceFields.hmp || []
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }
                }
            }

            Card {
                width: parent.width
                height: 32 + (deviceBackend.allDeviceFields.lidar || []).length * 28 + 20
                title: "TFA1500-L LiDAR"

                Column {
                    width: parent.width
                    Repeater {
                        model: deviceBackend.allDeviceFields.lidar || []
                        delegate: FieldRow {
                            labelText: modelData.label
                            fieldValue: modelData.value
                            fieldUnit: modelData.unit
                            keyName: modelData.key
                        }
                    }
                }
            }

            Card {
                width: parent.width
                height: 32 + 14 * 28 + 20
                title: "TCP 波形源"

                Column {
                    width: parent.width
                    FieldRow { labelText: "连接"; fieldValue: waveformBackend.connected ? "已连接" : "未连接"; keyName: "connected" }
                    FieldRow { labelText: "主机"; fieldValue: waveformBackend.host + ":" + waveformBackend.port; keyName: "host" }
                    FieldRow { labelText: "状态"; fieldValue: waveformBackend.statusText; keyName: "status" }
                    FieldRow { labelText: "帧率"; fieldValue: waveformBackend.frameRate.toFixed(1); fieldUnit: "Hz"; keyName: "frameRate" }
                    FieldRow { labelText: "原始采样"; fieldValue: String(waveformBackend.rawSampleCount); keyName: "rawSampleCount" }
                    FieldRow { labelText: "谐波采样"; fieldValue: String(waveformBackend.harmonicSampleCount); keyName: "harmonicSampleCount" }
                    FieldRow { labelText: "峰值总数"; fieldValue: String(waveformBackend.peakTotalCount); keyName: "peakTotalCount" }
                    FieldRow { labelText: "最新峰值"; fieldValue: waveformBackend.latestPeak.toFixed(3); keyName: "latestPeak" }
                    FieldRow { labelText: "过滤"; fieldValue: waveformBackend.filterEnabled ? "开" : "关"; keyName: "filterEnabled" }
                    FieldRow { labelText: "过滤范围"; fieldValue: waveformBackend.filterMin.toFixed(3) + " ~ " + waveformBackend.filterMax.toFixed(3); keyName: "filterRange" }
                    FieldRow { labelText: "模式"; fieldValue: peakFilterModeText(waveformBackend.peakFilterMode); keyName: "peakFilterMode" }
                    FieldRow { labelText: "搜索范围"; fieldValue: waveformBackend.peakSearchStartIndex + " ~ " + waveformBackend.peakSearchEndIndex; keyName: "peakSearchRange" }
                    FieldRow { labelText: "谐波过滤"; fieldValue: waveformBackend.harmonicFilteredView ? "开" : "关"; keyName: "harmonicFilteredView" }
                    FieldRow { labelText: "散点模式"; fieldValue: waveformBackend.scatterMode ? "开" : "关"; keyName: "scatterMode" }
                }
            }

            Card {
                width: parent.width
                height: 32 + 13 * 28 + 20
                title: ApplicationWindow.window.t("detailed.sysGroup")

                Column {
                    width: parent.width
                    FieldRow { labelText: "连接状态"; fieldValue: deviceBackend.statusText; keyName: "status" }
                    FieldRow { labelText: "串口数"; fieldValue: String(deviceBackend.ports.length); keyName: "ports" }
                    FieldRow { labelText: "记录状态"; fieldValue: recordingBackend.status; keyName: "recording" }
                    FieldRow { labelText: "传感器行数"; fieldValue: String(recordingBackend.sensorRows); keyName: "sensorRows" }
                    FieldRow { labelText: "波形帧数"; fieldValue: String(recordingBackend.waveformFrames); keyName: "waveformFrames" }
                    FieldRow { labelText: "文件大小"; fieldValue: recordingBackend.fileSizeText; keyName: "fileSize" }
                    FieldRow { labelText: "磁盘使用"; fieldValue: recordingBackend.recordUsageText; keyName: "recordUsage" }
                    FieldRow { labelText: "磁盘剩余"; fieldValue: recordingBackend.diskRemainingText; keyName: "diskRemaining" }
                    FieldRow { labelText: "磁盘总量"; fieldValue: recordingBackend.diskTotalText; keyName: "diskTotal" }
                    FieldRow { labelText: "时长"; fieldValue: recordingBackend.durationText; keyName: "duration" }
                    FieldRow { labelText: "系统运行"; fieldValue: recordingBackend.systemUptimeText; keyName: "uptime" }
                    FieldRow { labelText: "导出速率"; fieldValue: recordingBackend.exportRateHz + " Hz"; keyName: "exportRate" }
                    FieldRow { labelText: "波形速率"; fieldValue: recordingBackend.waveformExportRateHz + " Hz"; keyName: "waveformRate" }
                }
            }

            Item { width: 1; height: 24 }
        }
    }
}
