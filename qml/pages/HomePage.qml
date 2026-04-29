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
                radius: 6
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

                                    readonly property bool deviceActive: connected || online

                                    StatusPill {
                                        anchors.verticalCenter: parent.verticalCenter
                                        status: deviceActive ? "online" : "warning"
                                        label: ApplicationWindow.window.t(nameKey)
                                    }

                                    Button {
                                        id: linkButton
                                        property bool active: deviceActive
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 30
                                        height: 30
                                        text: "↗"
                                        padding: 0
                                        onClicked: ApplicationWindow.window.currentPage = "devices"

                                        contentItem: Text {
                                            text: linkButton.text
                                            color: linkButton.active ? ApplicationWindow.window.ok : ApplicationWindow.window.warning
                                            font.pixelSize: 14 * ApplicationWindow.window.scaleFactor
                                            font.weight: Font.Bold
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }

                                        background: Rectangle {
                                            radius: 5
                                            color: linkButton.active ? "#1A22C55E" : "#1AF59E0B"
                                            border.color: linkButton.active ? "#3322C55E" : "#33F59E0B"
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
                        text: (deviceBackend.autoDetectInProgress ? ApplicationWindow.window.t("topbar.cancel") : "↯  " + ApplicationWindow.window.t("home.autoDetect"))
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
                                    font.pixelSize: 12 * ApplicationWindow.window.scaleFactor
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
                    WaveformCanvas { anchors.fill: parent; anchors.margins: 8; samples: waveformBackend.rawSamples; lineColor: ApplicationWindow.window.waveformRaw }
                }
                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    title: ApplicationWindow.window.t("waveform.secondHarmonic")
                    WaveformCanvas { anchors.fill: parent; anchors.margins: 8; samples: waveformBackend.harmonicSamples; lineColor: ApplicationWindow.window.waveformHarmonic }
                }
            }

            Card {
                width: parent.width
                height: 180
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
    }
}
