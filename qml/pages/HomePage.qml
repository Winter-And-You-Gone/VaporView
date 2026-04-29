import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    id: page

    function numberText(value, decimals) {
        var n = Number(value)
        return isNaN(n) ? "--" : n.toFixed(decimals)
    }

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
                   (english ? "Range: +/-1.2V" : "信号范围: ±1.2V") + "    " +
                   (english ? "Timestamp: " : "当前时间戳: ") + (deviceBackend.coordinateData.localTime || "--")
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

    function joinedLogLines() {
        var lines = deviceBackend.logLines
        return lines && lines.length > 0 ? lines.join("\n") : ""
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 12
        clip: true

        Column {
            width: Math.max(page.width - 24, 980)
            spacing: 12

            Rectangle {
                width: parent.width
                height: 58
                radius: 8
                color: ApplicationWindow.window.card
                border.color: ApplicationWindow.window.border
                clip: true

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 12
                    spacing: 12

                    Text {
                        text: ApplicationWindow.window.t("home.deviceStatus")
                        color: ApplicationWindow.window.text
                        font.pixelSize: 13 * ApplicationWindow.window.scaleFactor
                        font.weight: Font.Bold
                        verticalAlignment: Text.AlignVCenter
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        contentWidth: statusRow.implicitWidth
                        contentHeight: height
                        boundsBehavior: Flickable.StopAtBounds
                        clip: true

                        Row {
                            id: statusRow
                            height: parent.height
                            spacing: 10

                            Repeater {
                                model: deviceBackend.devices
                                delegate: Row {
                                    height: statusRow.height
                                    spacing: 6

                                    readonly property bool deviceHealthy: connected && online

                                    StatusPill {
                                        anchors.verticalCenter: parent.verticalCenter
                                        status: deviceHealthy ? "online" : online ? "warning" : "offline"
                                        label: ApplicationWindow.window.t(nameKey)
                                    }

                                    Button {
                                        id: linkButton
                                        property bool active: connected
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 30
                                        height: 30
                                        text: ""
                                        padding: 0
                                        enabled: kind === "tcp" || !deviceBackend.busy
                                        onClicked: {
                                            if (kind === "tcp") {
                                                waveformBackend.toggleConnection()
                                            } else if (connected) {
                                                deviceBackend.disconnectDevices()
                                            } else {
                                                deviceBackend.connectDevices()
                                            }
                                        }

                                        contentItem: LucideIcon {
                                            anchors.centerIn: parent
                                            width: 16
                                            height: 16
                                            name: linkButton.active ? "unlink" : "link"
                                            iconColor: linkButton.active ? ApplicationWindow.window.ok : ApplicationWindow.window.muted
                                            stroke: 2
                                        }

                                        background: Rectangle {
                                            radius: 8
                                            color: linkButton.active
                                                   ? "#1A22C55E"
                                                   : (ApplicationWindow.window.dark
                                                      ? Qt.rgba(0.58, 0.64, 0.72, 0.10)
                                                      : Qt.rgba(0.39, 0.45, 0.55, 0.10))
                                            border.color: linkButton.active
                                                          ? "#3322C55E"
                                                          : (ApplicationWindow.window.dark
                                                             ? Qt.rgba(0.58, 0.64, 0.72, 0.30)
                                                             : Qt.rgba(0.39, 0.45, 0.55, 0.30))
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.preferredWidth: 1
                        Layout.preferredHeight: 30
                        color: ApplicationWindow.window.border
                    }

                    ToolbarButton {
                        iconName: deviceBackend.autoDetectInProgress ? "square" : "zap"
                        text: (deviceBackend.autoDetectInProgress ? ApplicationWindow.window.t("topbar.cancel") : ApplicationWindow.window.t("home.autoDetect"))
                        variant: "secondary"
                        onClicked: deviceBackend.autoDetectPortsOrCancel()
                    }
                }
            }

            GridLayout {
                width: parent.width
                columns: 3
                columnSpacing: 12
                rowSpacing: 12

                Card {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 2
                    Layout.preferredHeight: 190
                    title: ApplicationWindow.window.t("home.coordinateTitle")
                    GridLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        columns: 5
                        columnSpacing: 0
                        rowSpacing: 0

                        Repeater {
                            model: [
                                { l: "home.latitude", v: page.numberText(deviceBackend.coordinateData.latitude, 4), u: "°" },
                                { l: "home.longitude", v: page.numberText(deviceBackend.coordinateData.longitude, 4), u: "°" },
                                { l: "home.altitude", v: page.numberText(deviceBackend.coordinateData.altitude, 2), u: "m" },
                                { l: "home.velocity", v: page.numberText(deviceBackend.coordinateData.velocity, 1), u: "m/s" },
                                { l: "home.satellites", v: String(deviceBackend.coordinateData.satellites || 0), u: "" }
                            ]
                            delegate: MetricTile {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                label: ApplicationWindow.window.t(modelData.l)
                                value: modelData.v
                                unit: modelData.u
                            }
                        }

                        MetricTile {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.columnSpan: 2
                            label: ApplicationWindow.window.t("home.gnssTime")
                            value: deviceBackend.coordinateData.timestamp || "---"
                        }

                        MetricTile {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.columnSpan: 2
                            label: ApplicationWindow.window.t("home.localTime")
                            value: deviceBackend.coordinateData.localTime || "---"
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 16
                                anchors.rightMargin: 16
                                anchors.topMargin: 10
                                anchors.bottomMargin: 10
                                spacing: 6

                                Text {
                                    Layout.fillWidth: true
                                    text: ApplicationWindow.window.t("home.rtkStatus")
                                    color: ApplicationWindow.window.muted
                                    opacity: 0.9
                                    font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }

                                StatusPill {
                                    status: String(deviceBackend.coordinateData.rtkStatus || "").toLowerCase().indexOf("fix") >= 0 ? "online" : "warning"
                                    label: deviceBackend.coordinateData.rtkStatus || "---"
                                }
                            }
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 190
                    title: ApplicationWindow.window.t("home.envTitle")
                    GridLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        columns: 2
                        Repeater {
                            model: [
                                { l: "home.temperature", v: page.numberText(deviceBackend.environmentData.temperature, 1), u: "°C" },
                                { l: "home.humidity", v: page.numberText(deviceBackend.environmentData.humidity, 1), u: "%" },
                                { l: "home.pressure", v: page.numberText(deviceBackend.environmentData.pressure / 10.0, 1), u: "kPa" },
                                { l: "home.laserRange", v: page.numberText(deviceBackend.environmentData.laserRange, 1), u: "m" }
                            ]
                            delegate: MetricTile {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                label: ApplicationWindow.window.t(modelData.l)
                                value: modelData.v
                                unit: modelData.u
                            }
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 190
                    title: ApplicationWindow.window.t("home.recTitle")
                    GridLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        columns: 2
                        MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: ApplicationWindow.window.t("home.recFrames"); value: Number(recordingBackend.sensorRows).toLocaleString(Qt.locale(), "f", 0) }
                        MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: ApplicationWindow.window.t("home.sysUptime"); value: recordingBackend.systemUptimeText }
                        MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: ApplicationWindow.window.t("home.recDuration"); value: recordingBackend.durationText }
                        MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: ApplicationWindow.window.t("home.recSize"); value: recordingBackend.fileSizeText }
                    }
                }
            }

            GridLayout {
                width: parent.width
                height: 180
                columns: 2
                columnSpacing: 12
                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
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
                    Layout.fillHeight: true
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
            }

            GridLayout {
                width: parent.width
                height: 180
                columns: 2
                columnSpacing: 12

                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: ApplicationWindow.window.t("waveform.peakTrend")
                    headerRight: Row {
                        spacing: 8
                        ToolbarButton {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 76
                            height: 26
                            iconName: "trash-2"
                            iconSize: 13
                            text: ApplicationWindow.window.t("home.clearLog")
                            variant: "secondary"
                            ToolTip.text: ApplicationWindow.window.t("home.clearLog")
                            ToolTip.visible: hovered
                            ToolTip.delay: 400
                            onClicked: waveformBackend.clearPeakHistory()
                        }
                        ToolbarButton {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 72
                            height: 26
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
                        lineColor: ApplicationWindow.window.text
                        yMin: 1.0
                        yMax: 1.4
                        autoScaleY: waveformBackend.peakSamples.length > 1
                        plotBackground: ApplicationWindow.window.chartPlot
                        gridColor: ApplicationWindow.window.chartGrid
                        axisColor: ApplicationWindow.window.chartAxis
                        emptyColor: ApplicationWindow.window.muted
                        uiScale: ApplicationWindow.window.scaleFactor
                        tailWindow: true
                        maxVisualSamples: 200
                        sourcePointCount: Math.max(1, Math.min(200, waveformBackend.peakTotalCount))
                        xStartIndex: 0
                        xEndIndex: Math.max(0, Math.min(199, waveformBackend.peakTotalCount - 1))
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: ApplicationWindow.window.t("home.systemLog")
                    headerRight: ToolbarButton {
                        iconName: "trash-2"
                        iconSize: 13
                        text: ApplicationWindow.window.t("home.clearLog")
                        variant: "secondary"
                        onClicked: deviceBackend.clearLog()
                    }

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 8
                        clip: true
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        TextArea {
                            id: homeLogText
                            text: page.joinedLogLines()
                            readOnly: true
                            selectByMouse: true
                            selectByKeyboard: true
                            wrapMode: TextEdit.Wrap
                            color: ApplicationWindow.window.text
                            selectedTextColor: ApplicationWindow.window.primaryForeground
                            selectionColor: ApplicationWindow.window.primary
                            font.family: "Consolas"
                            font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                            padding: 0
                            leftPadding: 0
                            rightPadding: 0
                            topPadding: 0
                            bottomPadding: 0
                            background: Rectangle { color: "transparent" }
                        }
                    }

                    Text {
                        visible: deviceBackend.logLines.length === 0
                        anchors.centerIn: parent
                        text: ApplicationWindow.window.t("home.noLog")
                        color: ApplicationWindow.window.muted
                        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
                    }
                }
            }
        }
    }
}
