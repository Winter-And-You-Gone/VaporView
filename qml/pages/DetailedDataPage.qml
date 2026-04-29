import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    ScrollView {
        anchors.fill: parent
        anchors.margins: 12
        Column {
            width: Math.max(parent.width - 24, 900)
            spacing: 12

            Card {
                width: parent.width
                height: 250
                title: ApplicationWindow.window.t("detailed.gnssGroup")
                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    columns: 6
                    Repeater {
                        model: deviceBackend.detailedData.nav || []
                        delegate: MetricTile {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            label: modelData.label
                            value: modelData.value
                            unit: modelData.unit || ""
                        }
                    }
                }
            }

            Card {
                width: parent.width
                height: 130
                title: ApplicationWindow.window.t("detailed.envGroup")
                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    columns: 6
                    Repeater {
                        model: deviceBackend.detailedData.env || []
                        delegate: MetricTile {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            label: modelData.label
                            value: modelData.value
                            unit: modelData.unit || ""
                        }
                    }
                }
            }

            Card {
                width: parent.width
                height: 150
                title: ApplicationWindow.window.t("detailed.sysGroup")
                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    columns: 6
                    MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: "Connection"; value: deviceBackend.statusText }
                    MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: "Serial Ports"; value: String(deviceBackend.systemData.ports) }
                    MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: "Recording"; value: recordingBackend.status }
                    MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: "Rows"; value: String(recordingBackend.sensorRows) }
                    MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: "Frames"; value: String(recordingBackend.waveformFrames) }
                    MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: "Size"; value: recordingBackend.fileSizeText }
                }
            }
        }
    }
}
