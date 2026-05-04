import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    id: page

    width: parent ? parent.width : 0
    height: parent ? parent.height : 0

    readonly property int rightScrollPadding: 16

    function peakFilterModeText(mode) {
        switch (mode) {
            case 0: return "无"
            case 1: return "IQR 离群值"
            case 2: return "保留范围"
            case 3: return "排除范围"
            default: return "未知"
        }
    }

    function pickFields(list, keys) {
        if (!list) return []
        return list.filter(function(f) { return keys.indexOf(f.key) >= 0 })
    }

    property var tcpFields: [
        { key: "connected", label: "连接", value: waveformBackend.connected ? "已连接" : "未连接", unit: "" },
        { key: "status", label: "状态", value: waveformBackend.statusText, unit: "" },
        { key: "frameRate", label: "帧率", value: waveformBackend.frameRate.toFixed(1), unit: "Hz" },
        { key: "rawSampleCount", label: "原始采样", value: String(waveformBackend.rawSampleCount), unit: "" },
        { key: "harmonicSampleCount", label: "谐波采样", value: String(waveformBackend.harmonicSampleCount), unit: "" },
        { key: "peakTotalCount", label: "峰值总数", value: String(waveformBackend.peakTotalCount), unit: "" },
        { key: "latestPeak", label: "最新峰值", value: waveformBackend.latestPeak.toFixed(3), unit: "" },
        { key: "filterEnabled", label: "过滤", value: waveformBackend.filterEnabled ? "开" : "关", unit: "" },
        { key: "peakFilterMode", label: "模式", value: peakFilterModeText(waveformBackend.peakFilterMode), unit: "" },
        { key: "harmonicFilteredView", label: "谐波过滤", value: waveformBackend.harmonicFilteredView ? "开" : "关", unit: "" },
        { key: "scatterMode", label: "散点模式", value: waveformBackend.scatterMode ? "开" : "关", unit: "" },
    ]

    property var sysFields: [
        { key: "status", label: "连接状态", value: deviceBackend.statusText, unit: "" },
        { key: "ports", label: "串口数", value: String(deviceBackend.ports.length), unit: "" },
        { key: "recording", label: "记录状态", value: recordingBackend.status, unit: "" },
        { key: "sensorRows", label: "传感器行数", value: String(recordingBackend.sensorRows), unit: "" },
        { key: "waveformFrames", label: "波形帧数", value: String(recordingBackend.waveformFrames), unit: "" },
        { key: "fileSize", label: "文件大小", value: recordingBackend.fileSizeText, unit: "" },
        { key: "recordUsage", label: "磁盘使用", value: recordingBackend.recordUsageText, unit: "" },
        { key: "diskRemaining", label: "磁盘剩余", value: recordingBackend.diskRemainingText, unit: "" },
        { key: "diskTotal", label: "磁盘总量", value: recordingBackend.diskTotalText, unit: "" },
        { key: "duration", label: "时长", value: recordingBackend.durationText, unit: "" },
        { key: "uptime", label: "系统运行", value: recordingBackend.systemUptimeText, unit: "" },
        { key: "exportRate", label: "导出速率", value: recordingBackend.exportRateHz + " Hz", unit: "" },
        { key: "waveformRate", label: "波形速率", value: recordingBackend.waveformExportRateHz + " Hz", unit: "" },
    ]

    // ── 紧凑字段卡片 ──
    component MetricCell: Rectangle {
        id: metricCell
        property string labelText: ""
        property string fieldValue: "---"
        property string fieldUnit: ""
        property string keyName: ""

        radius: 6
        color: ApplicationWindow.window.cardHeader
        border.color: ApplicationWindow.window.border
        border.width: 1
        implicitHeight: 54
        height: 54

        Column {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 2

            Row {
                width: parent.width
                spacing: 4
                Text {
                    text: metricCell.labelText
                    color: ApplicationWindow.window.muted
                    font.pixelSize: 10
                    elide: Text.ElideRight
                    width: parent.width - keyText.width - 4
                }
                Text {
                    id: keyText
                    text: metricCell.keyName
                    color: ApplicationWindow.window.muted
                    font.pixelSize: 8
                    font.family: "Consolas"
                    visible: metricCell.keyName.length > 0
                }
            }

            Row {
                width: parent.width
                spacing: 4
                Text {
                    text: metricCell.fieldValue
                    color: ApplicationWindow.window.text
                    font.pixelSize: 14
                    font.bold: true
                    font.family: "Consolas"
                    elide: Text.ElideRight
                    width: parent.width - unitText.width - 4
                }
                Text {
                    id: unitText
                    text: metricCell.fieldUnit
                    color: ApplicationWindow.window.muted
                    font.pixelSize: 10
                    visible: metricCell.fieldUnit.length > 0
                }
            }
        }
    }

    // ── 自适应流式字段网格 ──
    component CompactFieldGrid: Item {
        id: compactGrid
        property var fields: []
        property int minCellWidth: 180
        property int columnGap: 8
        property int rowGap: 8
        property int columns: 0

        width: parent ? parent.width : 900

        property int computedColumns: columns > 0 ? columns : Math.max(1, Math.floor((width + columnGap) / (minCellWidth + columnGap)))
        property real computedCellWidth: columns > 0
            ? ((width - (computedColumns - 1) * columnGap) / computedColumns)
            : Math.max(minCellWidth, (width - (computedColumns - 1) * columnGap) / computedColumns)

        height: flow.childrenRect.height
        implicitHeight: flow.childrenRect.height

        Flow {
            id: flow
            width: compactGrid.width
            spacing: compactGrid.columnGap

            Repeater {
                model: compactGrid.fields || []

                delegate: Item {
                    width: compactGrid.computedCellWidth
                    height: 54 + compactGrid.rowGap

                    MetricCell {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 54

                        labelText: modelData.label
                        fieldValue: modelData.value
                        fieldUnit: modelData.unit
                        keyName: modelData.key
                    }
                }
            }
        }

    }

    // ── 通用卡片流式容器 ──
    component CardFlow: Item {
        id: cardFlow
        property int minCardWidth: 420
        property int gap: 12
        property int itemCount: 1

        width: parent ? parent.width : 900

        property int maxColumnsByWidth: Math.max(
            1,
            Math.floor((width + gap) / (minCardWidth + gap))
        )

        property int computedColumns: Math.max(
            1,
            Math.min(Math.max(1, itemCount), maxColumnsByWidth)
        )

        property real computedCardWidth:
            (width - (computedColumns - 1) * gap) / computedColumns

        height: flow.childrenRect.height
        implicitHeight: flow.childrenRect.height

        default property alias content: flow.data

        Flow {
            id: flow
            width: cardFlow.width
            spacing: cardFlow.gap
        }
    }

    // ── 整行文本字段 ──
    component FieldRow: Rectangle {
        id: fieldRow
        property string labelText: ""
        property string fieldValue: "---"
        property string fieldUnit: ""
        property string keyName: ""

        width: parent ? parent.width : 900
        height: 28
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

    Flickable {
        id: detailFlick
        x: 12
        y: 12
        width: page.width - 24
        height: page.height - 24
        clip: true

        boundsBehavior: Flickable.StopAtBounds
        contentWidth: detailFlick.width
        contentHeight: detailColumn.height

        ScrollBar.vertical: ScrollBar {
            id: detailVerticalScrollBar
            policy: ScrollBar.AsNeeded
        }

        Column {
            id: detailColumn
            width: detailFlick.width - page.rightScrollPadding
            height: implicitHeight
            spacing: 12

            // ── EPSILON 1: 定位与姿态 ──
            Card {
                width: parent.width
                height: implicitHeight
                title: "EPSILON — 定位与姿态"

                Column {
                    width: parent.width

                    CompactFieldGrid {
                        fields: pickFields(deviceBackend.allDeviceFields.epsilon || [], [
                            "latitude_deg","longitude_deg","height_m",
                            "roll_deg","pitch_deg","yaw_deg",
                            "gnss_satellites","gnss_fix_code","heading_valid",
                            "utc_unix_s","utc_microseconds","device_timestamp_us"
                        ])
                    }

                    FieldRow { labelText: "GNSS 状态"; fieldValue: pickFields(deviceBackend.allDeviceFields.epsilon || [], ["gnss_fix_text"])[0]?.value ?? "---"; keyName: "gnss_fix_text" }
                    FieldRow { labelText: "错误信息"; fieldValue: pickFields(deviceBackend.allDeviceFields.epsilon || [], ["error_message"])[0]?.value ?? "---"; keyName: "error_message" }
                }
            }

            // ── EPSILON 2 + 3: 坐标与速度 | IMU 与磁场 ──
            CardFlow {
                id: epsilonMidFlow
                width: parent.width
                // 3 列需 180*3 + 8*2 = 556，加安全余量
                minCardWidth: 580
                itemCount: 2

                Card {
                    width: epsilonMidFlow.computedCardWidth
                    height: implicitHeight
                    title: "EPSILON — 坐标与速度"

                    CompactFieldGrid {
                        columns: 3
                        minCellWidth: 160
                        fields: pickFields(deviceBackend.allDeviceFields.epsilon || [], [
                            "ecef_x_m","ecef_y_m","ecef_z_m",
                            "ned_n_m","ned_e_m","ned_d_m",
                            "vel_n_mps","vel_e_mps","vel_d_mps",
                            "body_vel_x_mps","body_vel_y_mps","body_vel_z_mps"
                        ])
                    }
                }

                Card {
                    width: epsilonMidFlow.computedCardWidth
                    height: implicitHeight
                    title: "EPSILON — IMU 与磁场"

                    CompactFieldGrid {
                        columns: 4
                        minCellWidth: 130
                        fields: pickFields(deviceBackend.allDeviceFields.epsilon || [], [
                            "body_acc_x_mps2","body_acc_y_mps2","body_acc_z_mps2",
                            "imu_acc_x_mps2","imu_acc_y_mps2","imu_acc_z_mps2",
                            "imu_gyr_x_radps","imu_gyr_y_radps","imu_gyr_z_radps",
                            "ang_vel_x_radps","ang_vel_y_radps","ang_vel_z_radps",
                            "mag_x_mg","mag_y_mg","mag_z_mg",
                            "quat_w","quat_x","quat_y","quat_z",
                            "imu_temp_c","pressure_pa","pressure_temp_c","pressure_altitude_m"
                        ])
                    }
                }
            }

            // ── EPSILON 4: 精度 / 状态 / 计数 / 包频率 ──
            Card {
                width: parent.width
                height: implicitHeight
                title: "EPSILON — 精度 / 状态 / 计数 / 包频率"

                Column {
                    width: parent.width

                    CompactFieldGrid {
                        fields: pickFields(deviceBackend.allDeviceFields.epsilon || [], [
                            "hdop","vdop","hacc_m","vacc_m",
                            "lat_std_m","lon_std_m","height_std_m","diff_age_s",
                            "raw_frame_count","dropped_frame_count","last_packet_id","last_serial_number",
                            "imu_packet_rate_hz","ahrs_packet_rate_hz","insgps_packet_rate_hz","sys_state_packet_rate_hz",
                            "raw_gnss_packet_rate_hz","satellite_packet_rate_hz","geodetic_packet_rate_hz","ecef_packet_rate_hz",
                            "valid"
                        ])
                    }

                    FieldRow { labelText: "系统状态位"; fieldValue: pickFields(deviceBackend.allDeviceFields.epsilon || [], ["system_status_bits"])[0]?.value ?? "---"; keyName: "system_status_bits" }
                    FieldRow { labelText: "滤波状态位"; fieldValue: pickFields(deviceBackend.allDeviceFields.epsilon || [], ["filter_status_bits"])[0]?.value ?? "---"; keyName: "filter_status_bits" }
                    FieldRow { labelText: "更新状态位"; fieldValue: pickFields(deviceBackend.allDeviceFields.epsilon || [], ["update_status_bits"])[0]?.value ?? "---"; keyName: "update_status_bits" }
                    FieldRow { labelText: "错误信息"; fieldValue: pickFields(deviceBackend.allDeviceFields.epsilon || [], ["error_message"])[0]?.value ?? "---"; keyName: "error_message" }
                }
            }

            // ── PTB / HMP / LiDAR ──
            CardFlow {
                id: envFlow
                width: parent.width
                minCardWidth: 280
                itemCount: 3

                Card {
                    width: envFlow.computedCardWidth
                    height: implicitHeight
                    title: "PTB210 气压计"

                    CompactFieldGrid {
                        minCellWidth: 160
                        fields: pickFields(deviceBackend.allDeviceFields.ptb || [], ["pressure_hpa","valid","timestamp"])
                    }
                }

                Card {
                    width: envFlow.computedCardWidth
                    height: implicitHeight
                    title: "HMP 温湿度"

                    CompactFieldGrid {
                        minCellWidth: 160
                        fields: pickFields(deviceBackend.allDeviceFields.hmp || [], ["temperature","humidity","valid","timestamp"])
                    }
                }

                Card {
                    width: envFlow.computedCardWidth
                    height: implicitHeight
                    title: "TFA1500-L LiDAR"

                    CompactFieldGrid {
                        minCellWidth: 160
                        fields: pickFields(deviceBackend.allDeviceFields.lidar || [], ["distance_m","signal_strength","valid","timestamp"])
                    }
                }
            }

            // ── TCP 波形源 + 系统状态 ──
            CardFlow {
                id: sysFlow
                width: parent.width
                minCardWidth: 430
                itemCount: 2

                Card {
                    width: sysFlow.computedCardWidth
                    height: implicitHeight
                    title: "TCP 波形源"

                    Column {
                        width: parent.width

                        CompactFieldGrid {
                            minCellWidth: 180
                            fields: tcpFields
                        }

                        FieldRow { labelText: "主机"; fieldValue: waveformBackend.host + ":" + waveformBackend.port; keyName: "host" }
                        FieldRow { labelText: "过滤范围"; fieldValue: waveformBackend.filterMin.toFixed(3) + " ~ " + waveformBackend.filterMax.toFixed(3); keyName: "filterRange" }
                        FieldRow { labelText: "搜索范围"; fieldValue: waveformBackend.peakSearchStartIndex + " ~ " + waveformBackend.peakSearchEndIndex; keyName: "peakSearchRange" }
                    }
                }

                Card {
                    width: sysFlow.computedCardWidth
                    height: implicitHeight
                    title: "系统 / 记录"

                    CompactFieldGrid {
                        minCellWidth: 180
                        fields: sysFields
                    }
                }
            }

            Item { width: 1; height: 36 }
        }
    }
}
