import QtQuick
import QtQuick.Layouts

Rectangle {
    id: tile
    property string label: ""
    property string value: "---"
    property string unit: ""
    color: ApplicationWindow.window.card
    border.width: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 6

        Text {
            Layout.fillWidth: true
            text: tile.label
            color: ApplicationWindow.window.muted
            font.pixelSize: 12 * ApplicationWindow.window.scaleFactor
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                text: tile.value
                color: ApplicationWindow.window.text
                font.pixelSize: 18 * ApplicationWindow.window.scaleFactor
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Text {
                text: tile.unit
                color: ApplicationWindow.window.muted
                font.pixelSize: 12 * ApplicationWindow.window.scaleFactor
                visible: tile.unit.length > 0
                verticalAlignment: Text.AlignBottom
            }
            Item { Layout.fillWidth: true }
        }
    }
}
