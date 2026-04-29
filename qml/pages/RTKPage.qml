import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: ApplicationWindow.window.t("rtk.ntripConfig")
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                TextField { Layout.fillWidth: true; placeholderText: ApplicationWindow.window.t("rtk.casterAddress"); text: rtkBackend.server; onEditingFinished: rtkBackend.server = text }
                TextField { Layout.fillWidth: true; placeholderText: ApplicationWindow.window.t("rtk.port"); text: rtkBackend.port; onEditingFinished: rtkBackend.port = text }
                TextField { Layout.fillWidth: true; placeholderText: ApplicationWindow.window.t("rtk.mountPoint"); text: rtkBackend.mountpoint; onEditingFinished: rtkBackend.mountpoint = text }
                RowLayout {
                    Layout.fillWidth: true
                    TextField { Layout.fillWidth: true; placeholderText: ApplicationWindow.window.t("rtk.username"); text: rtkBackend.username; onEditingFinished: rtkBackend.username = text }
                    TextField { Layout.fillWidth: true; placeholderText: ApplicationWindow.window.t("rtk.password"); echoMode: TextInput.Password; text: rtkBackend.password; onEditingFinished: rtkBackend.password = text }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField { Layout.fillWidth: true; placeholderText: "Output Port"; text: rtkBackend.outputPort; onEditingFinished: rtkBackend.outputPort = text }
                    TextField { Layout.preferredWidth: 110; placeholderText: "Baud"; text: String(rtkBackend.outputBaud); onEditingFinished: rtkBackend.outputBaud = Number(text) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField { id: leverX; Layout.fillWidth: true; placeholderText: "Lever X m" }
                    TextField { id: leverY; Layout.fillWidth: true; placeholderText: "Y" }
                    TextField { id: leverZ; Layout.fillWidth: true; placeholderText: "Z" }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ToolbarButton { text: rtkBackend.running ? "Stop" : "Start"; variant: rtkBackend.running ? "danger" : "primary"; onClicked: rtkBackend.running ? rtkBackend.stop() : rtkBackend.start() }
                    ToolbarButton { text: ApplicationWindow.window.t("rtk.testConnection"); onClicked: rtkBackend.testConnection() }
                    ToolbarButton { text: ApplicationWindow.window.t("rtk.saveConfig"); onClicked: rtkBackend.saveConfig() }
                }
                ToolbarButton { text: "Apply Lever Arm"; onClicked: rtkBackend.applyMainAntennaLeverArm(Number(leverX.text), Number(leverY.text), Number(leverZ.text)) }
                Item { Layout.fillHeight: true }
            }
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: ApplicationWindow.window.t("rtk.diagnostics")
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8
                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    MetricTile { Layout.fillWidth: true; Layout.preferredHeight: 66; label: ApplicationWindow.window.t("rtk.rtcmThroughput"); value: String(rtkBackend.stats.inputBps || 0); unit: "B/s" }
                    MetricTile { Layout.fillWidth: true; Layout.preferredHeight: 66; label: "Output"; value: String(rtkBackend.stats.outputBps || 0); unit: "B/s" }
                    MetricTile { Layout.fillWidth: true; Layout.preferredHeight: 66; label: "RTCM3"; value: String(rtkBackend.stats.rtcm3FrameCount || 0) }
                    MetricTile { Layout.fillWidth: true; Layout.preferredHeight: 66; label: ApplicationWindow.window.t("rtk.diffStatus"); value: rtkBackend.running ? ApplicationWindow.window.t("rtk.connected") : "---" }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Column {
                        width: parent.width
                        Repeater {
                            model: rtkBackend.diagnostics
                            delegate: Text {
                                width: parent.width
                                text: modelData
                                color: ApplicationWindow.window.muted
                                font.family: "Consolas"
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
