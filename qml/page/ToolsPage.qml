import QtQuick
import QtQuick.Layouts
import FluentUI 1.0

FluScrollablePage {
    title: appController.english ? "Tools" : "工具"
    padding: 12
    spacing: 12

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: workbenchLayout.implicitHeight + 40

        ColumnLayout {
            id: workbenchLayout
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            FluText {
                text: appController.english ? "Quick Actions" : "快捷操作"
                font: FluTextStyle.Title
            }

            Flow {
                Layout.fillWidth: true
                spacing: 10

                FluFilledButton {
                    text: appController.english ? "Refresh Ports" : "刷新串口"
                    onClicked: appController.refreshPorts()
                }

                FluButton {
                    text: appController.english ? "Auto Detect Devices" : "自动识别设备"
                    onClicked: appController.autoDetectPorts()
                }
            }
        }
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: stateLayout.implicitHeight + 40

        ColumnLayout {
            id: stateLayout
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10

            FluText {
                text: appController.english ? "Current State" : "当前状态"
                font: FluTextStyle.BodyStrong
            }

            FluText {
                text: appController.statusText
                font: FluTextStyle.TitleLarge
            }
        }
    }
}
