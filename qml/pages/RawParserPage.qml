import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components"

Item {
    FileDialog {
        id: rawFileDialog
        title: ApplicationWindow.window.t("rawParser.dropZone")
        nameFilters: ["VaporView raw DAT (*.dat)", "All files (*)"]
        onAccepted: rawParserBackend.openRawFile(selectedFile.toString())
    }

    FileDialog {
        id: exportDialog
        title: ApplicationWindow.window.t("rawParser.export")
        fileMode: FileDialog.SaveFile
        nameFilters: ["CSV (*.csv)", "JSON (*.json)", "BIN (*.bin)"]
        property string mode: "csv"
        onAccepted: {
            if (mode === "csv") rawParserBackend.exportListCsv(selectedFile.toString())
            else if (mode === "bin") rawParserBackend.exportSelectedPayload(selectedFile.toString())
            else rawParserBackend.exportDecodedJson(selectedFile.toString())
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 78
            radius: 8
            color: ApplicationWindow.window.cardAlt
            border.color: ApplicationWindow.window.border
            border.width: 1
            Text {
                anchors.centerIn: parent
                text: rawParserBackend.rawFilePath.length > 0 ? rawParserBackend.rawFilePath : ApplicationWindow.window.t("rawParser.dropZone")
                color: ApplicationWindow.window.muted
                font.pixelSize: 12 * ApplicationWindow.window.scaleFactor
                elide: Text.ElideMiddle
                width: parent.width - 24
                horizontalAlignment: Text.AlignHCenter
            }
            MouseArea { anchors.fill: parent; onClicked: rawFileDialog.open() }
        }

        RowLayout {
            Layout.fillWidth: true
            ToolbarButton { iconName: "upload"; text: ApplicationWindow.window.t("rawParser.dropZone"); variant: "primary"; onClicked: rawFileDialog.open() }
            ToolbarButton { iconName: "download"; text: ApplicationWindow.window.t("rawParser.export") + " CSV"; onClicked: { exportDialog.mode = "csv"; exportDialog.open() } }
            ToolbarButton { iconName: "download"; text: ApplicationWindow.window.t("rawParser.export") + " JSON"; onClicked: { exportDialog.mode = "json"; exportDialog.open() } }
            ToolbarButton { iconName: "download"; text: ApplicationWindow.window.t("rawParser.export") + " BIN"; onClicked: { exportDialog.mode = "bin"; exportDialog.open() } }
            ToolbarButton { iconName: "trash-2"; text: ApplicationWindow.window.t("rawParser.clearAll"); variant: "danger"; onClicked: rawParserBackend.clear() }
            Item { Layout.fillWidth: true }
            Text { text: rawParserBackend.records.length + " " + ApplicationWindow.window.t("rawParser.records"); color: ApplicationWindow.window.muted; font.pixelSize: 11 }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2
                title: ApplicationWindow.window.t("rawParser.parseRecords")
                ListView {
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    model: rawParserBackend.records
                    delegate: Rectangle {
                        width: ListView.view.width
                        height: 34
                        color: index % 2 === 0 ? "transparent" : ApplicationWindow.window.cardAlt
                        border.color: ApplicationWindow.window.border
                        MouseArea { anchors.fill: parent; onClicked: rawParserBackend.selectRecord(index) }
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 6
                            Text { Layout.preferredWidth: 42; text: modelData.index; color: ApplicationWindow.window.muted; font.family: "Consolas"; font.pixelSize: 10 }
                            Text { Layout.preferredWidth: 170; text: modelData.time; color: ApplicationWindow.window.text; font.family: "Consolas"; font.pixelSize: 10; elide: Text.ElideRight }
                            Text { Layout.preferredWidth: 88; text: modelData.source; color: ApplicationWindow.window.text; font.pixelSize: 10; elide: Text.ElideRight }
                            Text { Layout.preferredWidth: 108; text: modelData.recordTypeName; color: ApplicationWindow.window.text; font.pixelSize: 10; elide: Text.ElideRight }
                            Text { Layout.preferredWidth: 80; text: modelData.payloadSize + " B"; color: ApplicationWindow.window.muted; font.family: "Consolas"; font.pixelSize: 10 }
                            Text { Layout.fillWidth: true; text: modelData.payloadHex; color: ApplicationWindow.window.muted; font.family: "Consolas"; font.pixelSize: 10; elide: Text.ElideRight }
                        }
                    }
                }
            }

            Card {
                Layout.preferredWidth: 340
                Layout.fillHeight: true
                title: ApplicationWindow.window.t("rawParser.formatInfo")
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 10
                    Text {
                        width: parent.width
                        text: JSON.stringify(rawParserBackend.selectedRecord, null, 2)
                        color: ApplicationWindow.window.text
                        font.family: "Consolas"
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
