import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components"

Item {
    function iconLibraryIndex() {
        if (ApplicationWindow.window.iconLibrary === "tabler") return 1
        if (ApplicationWindow.window.iconLibrary === "phosphor") return 2
        return 0
    }

    function setIconLibrary(index) {
        var library = "lucide"
        if (index === 1) library = "tabler"
        else if (index === 2) library = "phosphor"
        ApplicationWindow.window.applyIconLibrary(library)
        appBackend.saveIconLibrary(library)
    }

    FolderDialog {
        id: recordFolderDialog
        title: ApplicationWindow.window.t("settings.recordDir")
        onAccepted: settingsBackend.recordDirectory = selectedFolder.toString().replace("file:///", "")
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        spacing: ApplicationWindow.window.theme.spacing

        RowLayout {
            Layout.fillWidth: true
            spacing: ApplicationWindow.window.theme.spacing

            // ── 左列 ──
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: ApplicationWindow.window.theme.spacing

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 160
                    title: ApplicationWindow.window.t("settings.languageTheme")
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: ApplicationWindow.window.t("settings.language"); color: ApplicationWindow.window.muted; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor }
                            AppComboBox {
                                Layout.preferredWidth: 120
                                model: [ApplicationWindow.window.t("settings.languageZh"), ApplicationWindow.window.t("settings.languageEn")]
                                currentIndex: appBackend.language === "zh" ? 0 : 1
                                onActivated: appBackend.language = index === 0 ? "zh" : "en"
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: ApplicationWindow.window.t("settings.theme"); color: ApplicationWindow.window.muted; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor }
                            ToolbarButton { iconName: "settings"; text: appBackend.dark ? ApplicationWindow.window.t("settings.light") : ApplicationWindow.window.t("settings.dark"); onClicked: appBackend.toggleTheme() }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: ApplicationWindow.window.t("settings.iconLibrary"); color: ApplicationWindow.window.muted; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor }
                            AppComboBox {
                                Layout.preferredWidth: 160
                                model: ["Lucide", "Tabler Icons", "Phosphor Icons"]
                                currentIndex: iconLibraryIndex()
                                onActivated: function(idx) { setIconLibrary(idx) }
                            }
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    title: ApplicationWindow.window.t("settings.defaultSampleRate")
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: ApplicationWindow.window.t("settings.sensorCsv"); color: ApplicationWindow.window.muted; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor }
                            AppStepper { value: recordingBackend.exportRateHz; minimumValue: 1; maximumValue: 200; stepSize: 1; onValueEdited: function(v) { recordingBackend.exportRateHz = v } }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: ApplicationWindow.window.t("settings.waveform"); color: ApplicationWindow.window.muted; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor }
                            AppStepper { value: recordingBackend.waveformExportRateHz; minimumValue: 0; maximumValue: 200; stepSize: 1; onValueEdited: function(v) { recordingBackend.waveformExportRateHz = v } }
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 120
                    title: ApplicationWindow.window.t("settings.advancedDiag")
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        Text { Layout.fillWidth: true; text: ApplicationWindow.window.t("settings.backendInfo"); color: ApplicationWindow.window.muted; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor }
                        Text { Layout.fillWidth: true; text: ApplicationWindow.window.t("settings.collectorsInfo"); color: ApplicationWindow.window.muted; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor }
                        Text { Layout.fillWidth: true; text: ApplicationWindow.window.t("settings.rawDatInfo"); color: ApplicationWindow.window.muted; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor }
                    }
                }
            }

            // ── 右列 ──
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: ApplicationWindow.window.theme.spacing

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 130
                    title: ApplicationWindow.window.t("settings.recordDir")
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        AppTextField {
                            Layout.fillWidth: true
                            text: settingsBackend.recordDirectory
                            onEditingFinished: settingsBackend.recordDirectory = text
                        }
                        ToolbarButton { iconName: "folder-open"; text: ApplicationWindow.window.t("settings.browse"); onClicked: recordFolderDialog.open() }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    title: ApplicationWindow.window.t("settings.displayDensity")
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        RowLayout {
                            Layout.fillWidth: true
                            Text { Layout.fillWidth: true; text: ApplicationWindow.window.t("settings.fontScale"); color: ApplicationWindow.window.muted; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor }
                            Slider { Layout.fillWidth: true; from: 70; to: 150; stepSize: 5; value: appBackend.fontScale; onMoved: appBackend.fontScale = value }
                            Text { text: appBackend.fontScale + "%"; color: ApplicationWindow.window.text; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor }
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    title: ApplicationWindow.window.t("settings.about")
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        Text { Layout.fillWidth: true; text: settingsBackend.aboutText; color: ApplicationWindow.window.text; font.bold: true; font.pixelSize: 12 * ApplicationWindow.window.scaleFactor }
                        Text { text: ApplicationWindow.window.t("settings.version") + ": " + appBackend.version; color: ApplicationWindow.window.muted; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            ToolbarButton { iconName: "save"; text: ApplicationWindow.window.t("settings.save"); variant: "primary"; onClicked: settingsBackend.save() }
            ToolbarButton {
                iconName: "rotate-ccw"
                text: ApplicationWindow.window.t("settings.reset")
                onClicked: {
                    settingsBackend.reset()
                    ApplicationWindow.window.applyIconLibrary("lucide")
                }
            }
            Item { Layout.fillWidth: true }
        }
    }
}
