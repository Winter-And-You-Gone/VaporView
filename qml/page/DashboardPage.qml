import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI 1.0

FluScrollablePage {
    title: appController.english ? "Overview" : "总览"
    padding: 12
    spacing: 12

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: overviewLayout.implicitHeight + 48

        GridLayout {
            id: overviewLayout
            anchors.fill: parent
            anchors.margins: 24
            columns: width > 920 ? 2 : 1
            columnSpacing: 20
            rowSpacing: 20

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                FluText {
                    text: appController.english ? "Mission Overview" : "任务总览"
                    font: FluTextStyle.Title
                }

                FluText {
                    text: appController.statusText
                    font: FluTextStyle.BodyStrong
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
                                      ? "Ports: " + appController.gnssPort + ", " + appController.imuPort
                                      : "端口: " + appController.gnssPort + ", " + appController.imuPort
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
