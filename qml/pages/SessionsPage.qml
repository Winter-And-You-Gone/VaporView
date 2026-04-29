import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components"

Item {
    property int selectedIndex: 0
    FolderDialog {
        id: folderDialog
        title: ApplicationWindow.window.t("settings.recordDir")
        onAccepted: {
            sessionBackend.setRecordingDirectory(selectedFolder.toString().replace("file:///", ""))
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            title: ApplicationWindow.window.t("sessions.sessionTable")
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                RowLayout {
                    Layout.fillWidth: true
                    ToolbarButton { iconName: "folder-open"; text: ApplicationWindow.window.t("settings.browse"); onClicked: folderDialog.open() }
                    ToolbarButton { iconName: "refresh-cw"; text: "Reload"; onClicked: sessionBackend.refreshSessions() }
                    Text { Layout.fillWidth: true; text: sessionBackend.recordingDirectory; color: ApplicationWindow.window.muted; font.pixelSize: 10; elide: Text.ElideMiddle }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: sessionBackend.sessions
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 54
                        color: index === selectedIndex ? Qt.rgba(ApplicationWindow.window.primary.r,
                                                                  ApplicationWindow.window.primary.g,
                                                                  ApplicationWindow.window.primary.b,
                                                                  0.06)
                                                       : "transparent"
                        border.color: ApplicationWindow.window.border
                        MouseArea {
                            anchors.fill: parent
                            onClicked: { selectedIndex = index; sessionBackend.selectSession(index) }
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            Text { Layout.fillWidth: true; text: modelData.name; color: ApplicationWindow.window.text; font.bold: true; elide: Text.ElideRight }
                            Text { text: modelData.duration; color: ApplicationWindow.window.muted; font.family: "Consolas"; font.pixelSize: 10 }
                            Text { text: modelData.size; color: ApplicationWindow.window.muted; font.family: "Consolas"; font.pixelSize: 10 }
                        }
                    }
                }
            }
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1.2
            title: ApplicationWindow.window.t("sessions.detailPanel")
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    MetricTile { Layout.fillWidth: true; Layout.preferredHeight: 68; label: ApplicationWindow.window.t("sessions.name"); value: sessionBackend.selectedSession.name || "---" }
                    MetricTile { Layout.fillWidth: true; Layout.preferredHeight: 68; label: ApplicationWindow.window.t("sessions.duration"); value: sessionBackend.selectedSession.duration || "---" }
                    MetricTile { Layout.fillWidth: true; Layout.preferredHeight: 68; label: ApplicationWindow.window.t("sessions.frames"); value: String(sessionBackend.selectedSession.frames || 0) }
                }

                TabBar {
                    id: tabs
                    Layout.fillWidth: true
                    TabButton {
                        id: csvTab
                        text: ApplicationWindow.window.t("sessions.csvPreview")
                        background: Rectangle { radius: 8; color: csvTab.checked ? ApplicationWindow.window.secondary : "transparent" }
                    }
                    TabButton {
                        id: waveformTab
                        text: ApplicationWindow.window.t("sessions.waveformPreview")
                        background: Rectangle { radius: 8; color: waveformTab.checked ? ApplicationWindow.window.secondary : "transparent" }
                    }
                    TabButton {
                        id: infoTab
                        text: ApplicationWindow.window.t("sessions.sessionInfo")
                        background: Rectangle { radius: 8; color: infoTab.checked ? ApplicationWindow.window.secondary : "transparent" }
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: tabs.currentIndex

                    ScrollView {
                        clip: true
                        Column {
                            width: parent.width
                            Repeater {
                                model: sessionBackend.csvPreviewRows
                                delegate: Text {
                                    width: parent.width
                                    text: JSON.stringify(modelData)
                                    color: ApplicationWindow.window.text
                                    font.family: "Consolas"
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                    WaveformCanvas {
                        samples: sessionBackend.waveformPreview
                        lineColor: ApplicationWindow.window.primary
                        plotBackground: ApplicationWindow.window.chartPlot
                        gridColor: ApplicationWindow.window.chartGrid
                        axisColor: ApplicationWindow.window.chartAxis
                        emptyColor: ApplicationWindow.window.muted
                        uiScale: ApplicationWindow.window.scaleFactor
                        emptyText: ApplicationWindow.window.t("sessions.waveformPreview")
                    }
                    ScrollView {
                        Text {
                            width: parent.width
                            text: JSON.stringify(sessionBackend.selectedSession, null, 2)
                            color: ApplicationWindow.window.text
                            font.family: "Consolas"
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }
}
