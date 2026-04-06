import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import FluentUI 1.0
import VaporViewApp

FluScrollablePage {
    title: appController.english ? "Sessions" : "会话"
    padding: 12
    spacing: 12

    Connections {
        target: sessionController
        function onSessionChanged() {
            sessionPathField.text = sessionController.sessionPath
        }
    }

    FolderDialog {
        id: folderDialog
        title: appController.english ? "Choose Session Folder" : "选择会话目录"
        onAccepted: sessionController.loadSessionUrl(selectedFolder)
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: sessionHeaderLayout.implicitHeight + 40

        ColumnLayout {
            id: sessionHeaderLayout
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10

            FluText {
                text: appController.english ? "Session Explorer" : "会话浏览器"
                font: FluTextStyle.Title
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                FluTextBox {
                    id: sessionPathField
                    Layout.fillWidth: true
                    text: sessionController.sessionPath
                    placeholderText: appController.english ? "Paste a session directory or session.json path" : "粘贴 session 目录或 session.json 路径"
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 10

                FluFilledButton {
                    text: appController.english ? "Open" : "打开"
                    onClicked: sessionController.loadSessionPath(sessionPathField.text)
                }

                FluButton {
                    text: appController.english ? "Browse" : "浏览"
                    onClicked: folderDialog.open()
                }

                FluButton {
                    text: appController.english ? "Reload" : "重载"
                    onClicked: sessionController.reload()
                }

                FluButton {
                    text: appController.english ? "Clear" : "清空"
                    onClicked: sessionController.clear()
                }
            }

            FluText {
                text: sessionController.statusText
                color: "#64748b"
                wrapMode: Text.WordWrap
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: width > 1180 ? 3 : width > 760 ? 2 : 1
        columnSpacing: 12
        rowSpacing: 12

        Repeater {
            model: [
                { title: appController.english ? "Session" : "会话", value: sessionController.sessionName || "---" },
                { title: appController.english ? "Start" : "开始时间", value: sessionController.startTime || "---" },
                { title: appController.english ? "End" : "结束时间", value: sessionController.endTime || "---" },
                { title: appController.english ? "Sensor Rows" : "传感器行数", value: String(sessionController.sensorRows) },
                { title: appController.english ? "Wave Files" : "波形文件", value: String(sessionController.waveformFiles) },
                { title: appController.english ? "Wave Frames" : "波形帧数", value: String(sessionController.waveformFrames) }
            ]

            delegate: FluFrame {
                Layout.fillWidth: true
                Layout.preferredHeight: 110

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 8

                    FluText {
                        text: modelData.title
                        color: "#64748b"
                    }

                    FluText {
                        text: modelData.value
                        font: FluTextStyle.TitleSmall
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: waveSectionLayout.implicitHeight + 36

        ColumnLayout {
            id: waveSectionLayout
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            RowLayout {
                Layout.fillWidth: true

                FluText {
                    text: appController.english ? "Waveform Frame" : "波形帧"
                    font: FluTextStyle.BodyStrong
                }

                Item { Layout.fillWidth: true }

                FluButton {
                    text: sessionController.peakScatterMode
                          ? (appController.english ? "Show Polyline" : "切换到折线图")
                          : (appController.english ? "Show Scatter" : "切换到散点图")
                    onClicked: sessionController.togglePeakPlotMode()
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 760 ? 2 : 1
                columnSpacing: 10
                rowSpacing: 10

                FluSlider {
                    Layout.fillWidth: true
                    from: 1
                    to: Math.max(1, sessionController.totalFrames)
                    enabled: sessionController.totalFrames > 0
                    value: Math.max(1, sessionController.currentFrame)
                    onMoved: sessionController.setCurrentFrame(Math.round(value))
                }

                FluSpinBox {
                    from: 1
                    to: Math.max(1, sessionController.totalFrames)
                    enabled: sessionController.totalFrames > 0
                    value: Math.max(1, sessionController.currentFrame)
                    onValueChanged: sessionController.setCurrentFrame(value)
                }
            }

            FluText {
                text: sessionController.frameInfoText
                wrapMode: Text.WordWrap
                color: "#64748b"
            }

            WaveformPlot {
                Layout.fillWidth: true
                Layout.preferredHeight: 280
                samples: sessionController.currentWaveSamples
                emptyText: appController.english ? "No waveform frame" : "尚未加载波形帧"
            }

            PeakSeriesPlot {
                Layout.fillWidth: true
                Layout.preferredHeight: 240
                values: sessionController.peakValues
                currentFrame: sessionController.currentFrame
                scatterMode: sessionController.peakScatterMode
                emptyText: appController.english ? "No peak overview" : "暂无峰值概览"
            }
        }
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: 480

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            FluText {
                text: appController.english ? "Sensor CSV" : "传感器 CSV"
                font: FluTextStyle.BodyStrong
            }

            FluText {
                text: sessionController.csvInfoText
                wrapMode: Text.WordWrap
                color: "#64748b"
            }

            FluTableView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columnSource: sessionController.csvColumnSource
                dataSource: sessionController.csvRows
                verticalHeaderVisible: false
            }
        }
    }
}
