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
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 2

        Text {
            Layout.fillWidth: true
            text: tile.label.toUpperCase()
            color: ApplicationWindow.window.muted
            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                Layout.fillWidth: true
                text: tile.value
                color: ApplicationWindow.window.text
                font.pixelSize: 14 * ApplicationWindow.window.scaleFactor
                font.weight: Font.DemiBold
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
