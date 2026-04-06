import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI 1.0

FluScrollablePage {
    title: appController.english ? "Serial Devices" : "串口设备"
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

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: deviceLayout.implicitHeight + 40

        ColumnLayout {
            id: deviceLayout
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            FluText {
                text: appController.english ? "Device Wiring Matrix" : "设备连线矩阵"
                font: FluTextStyle.Title
            }

            FluText {
                text: appController.english
                      ? "The page now uses FluentUI controls for port selection, baudrate setup and connection actions."
                      : "这一页已经切到 FluentUI 组件，用于选择串口、波特率与连接动作。"
                color: "#64748b"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 960 ? 2 : 1
                columnSpacing: 12
                rowSpacing: 12

                FluFrame {
                    Layout.fillWidth: true
                    Layout.preferredHeight: gnssLayout.implicitHeight + 32

                    ColumnLayout {
                        id: gnssLayout
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        FluText {
                            text: "GNSS"
                            font: FluTextStyle.BodyStrong
                        }

                        FluText { text: appController.english ? "Port" : "端口"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.portOptions
                            currentIndex: portIndex(appController.portOptions, appController.gnssPort)
                            onActivated: appController.gnssPort = currentText
                            onCommit: function(text) { appController.gnssPort = text }
                            disabled: !appController.canEditPorts
                        }

                        FluText { text: appController.english ? "Baud" : "波特率"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.baudOptions
                            currentIndex: listIndex(appController.baudOptions, appController.gnssBaud)
                            onActivated: appController.gnssBaud = currentText
                            onCommit: function(text) { appController.gnssBaud = text }
                            disabled: !appController.canEditPorts
                        }

                        FluText { text: appController.english ? "Rate" : "频率"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.rateOptions
                            currentIndex: listIndex(appController.rateOptions, appController.gnssRate)
                            onActivated: appController.gnssRate = currentText
                            onCommit: function(text) { appController.gnssRate = text }
                            disabled: !appController.canEditPorts
                        }
                    }
                }

                FluFrame {
                    Layout.fillWidth: true
                    Layout.preferredHeight: imuLayout.implicitHeight + 32

                    ColumnLayout {
                        id: imuLayout
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        FluText {
                            text: "IMU"
                            font: FluTextStyle.BodyStrong
                        }

                        FluText { text: appController.english ? "Port" : "端口"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.portOptions
                            currentIndex: portIndex(appController.portOptions, appController.imuPort)
                            onActivated: appController.imuPort = currentText
                            onCommit: function(text) { appController.imuPort = text }
                            disabled: !appController.canEditPorts
                        }

                        FluText { text: appController.english ? "Baud" : "波特率"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.baudOptions
                            currentIndex: listIndex(appController.baudOptions, appController.imuBaud)
                            onActivated: appController.imuBaud = currentText
                            onCommit: function(text) { appController.imuBaud = text }
                            disabled: !appController.canEditPorts
                        }

                        FluText { text: appController.english ? "Rate" : "频率"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.rateOptions
                            currentIndex: listIndex(appController.rateOptions, appController.imuRate)
                            onActivated: appController.imuRate = currentText
                            onCommit: function(text) { appController.imuRate = text }
                            disabled: !appController.canEditPorts
                        }
                    }
                }

                FluFrame {
                    Layout.fillWidth: true
                    Layout.preferredHeight: ptbLayout.implicitHeight + 32

                    ColumnLayout {
                        id: ptbLayout
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        FluText {
                            text: "PTB210"
                            font: FluTextStyle.BodyStrong
                        }

                        FluText { text: appController.english ? "Port" : "端口"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.portOptions
                            currentIndex: portIndex(appController.portOptions, appController.ptbPort)
                            onActivated: appController.ptbPort = currentText
                            onCommit: function(text) { appController.ptbPort = text }
                            disabled: !appController.canEditPorts
                        }

                        FluText { text: appController.english ? "Baud" : "波特率"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.baudOptions
                            currentIndex: listIndex(appController.baudOptions, appController.ptbBaud)
                            onActivated: appController.ptbBaud = currentText
                            onCommit: function(text) { appController.ptbBaud = text }
                            disabled: !appController.canEditPorts
                        }

                        FluText { text: appController.english ? "Rate" : "频率"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.rateOptions
                            currentIndex: listIndex(appController.rateOptions, appController.ptbRate)
                            onActivated: appController.ptbRate = currentText
                            onCommit: function(text) { appController.ptbRate = text }
                            disabled: !appController.canEditPorts
                        }
                    }
                }

                FluFrame {
                    Layout.fillWidth: true
                    Layout.preferredHeight: hmpLayout.implicitHeight + 32

                    ColumnLayout {
                        id: hmpLayout
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        FluText {
                            text: "HMP3"
                            font: FluTextStyle.BodyStrong
                        }

                        FluText { text: appController.english ? "Port" : "端口"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.portOptions
                            currentIndex: portIndex(appController.portOptions, appController.hmpPort)
                            onActivated: appController.hmpPort = currentText
                            onCommit: function(text) { appController.hmpPort = text }
                            disabled: !appController.canEditPorts
                        }

                        FluText { text: appController.english ? "Baud" : "波特率"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.baudOptions
                            currentIndex: listIndex(appController.baudOptions, appController.hmpBaud)
                            onActivated: appController.hmpBaud = currentText
                            onCommit: function(text) { appController.hmpBaud = text }
                            disabled: !appController.canEditPorts
                        }

                        FluText { text: appController.english ? "Rate" : "频率"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.rateOptions
                            currentIndex: listIndex(appController.rateOptions, appController.hmpRate)
                            onActivated: appController.hmpRate = currentText
                            onCommit: function(text) { appController.hmpRate = text }
                            disabled: !appController.canEditPorts
                        }
                    }
                }

                FluFrame {
                    Layout.fillWidth: true
                    Layout.preferredHeight: lidarLayout.implicitHeight + 32

                    ColumnLayout {
                        id: lidarLayout
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 10

                        FluText {
                            text: "TF03"
                            font: FluTextStyle.BodyStrong
                        }

                        FluText { text: appController.english ? "Port" : "端口"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.portOptions
                            currentIndex: portIndex(appController.portOptions, appController.lidarPort)
                            onActivated: appController.lidarPort = currentText
                            onCommit: function(text) { appController.lidarPort = text }
                            disabled: !appController.canEditPorts
                        }

                        FluText { text: appController.english ? "Baud" : "波特率"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
                            editable: true
                            model: appController.baudOptions
                            currentIndex: listIndex(appController.baudOptions, appController.lidarBaud)
                            onActivated: appController.lidarBaud = currentText
                            onCommit: function(text) { appController.lidarBaud = text }
                            disabled: !appController.canEditPorts
                        }

                        FluText { text: appController.english ? "Rate" : "频率"; color: "#64748b" }
                        FluComboBox {
                            Layout.fillWidth: true
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

            Flow {
                Layout.fillWidth: true
                spacing: 10

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

                FluFilledButton {
                    text: appController.english ? "Connect" : "连接"
                    disabled: !appController.canConnect
                    onClicked: appController.connectDevices()
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
            }
        }
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: notesLayout.implicitHeight + 40

        ColumnLayout {
            id: notesLayout
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10

            FluText {
                text: appController.english ? "Migration Notes" : "迁移说明"
                font: FluTextStyle.BodyStrong
            }

            FluText {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#64748b"
                text: appController.english
                      ? "Device selection, RTK controls and session analysis now live in the Fluent shell. Remaining migration work is focused on recording and waveform capture."
                      : "设备选择、RTK 控制和会话分析都已经进入 Fluent 主壳，剩余迁移重点会放在录制流程与波形采集。"
            }
        }
    }
}
