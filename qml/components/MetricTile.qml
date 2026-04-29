import QtQuick
import QtQuick.Layouts

Rectangle {
    id: tile
    property string label: ""
    property string value: "---"
    property string unit: ""
    color: ApplicationWindow.window.card
    border.color: ApplicationWindow.window.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 2

        Text {
            Layout.fillWidth: true
            text: tile.label
            color: ApplicationWindow.window.muted
            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                Layout.fillWidth: true
                text: tile.value
                color: ApplicationWindow.window.text
                font.pixelSize: 15 * ApplicationWindow.window.scaleFactor
                font.weight: Font.Bold
                font.family: "Consolas"
                elide: Text.ElideRight
            }
            Text {
                text: tile.unit
                color: ApplicationWindow.window.muted
                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                visible: tile.unit.length > 0
            }
        }
    }
}
