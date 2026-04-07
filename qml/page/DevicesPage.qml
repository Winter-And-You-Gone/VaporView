import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI 1.0
import VaporViewApp

FluScrollablePage {
    id: page
    title: appController.english ? "Overview & Devices" : "总览与设备"
    padding: 12
    spacing: 12

    function portIndex(options, value) {
        const idx = options.indexOf(value)
        return idx >= 0 ? idx : 0
    }

    function listIndex(options, value) {
        const idx = options.indexOf(value)
        return idx >= 0 ? idx : 0
    }

    function sensorText(data, key) {
        const value = data ? data[key] : undefined
        if (value === undefined || value === null || value === "") {
            return "-"
        }
        return String(value)
    }

    function sensorNumber(data, key, digits, suffix) {
        const value = data ? data[key] : undefined
        if (value === undefined || value === null) {
            return "-"
        }
        if (!data.valid && Number(value) === 0) {
            return "-"
        }
        return Number(value).toFixed(digits) + (suffix || "")
    }

    function sensorInteger(data, key, suffix) {
        const value = data ? data[key] : undefined
        if (value === undefined || value === null) {
            return "-"
        }
        if (!data.valid && Number(value) === 0) {
            return "-"
        }
        return String(Math.round(Number(value))) + (suffix || "")
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: overviewLayout.implicitHeight + 44

        GridLayout {
            id: overviewLayout
            anchors.fill: parent
            anchors.margins: 22
            columns: 1
            columnSpacing: 18
            rowSpacing: 18

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10

                Flow {
                    Layout.fillWidth: true
                    spacing: 10

                    FluFilledButton {
                        text: appController.english ? "Connect" : "连接"
                        disabled: !appController.canConnect
                        onClicked: appController.connectDevices()
                    }

                    FluButton {
                        text: appController.english ? "Refresh Ports" : "刷新端口"
                        disabled: !appController.canEditPorts
                        onClicked: appController.refreshPorts()
                    }

                    FluButton {
                        text: appController.english ? "Auto Detect" : "自动识别"
                        disabled: !appController.canEditPorts
                        onClicked: appController.autoDetectPorts()
                    }

                    FluButton {
                        text: appController.english ? "Cancel" : "取消"
                        disabled: !appController.canCancelConnect
                        onClicked: appController.cancelConnect()
                    }

                    FluButton {
                        text: appController.english ? "Disconnect" : "断开"
                        disabled: !appController.canDisconnect
                        onClicked: appController.disconnectDevices()
                    }

                    FluFrame {
                        width: statusRow.implicitWidth + 28
                        height: statusRow.implicitHeight + 20

                        RowLayout {
                            id: statusRow
                            anchors.centerIn: parent
                            spacing: 12

                            FluText {
                                text: appController.english ? "Connection State" : "连接状态"
                                font: FluTextStyle.BodyStrong
                            }

                            FluProgressRing {
                                indeterminate: appController.connectionAttemptInProgress || appController.portDetectionInProgress
                                value: appController.connected ? 1 : 0
                                Layout.preferredWidth: 30
                                Layout.preferredHeight: 30
                            }

                            FluText {
                                text: appController.statusText
                                font: FluTextStyle.BodyStrong
                            }

                            FluText {
                                text: appController.english
                                      ? "GNSS " + sensorText(appController.gnssData, "status") + " | IMU " + sensorText(appController.imuData, "status")
                                      : "GNSS " + sensorText(appController.gnssData, "status") + " | IMU " + sensorText(appController.imuData, "status")
                                color: "#64748b"
                            }
                        }
                    }
                }
            }
        }
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: configLayout.implicitHeight + 40

        ColumnLayout {
            id: configLayout
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            GridLayout {
                Layout.fillWidth: true
                columns: 6
                columnSpacing: 10
                rowSpacing: 10

                FluText {
                    text: ""
                    Layout.preferredWidth: 96
                }

                FluText { text: "GNSS"; font: FluTextStyle.BodyStrong; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                FluText { text: "IMU"; font: FluTextStyle.BodyStrong; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                FluText { text: "PTB210"; font: FluTextStyle.BodyStrong; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                FluText { text: "HMP3"; font: FluTextStyle.BodyStrong; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                FluText { text: "TF03"; font: FluTextStyle.BodyStrong; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }

                FluText {
                    text: appController.english ? "Port" : "端口"
                    color: "#64748b"
                    font: FluTextStyle.BodyStrong
                    Layout.preferredWidth: 96
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.portOptions
                    currentIndex: portIndex(appController.portOptions, appController.gnssPort)
                    onActivated: appController.gnssPort = currentText
                    onCommit: function(text) { appController.gnssPort = text }
                    disabled: !appController.canEditPorts
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.portOptions
                    currentIndex: portIndex(appController.portOptions, appController.imuPort)
                    onActivated: appController.imuPort = currentText
                    onCommit: function(text) { appController.imuPort = text }
                    disabled: !appController.canEditPorts
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.portOptions
                    currentIndex: portIndex(appController.portOptions, appController.ptbPort)
                    onActivated: appController.ptbPort = currentText
                    onCommit: function(text) { appController.ptbPort = text }
                    disabled: !appController.canEditPorts
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.portOptions
                    currentIndex: portIndex(appController.portOptions, appController.hmpPort)
                    onActivated: appController.hmpPort = currentText
                    onCommit: function(text) { appController.hmpPort = text }
                    disabled: !appController.canEditPorts
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.portOptions
                    currentIndex: portIndex(appController.portOptions, appController.lidarPort)
                    onActivated: appController.lidarPort = currentText
                    onCommit: function(text) { appController.lidarPort = text }
                    disabled: !appController.canEditPorts
                }

                FluText {
                    text: appController.english ? "Baud" : "波特率"
                    color: "#64748b"
                    font: FluTextStyle.BodyStrong
                    Layout.preferredWidth: 96
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.baudOptions
                    currentIndex: listIndex(appController.baudOptions, appController.gnssBaud)
                    onActivated: appController.gnssBaud = currentText
                    onCommit: function(text) { appController.gnssBaud = text }
                    disabled: !appController.canEditPorts
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.baudOptions
                    currentIndex: listIndex(appController.baudOptions, appController.imuBaud)
                    onActivated: appController.imuBaud = currentText
                    onCommit: function(text) { appController.imuBaud = text }
                    disabled: !appController.canEditPorts
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.baudOptions
                    currentIndex: listIndex(appController.baudOptions, appController.ptbBaud)
                    onActivated: appController.ptbBaud = currentText
                    onCommit: function(text) { appController.ptbBaud = text }
                    disabled: !appController.canEditPorts
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.baudOptions
                    currentIndex: listIndex(appController.baudOptions, appController.hmpBaud)
                    onActivated: appController.hmpBaud = currentText
                    onCommit: function(text) { appController.hmpBaud = text }
                    disabled: !appController.canEditPorts
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.baudOptions
                    currentIndex: listIndex(appController.baudOptions, appController.lidarBaud)
                    onActivated: appController.lidarBaud = currentText
                    onCommit: function(text) { appController.lidarBaud = text }
                    disabled: !appController.canEditPorts
                }

                FluText {
                    text: appController.english ? "Rate" : "频率"
                    color: "#64748b"
                    font: FluTextStyle.BodyStrong
                    Layout.preferredWidth: 96
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.rateOptions
                    currentIndex: listIndex(appController.rateOptions, appController.gnssRate)
                    onActivated: appController.gnssRate = currentText
                    onCommit: function(text) { appController.gnssRate = text }
                    disabled: !appController.canEditPorts
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.rateOptions
                    currentIndex: listIndex(appController.rateOptions, appController.imuRate)
                    onActivated: appController.imuRate = currentText
                    onCommit: function(text) { appController.imuRate = text }
                    disabled: !appController.canEditPorts
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.rateOptions
                    currentIndex: listIndex(appController.rateOptions, appController.ptbRate)
                    onActivated: appController.ptbRate = currentText
                    onCommit: function(text) { appController.ptbRate = text }
                    disabled: !appController.canEditPorts
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.rateOptions
                    currentIndex: listIndex(appController.rateOptions, appController.hmpRate)
                    onActivated: appController.hmpRate = currentText
                    onCommit: function(text) { appController.hmpRate = text }
                    disabled: !appController.canEditPorts
                }

                FluComboBox {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 148
                    editable: true
                    model: appController.rateOptions
                    currentIndex: listIndex(appController.rateOptions, appController.lidarRate)
                    onActivated: appController.lidarRate = currentText
                    onCommit: function(text) { appController.lidarRate = text }
                    disabled: !appController.canEditPorts
                }
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: width > 980 ? 2 : 1
        columnSpacing: 12
        rowSpacing: 12

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: 12

            SensorCard {
                title: "GNSS"
                subtitle: appController.english ? "Positioning and heading" : "定位与定向"
                accentColor: "#2563eb"
                active: appController.gnssData.valid
                rateText: sensorNumber(appController.gnssData, "rate", 1, " Hz")
                fieldColumns: page.width > 980 ? 3 : 2
                labelWidth: 76
                fields: [
                    { label: appController.english ? "Status" : "状态", value: sensorText(appController.gnssData, "status") },
                    { label: appController.english ? "Latitude" : "纬度", value: sensorNumber(appController.gnssData, "latitude", 8, " deg") },
                    { label: appController.english ? "Longitude" : "经度", value: sensorNumber(appController.gnssData, "longitude", 8, " deg") },
                    { label: appController.english ? "Altitude" : "高度", value: sensorNumber(appController.gnssData, "altitude", 3, " m") },
                    { label: appController.english ? "Vel North" : "北向速度", value: sensorNumber(appController.gnssData, "velNorth", 3, " m/s") },
                    { label: appController.english ? "Vel East" : "东向速度", value: sensorNumber(appController.gnssData, "velEast", 3, " m/s") },
                    { label: appController.english ? "Vel Down" : "下向速度", value: sensorNumber(appController.gnssData, "velDown", 3, " m/s") },
                    { label: appController.english ? "Ground Speed" : "地速", value: sensorNumber(appController.gnssData, "groundSpeed", 3, " m/s") },
                    { label: appController.english ? "Heading" : "航向", value: sensorNumber(appController.gnssData, "heading", 2, " deg") },
                    { label: appController.english ? "Pitch" : "俯仰", value: sensorNumber(appController.gnssData, "headingPitch", 2, " deg") },
                    { label: appController.english ? "Baseline" : "基线长", value: sensorNumber(appController.gnssData, "headingLength", 3, " m") },
                    { label: appController.english ? "Heading Type" : "定向类型", value: sensorText(appController.gnssData, "headingType") },
                    { label: appController.english ? "Heading Used" : "定向解卫星", value: sensorInteger(appController.gnssData, "headingSolutionSatellites", "") },
                    { label: appController.english ? "Heading Tracked" : "定向跟踪卫星", value: sensorInteger(appController.gnssData, "headingTrackedSatellites", "") },
                    { label: appController.english ? "Heading GGL1" : "定向 GGL1", value: sensorInteger(appController.gnssData, "headingGgl1", "") },
                    { label: appController.english ? "Heading GGL1L2" : "定向 GGL1L2", value: sensorInteger(appController.gnssData, "headingGgl1L2", "") },
                    { label: appController.english ? "Sat Used" : "定位卫星", value: sensorInteger(appController.gnssData, "satellitesUsed", "") },
                    { label: appController.english ? "Sat Tracked" : "跟踪卫星", value: sensorInteger(appController.gnssData, "satellitesTracked", "") },
                    { label: "Sigma Lat", value: sensorNumber(appController.gnssData, "sigmaLat", 3, " m") },
                    { label: "Sigma Lon", value: sensorNumber(appController.gnssData, "sigmaLon", 3, " m") },
                    { label: "Sigma Alt", value: sensorNumber(appController.gnssData, "sigmaAlt", 3, " m") },
                    { label: "GDOP", value: sensorNumber(appController.gnssData, "gdop", 2, "") },
                    { label: "PDOP", value: sensorNumber(appController.gnssData, "pdop", 2, "") },
                    { label: "HDOP", value: sensorNumber(appController.gnssData, "hdop", 2, "") },
                    { label: "HTDOP", value: sensorNumber(appController.gnssData, "htdop", 2, "") },
                    { label: "TDOP", value: sensorNumber(appController.gnssData, "tdop", 2, "") },
                    { label: appController.english ? "Diff Age" : "差分时延", value: sensorNumber(appController.gnssData, "diffAge", 2, " s") },
                    { label: appController.english ? "Undulation" : "大地水准面", value: sensorNumber(appController.gnssData, "undulation", 3, " m") },
                    { label: appController.english ? "Elevation Cutoff" : "高度角截止", value: sensorNumber(appController.gnssData, "elevationCutoff", 2, " deg") }
                ]
            }

            SensorCard {
                title: "IMU"
                subtitle: appController.english ? "Attitude and inertial data" : "姿态与惯导"
                accentColor: "#0f766e"
                active: appController.imuData.valid
                rateText: sensorNumber(appController.imuData, "rate", 1, " Hz")
                fieldColumns: page.width > 980 ? 3 : 2
                labelWidth: 76
                fields: [
                    { label: appController.english ? "Status" : "状态", value: sensorText(appController.imuData, "status") },
                    { label: appController.english ? "Source" : "来源", value: sensorText(appController.imuData, "source") },
                    { label: "Roll", value: sensorNumber(appController.imuData, "roll", 2, " deg") },
                    { label: "Pitch", value: sensorNumber(appController.imuData, "pitch", 2, " deg") },
                    { label: "Yaw", value: sensorNumber(appController.imuData, "yaw", 2, " deg") },
                    { label: "Acc X", value: sensorNumber(appController.imuData, "accX", 3, " m/s^2") },
                    { label: "Acc Y", value: sensorNumber(appController.imuData, "accY", 3, " m/s^2") },
                    { label: "Acc Z", value: sensorNumber(appController.imuData, "accZ", 3, " m/s^2") },
                    { label: "Gyro X", value: sensorNumber(appController.imuData, "gyroX", 3, " deg/s") },
                    { label: "Gyro Y", value: sensorNumber(appController.imuData, "gyroY", 3, " deg/s") },
                    { label: "Gyro Z", value: sensorNumber(appController.imuData, "gyroZ", 3, " deg/s") },
                    { label: "Quat W", value: sensorNumber(appController.imuData, "quatW", 4, "") },
                    { label: "Quat X", value: sensorNumber(appController.imuData, "quatX", 4, "") },
                    { label: "Quat Y", value: sensorNumber(appController.imuData, "quatY", 4, "") },
                    { label: "Quat Z", value: sensorNumber(appController.imuData, "quatZ", 4, "") },
                    { label: appController.english ? "Temperature" : "温度", value: sensorNumber(appController.imuData, "temperature", 2, " C") },
                    { label: appController.english ? "Pressure" : "气压", value: sensorNumber(appController.imuData, "pressure", 2, " hPa") },
                    { label: appController.english ? "System Time Us" : "系统时间 Us", value: sensorText(appController.imuData, "systemTimeUs") },
                    { label: appController.english ? "System Time Ms" : "系统时间 Ms", value: sensorText(appController.imuData, "systemTimeMs") }
                ]
            }
        }

        ColumnLayout {
            Layout.fillWidth: width <= 980
            Layout.preferredWidth: 320
            Layout.maximumWidth: 360
            Layout.alignment: Qt.AlignTop
            spacing: 12

            SensorCard {
                title: "PTB210"
                subtitle: appController.english ? "Barometric channel" : "气压通道"
                accentColor: "#9333ea"
                active: appController.ptbData.valid
                rateText: sensorNumber(appController.ptbData, "rate", 1, " Hz")
                labelWidth: 92
                fields: [
                    { label: appController.english ? "Status" : "状态", value: sensorText(appController.ptbData, "status") },
                    { label: appController.english ? "Pressure" : "气压", value: sensorNumber(appController.ptbData, "pressure", 2, " hPa") }
                ]
            }

            SensorCard {
                title: "HMP3"
                subtitle: appController.english ? "Humidity and temperature" : "温湿度"
                accentColor: "#ea580c"
                active: appController.hmpData.valid
                rateText: sensorNumber(appController.hmpData, "rate", 1, " Hz")
                labelWidth: 92
                fields: [
                    { label: appController.english ? "Status" : "状态", value: sensorText(appController.hmpData, "status") },
                    { label: appController.english ? "Temperature" : "温度", value: sensorNumber(appController.hmpData, "temperature", 2, " C") },
                    { label: appController.english ? "Humidity" : "湿度", value: sensorNumber(appController.hmpData, "humidity", 2, " %") }
                ]
            }

            SensorCard {
                title: "TF03"
                subtitle: appController.english ? "Range finding" : "测距"
                accentColor: "#dc2626"
                active: appController.lidarData.valid
                rateText: sensorNumber(appController.lidarData, "rate", 1, " Hz")
                labelWidth: 92
                fields: [
                    { label: appController.english ? "Status" : "状态", value: sensorText(appController.lidarData, "status") },
                    { label: appController.english ? "Distance" : "距离", value: sensorNumber(appController.lidarData, "distance", 3, " m") },
                    { label: appController.english ? "Strength" : "强度", value: sensorInteger(appController.lidarData, "strength", "") }
                ]
            }
        }
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: 320

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 12

            FluText {
                text: appController.english ? "Live Event Log" : "实时事件日志"
                font: FluTextStyle.BodyStrong
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 8
                color: "#0f172a"

                ListView {
                    anchors.fill: parent
                    anchors.margins: 14
                    clip: true
                    spacing: 6
                    model: appController.logModel

                    delegate: FluText {
                        width: ListView.view.width
                        text: typeof modelData === "undefined" ? "" : String(modelData)
                        color: "#dbeafe"
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }
}
