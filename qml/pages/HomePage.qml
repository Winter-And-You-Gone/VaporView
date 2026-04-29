import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    id: page

    ScrollView {
        anchors.fill: parent
        anchors.margins: 12
        clip: true

        Column {
            width: Math.max(page.width - 24, 980)
            spacing: 12

            Card {
                width: parent.width
                height: 58
                title: ApplicationWindow.window.t("home.deviceStatus")
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    Repeater {
                        model: deviceBackend.devices
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30
                            radius: 5
                            color: connected ? Qt.rgba(ApplicationWindow.window.ok.r,
                                                       ApplicationWindow.window.ok.g,
                                                       ApplicationWindow.window.ok.b,
                                                       0.10)
                                             : ApplicationWindow.window.secondary
                            border.color: connected ? Qt.rgba(ApplicationWindow.window.ok.r,
                                                              ApplicationWindow.window.ok.g,
                                                              ApplicationWindow.window.ok.b,
                                                              0.20)
                                                    : ApplicationWindow.window.border
                            Text {
                                anchors.centerIn: parent
                                text: displayName + "  " + (connected ? Math.round(actualRate * 10) / 10 + " Hz" : "--")
                                color: connected ? ApplicationWindow.window.ok : ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                font.bold: connected
                                elide: Text.ElideRight
                            }
                        }
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
                        columns: 4
                        columnSpacing: 0
                        rowSpacing: 0
                        Repeater {
                            model: [
                                { l: "home.latitude", v: Number(deviceBackend.coordinateData.latitude).toFixed(7), u: "°" },
                                { l: "home.longitude", v: Number(deviceBackend.coordinateData.longitude).toFixed(7), u: "°" },
                                { l: "home.altitude", v: Number(deviceBackend.coordinateData.altitude).toFixed(3), u: "m" },
                                { l: "home.velocity", v: Number(deviceBackend.coordinateData.velocity).toFixed(2), u: "m/s" },
                                { l: "home.heading", v: Number(deviceBackend.coordinateData.heading).toFixed(2), u: "°" },
                                { l: "home.pitch", v: Number(deviceBackend.coordinateData.pitch).toFixed(2), u: "°" },
                                { l: "home.rtkStatus", v: deviceBackend.coordinateData.rtkStatus || "---", u: "" },
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
                                { l: "home.temperature", v: Number(deviceBackend.environmentData.temperature).toFixed(2), u: "°C" },
                                { l: "home.humidity", v: Number(deviceBackend.environmentData.humidity).toFixed(2), u: "%" },
                                { l: "home.pressure", v: Number(deviceBackend.environmentData.pressure).toFixed(2), u: "hPa" },
                                { l: "home.laserRange", v: Number(deviceBackend.environmentData.laserRange).toFixed(3), u: "m" }
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
                        MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: ApplicationWindow.window.t("home.recStatus"); value: recordingBackend.status }
                        MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: ApplicationWindow.window.t("home.recDuration"); value: recordingBackend.durationText }
                        MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: ApplicationWindow.window.t("home.recSize"); value: recordingBackend.fileSizeText }
                        MetricTile { Layout.fillWidth: true; Layout.fillHeight: true; label: ApplicationWindow.window.t("home.recFrames"); value: String(recordingBackend.waveformFrames) }
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
                }
            }
        }
    }
}
