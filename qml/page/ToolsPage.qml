import QtQuick
import QtQuick.Layouts
import FluentUI 1.0

FluScrollablePage {
    title: appController.english ? "Tools" : "工具"
    padding: 12
    spacing: 12

    FluFrame {
        Layout.fillWidth: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            FluText {
                text: appController.english ? "Fluent Workbench" : "Fluent 工作台"
                font: FluTextStyle.Title
            }

            FluText {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#64748b"
                text: appController.english
                      ? "The remaining tools now live behind Fluent pages. This area can be used for project notes, future utilities and migration checkpoints."
                      : "剩余工具现在已经进入 Fluent 页面，这里保留给项目说明、后续实用工具和迁移检查点。"
            }

            RowLayout {
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

        ColumnLayout {
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

            FluText {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#64748b"
                text: appController.english
                      ? "Navigation now includes dedicated RTK and session pages. Remaining work is focused on deeper workflow parity such as recording and waveform acquisition."
                      : "导航里已经有独立的 RTK 和会话页面，后续重点会放在更深层的录制流程与波形采集能力补齐。"
            }
        }
    }
}
