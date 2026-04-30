import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    id: page

    function frameRateText() {
        var hz = Number(waveformBackend.frameRate)
        return hz > 0 ? hz.toFixed(0) + " Hz" : "-- Hz"
    }

    function peakValueText() {
        var peak = Number(waveformBackend.latestPeak)
        return peak > 0 ? peak.toFixed(3) + "V" : "--"
    }

    function waveHeaderText(kind) {
        var english = appBackend.language === "en"
        if (kind === "raw")
            return (english ? "Rate: " : "帧率: ") + frameRateText() + "    " +
                   (english ? "Range: +/-1.2V" : "信号范围: ±1.2V")
        if (kind === "harmonic")
            return (english ? "Rate: " : "帧率: ") + frameRateText() + "    " +
                   (english ? "Peak: " : "峰值: ") + peakValueText()
        return (english ? "Filter: " : "滤波状态: ") +
               (waveformBackend.filterEnabled ? (english ? "Enabled" : "已启用") : (english ? "Disabled" : "未启用")) + "    " +
               (english ? "Latest peak: " : "最新峰值: ") + peakValueText()
    }

    function trendToggleText() {
        var english = appBackend.language === "en"
        return waveformBackend.scatterMode ? (english ? "Line" : "折线") : (english ? "Scatter" : "散点")
    }

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
                headerRight: Text {
                    text: page.waveHeaderText("raw")
                    color: ApplicationWindow.window.muted
                    font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                    font.weight: Font.Medium
                }
                WaveformCanvas {
                    anchors.fill: parent
                    anchors.margins: 8
                    samples: waveformBackend.rawSamples
                    sourcePointCount: waveformBackend.rawSampleCount > 1 ? waveformBackend.rawSampleCount : 201
                    xStartIndex: 0
                    xEndIndex: waveformBackend.rawSampleCount > 1 ? waveformBackend.rawSampleCount - 1 : 200
                    autoScaleY: waveformBackend.rawSampleCount > 1
                    plotBackground: ApplicationWindow.window.chartPlot
                    gridColor: ApplicationWindow.window.chartGrid
                    axisColor: ApplicationWindow.window.chartAxis
                    emptyColor: ApplicationWindow.window.muted
                    uiScale: ApplicationWindow.window.scaleFactor
                    lineColor: ApplicationWindow.window.waveformRaw
                }
            }
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                title: ApplicationWindow.window.t("waveform.secondHarmonic")
                headerRight: Text {
                    text: page.waveHeaderText("harmonic")
                    color: ApplicationWindow.window.muted
                    font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                    font.weight: Font.Medium
                }
                WaveformCanvas {
                    anchors.fill: parent
                    anchors.margins: 8
                    samples: waveformBackend.harmonicSamples
                    sourcePointCount: waveformBackend.harmonicSampleCount > 1 ? waveformBackend.harmonicSampleCount : 201
                    xStartIndex: 0
                    xEndIndex: waveformBackend.harmonicSampleCount > 1 ? waveformBackend.harmonicSampleCount - 1 : 200
                    autoScaleY: waveformBackend.harmonicSampleCount > 1
                    plotBackground: ApplicationWindow.window.chartPlot
                    gridColor: ApplicationWindow.window.chartGrid
                    axisColor: ApplicationWindow.window.chartAxis
                    emptyColor: ApplicationWindow.window.muted
                    uiScale: ApplicationWindow.window.scaleFactor
                    lineColor: ApplicationWindow.window.waveformHarmonic
                }
            }
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: ApplicationWindow.window.t("waveform.peakTrend")
                headerRight: Row {
                    spacing: 8
                    ToolbarButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "trash-2"
                        iconSize: 13
                        text: appBackend.language === "en" ? "Clear" : "清空"
                        variant: "secondary"
                        ToolTip.text: appBackend.language === "en" ? "Clear" : "清空"
                        ToolTip.visible: hovered
                        ToolTip.delay: 400
                        onClicked: waveformBackend.clearPeakHistory()
                    }
                    ToolbarButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "activity"
                        iconSize: 13
                        text: page.trendToggleText()
                        variant: "secondary"
                        onClicked: waveformBackend.scatterMode = !waveformBackend.scatterMode
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.waveHeaderText("peak")
                        color: ApplicationWindow.window.muted
                        font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                        font.weight: Font.Medium
                    }
                }
                WaveformCanvas {
                    anchors.fill: parent
                    anchors.margins: 8
                    samples: waveformBackend.peakSamples
                    scatter: waveformBackend.scatterMode
                    fillUnderLine: !waveformBackend.scatterMode
                    hardLineCorners: true
                    lineColor: ApplicationWindow.window.waveformRaw
                    yMin: 1.0
                    yMax: 1.4
                    autoScaleY: waveformBackend.peakSamples.length > 1
                    plotBackground: ApplicationWindow.window.chartPlot
                    gridColor: ApplicationWindow.window.chartGrid
                    axisColor: ApplicationWindow.window.chartAxis
                    emptyColor: ApplicationWindow.window.muted
                    uiScale: ApplicationWindow.window.scaleFactor
                    tailWindow: true
                    maxVisualSamples: 1000
                    sourcePointCount: Math.max(1, Math.min(1000, waveformBackend.peakTotalCount))
                    xStartIndex: 0
                    xEndIndex: Math.max(0, Math.min(999, waveformBackend.peakTotalCount - 1))
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
