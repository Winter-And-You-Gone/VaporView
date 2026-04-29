import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                title: ApplicationWindow.window.t("waveform.rawData")
                WaveformCanvas { anchors.fill: parent; anchors.margins: 8; samples: waveformBackend.rawSamples; lineColor: ApplicationWindow.window.waveformRaw }
            }
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                title: ApplicationWindow.window.t("waveform.secondHarmonic")
                WaveformCanvas { anchors.fill: parent; anchors.margins: 8; samples: waveformBackend.harmonicSamples; lineColor: ApplicationWindow.window.waveformHarmonic }
            }
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: ApplicationWindow.window.t("waveform.peakTrend")
                WaveformCanvas {
                    anchors.fill: parent
                    anchors.margins: 8
                    samples: waveformBackend.peakSamples
                    scatter: waveformBackend.scatterMode
                    lineColor: ApplicationWindow.window.primary
                    yMin: 1.0
                    yMax: 1.4
                    xSamplePeriod: 1
                }
            }
        }

        Card {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            title: ApplicationWindow.window.t("waveform.controlPanel")

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10

                Text { text: waveformBackend.statusText; color: ApplicationWindow.window.muted; font.pixelSize: 11 }
                TextField {
                    Layout.fillWidth: true
                    text: waveformBackend.host
                    placeholderText: "TCP Host"
                    enabled: !waveformBackend.connected
                    onEditingFinished: waveformBackend.host = text
                }
                TextField {
                    Layout.fillWidth: true
                    text: String(waveformBackend.port)
                    placeholderText: "Port"
                    enabled: !waveformBackend.connected
                    validator: IntValidator { bottom: 1; top: 65535 }
                    onEditingFinished: waveformBackend.port = Number(text)
                }
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: waveformBackend.connected ? "unlink" : "link"
                    text: waveformBackend.connected ? ApplicationWindow.window.t("topbar.disconnect") : ApplicationWindow.window.t("devices.connect")
                    variant: waveformBackend.connected ? "danger" : "primary"
                    onClicked: waveformBackend.toggleConnection()
                }

                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: ApplicationWindow.window.border }

                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: ApplicationWindow.window.t("home.frameRate"); color: ApplicationWindow.window.muted; font.pixelSize: 11 }
                    Text { text: Number(waveformBackend.frameRate).toFixed(1) + " Hz"; color: ApplicationWindow.window.text; font.family: "Consolas"; font.pixelSize: 11 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: ApplicationWindow.window.t("waveform.peakValue"); color: ApplicationWindow.window.muted; font.pixelSize: 11 }
                    Text { text: Number(waveformBackend.latestPeak).toFixed(4) + " V"; color: ApplicationWindow.window.text; font.family: "Consolas"; font.pixelSize: 11 }
                }
                CheckBox {
                    text: ApplicationWindow.window.t("waveform.filterSwitch")
                    checked: waveformBackend.filterEnabled
                    onToggled: waveformBackend.filterEnabled = checked
                }
                CheckBox {
                    text: "Scatter trend"
                    checked: waveformBackend.scatterMode
                    onToggled: waveformBackend.scatterMode = checked
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField { id: minPeak; Layout.fillWidth: true; placeholderText: "Min"; text: "-1000" }
                    TextField { id: maxPeak; Layout.fillWidth: true; placeholderText: "Max"; text: "1000" }
                }
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "activity"
                    text: "Apply Peak Filter"
                    onClicked: waveformBackend.configurePeakFilter(Number(minPeak.text), Number(maxPeak.text), waveformBackend.filterEnabled)
                }
                ToolbarButton { Layout.fillWidth: true; iconName: "trash-2"; text: "Clear Peak"; onClicked: waveformBackend.clearPeakHistory() }
                Item { Layout.fillHeight: true }
                Text {
                    Layout.fillWidth: true
                    text: ApplicationWindow.window.t("waveform.recordFreq") + ": " + recordingBackend.waveformExportRateHz + " Hz"
                    color: ApplicationWindow.window.muted
                    font.pixelSize: 10
                }
            }
        }
    }
}
