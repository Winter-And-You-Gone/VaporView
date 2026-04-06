import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI 1.0
import VaporViewApp

FluScrollablePage {
    title: appController.english ? "Mission Overview" : "任务总览"
    padding: 12
    spacing: 12

    function fmt(value, digits) {
        return Number(value).toFixed(digits)
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: heroLayout.implicitHeight + 48

        GridLayout {
            id: heroLayout
            anchors.fill: parent
            anchors.margins: 24
            columns: width > 920 ? 2 : 1
            columnSpacing: 20
            rowSpacing: 20

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                FluText {
                    text: appController.english ? "Fluent Mission Console" : "Fluent 任务控制台"
                    font: FluTextStyle.Title
                }

                FluText {
                    text: appController.english
                          ? "Widgets are replaced by a QML shell, while serial and device logic stay in C++."
                          : "主界面已切到 QML，串口与设备业务逻辑仍由 C++ 承担。"
                    color: "#64748b"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 10

                    FluFilledButton {
                        text: appController.english ? "Connect Devices" : "连接设备"
                        disabled: !appController.canConnect
                        onClicked: appController.connectDevices()
                    }

                    FluButton {
                        text: appController.english ? "Auto Detect" : "自动识别"
                        disabled: !appController.canEditPorts
                        onClicked: appController.autoDetectPorts()
                    }

                    FluButton {
                        text: appController.english ? "Disconnect" : "断开连接"
                        disabled: !appController.canDisconnect
                        onClicked: appController.disconnectDevices()
                    }

                    FluButton {
                        text: appController.english ? "Cancel" : "取消连接"
                        disabled: !appController.canCancelConnect
                        onClicked: appController.cancelConnect()
                    }
                }
            }

            FluFrame {
                Layout.fillWidth: true
                Layout.preferredWidth: 240
                Layout.preferredHeight: statusCardLayout.implicitHeight + 36

                ColumnLayout {
                    id: statusCardLayout
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10

                    FluText {
                        text: appController.english ? "Connection State" : "连接状态"
                        font: FluTextStyle.BodyStrong
                    }

                    RowLayout {
                        spacing: 12

                        FluProgressRing {
                            indeterminate: appController.connectionAttemptInProgress || appController.portDetectionInProgress
                            value: appController.connected ? 1 : 0
                            Layout.preferredWidth: 40
                            Layout.preferredHeight: 40
                        }

                        ColumnLayout {
                            FluText {
                                text: appController.statusText
                                font: FluTextStyle.BodyStrong
                            }

                            FluText {
                                text: appController.english
                                      ? "Realtime serial acquisition shell"
                                      : "实时串口采集外壳"
                                color: "#64748b"
                            }
                        }
                    }
                }
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: width > 1320 ? 3 : width > 780 ? 2 : 1
        columnSpacing: 12
        rowSpacing: 12

        SensorCard {
            title: "GNSS"
            subtitle: appController.english ? "Positioning solution" : "定位解"
            accentColor: "#2563eb"
            active: appController.gnssData.valid
            rateText: fmt(appController.gnssData.rate || 0, 1) + " Hz"
            fields: [
                { label: appController.english ? "Status" : "状态", value: String(appController.gnssData.status || "-") },
                { label: appController.english ? "Latitude" : "纬度", value: fmt(appController.gnssData.latitude || 0, 6) },
                { label: appController.english ? "Longitude" : "经度", value: fmt(appController.gnssData.longitude || 0, 6) },
                { label: appController.english ? "Altitude" : "高度", value: fmt(appController.gnssData.altitude || 0, 2) + " m" },
                { label: appController.english ? "Heading" : "航向", value: fmt(appController.gnssData.heading || 0, 2) + " deg" },
                { label: appController.english ? "Satellites" : "卫星数", value: String(appController.gnssData.satellites || 0) }
            ]
        }

        SensorCard {
            title: "IMU"
            subtitle: appController.english ? "Attitude and inertial data" : "姿态与惯导"
            accentColor: "#0f766e"
            active: appController.imuData.valid
            rateText: fmt(appController.imuData.rate || 0, 1) + " Hz"
            fields: [
                { label: appController.english ? "Source" : "来源", value: String(appController.imuData.source || "-") },
                { label: "Roll", value: fmt(appController.imuData.roll || 0, 2) + " deg" },
                { label: "Pitch", value: fmt(appController.imuData.pitch || 0, 2) + " deg" },
                { label: "Yaw", value: fmt(appController.imuData.yaw || 0, 2) + " deg" },
                { label: appController.english ? "Temp" : "温度", value: fmt(appController.imuData.temperature || 0, 2) + " C" },
                { label: appController.english ? "Pressure" : "气压", value: fmt(appController.imuData.pressure || 0, 2) + " hPa" }
            ]
        }

        SensorCard {
            title: "PTB210"
            subtitle: appController.english ? "Barometric channel" : "气压通道"
            accentColor: "#9333ea"
            active: appController.ptbData.valid
            rateText: fmt(appController.ptbData.rate || 0, 1) + " Hz"
            fields: [
                { label: appController.english ? "Status" : "状态", value: String(appController.ptbData.status || "-") },
                { label: appController.english ? "Pressure" : "气压", value: fmt(appController.ptbData.pressure || 0, 2) + " hPa" }
            ]
        }

        SensorCard {
            title: "HMP3"
            subtitle: appController.english ? "Humidity and temperature" : "温湿度"
            accentColor: "#ea580c"
            active: appController.hmpData.valid
            rateText: fmt(appController.hmpData.rate || 0, 1) + " Hz"
            fields: [
                { label: appController.english ? "Status" : "状态", value: String(appController.hmpData.status || "-") },
                { label: appController.english ? "Temperature" : "温度", value: fmt(appController.hmpData.temperature || 0, 2) + " C" },
                { label: appController.english ? "Humidity" : "湿度", value: fmt(appController.hmpData.humidity || 0, 2) + " %" }
            ]
        }

        SensorCard {
            title: "TF03"
            subtitle: appController.english ? "Range finding" : "测距"
            accentColor: "#dc2626"
            active: appController.lidarData.valid
            rateText: fmt(appController.lidarData.rate || 0, 1) + " Hz"
            fields: [
                { label: appController.english ? "Status" : "状态", value: String(appController.lidarData.status || "-") },
                { label: appController.english ? "Distance" : "距离", value: fmt(appController.lidarData.distance || 0, 3) + " m" },
                { label: appController.english ? "Strength" : "强度", value: String(appController.lidarData.strength || 0) }
            ]
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
