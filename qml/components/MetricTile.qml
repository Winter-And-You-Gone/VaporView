import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: tile

    property string label: ""
    property string value: "---"
    property string unit: ""

    readonly property var theme: ApplicationWindow.window.theme

    color: theme.surface
    border.width: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.cardPadding
        anchors.rightMargin: theme.cardPadding
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 6

        Text {
            Layout.fillWidth: true
            text: tile.label
            color: theme.muted
            opacity: 0.9
            font.pixelSize: theme.font(theme.smallFontSize)
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Text {
                text: tile.value
                color: theme.text
                font.pixelSize: theme.font(theme.valueFontSize)
                font.weight: Font.Bold
                elide: Text.ElideRight
            }

            Text {
                text: tile.unit
                color: theme.muted
                opacity: 0.9
                font.pixelSize: theme.font(theme.smallFontSize)
                visible: tile.unit.length > 0
                verticalAlignment: Text.AlignBottom
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }
}
