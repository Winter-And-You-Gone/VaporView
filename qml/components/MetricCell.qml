import QtQuick

Rectangle {
    id: cell

    property string label: ""
    property string value: ""
    property string unit: ""
    property color valueColor: theme.text

    readonly property var theme: ApplicationWindow.window.theme

    implicitWidth: 180
    implicitHeight: Math.max(52, theme.controlHeight + 18)

    radius: theme.radius
    color: theme.surfaceAlt
    border.width: theme.borderWidth
    border.color: theme.border

    Column {
        anchors.fill: parent
        anchors.margins: Math.max(6, theme.controlPaddingX - 2)
        spacing: 3

        Text {
            id: labelText

            width: parent.width
            text: cell.label
            color: theme.muted
            font.pixelSize: theme.font(theme.smallFontSize)
            elide: Text.ElideRight
            maximumLineCount: 1
            verticalAlignment: Text.AlignVCenter
        }

        Row {
            id: valueRow

            width: parent.width
            spacing: 4

            Text {
                id: valueText

                width: parent.width - (unitText.visible ? unitText.implicitWidth + valueRow.spacing : 0)
                text: cell.value
                color: cell.valueColor
                font.pixelSize: theme.font(theme.valueFontSize)
                font.bold: true
                font.family: "Consolas"
                elide: Text.ElideRight
                maximumLineCount: 1
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                id: unitText

                visible: cell.unit.length > 0
                text: cell.unit
                color: theme.muted
                font.pixelSize: theme.font(theme.smallFontSize)
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
