import QtQuick

Rectangle {
    id: cell

    property string label: ""
    property string value: ""
    property string unit: ""
    property color valueColor: ApplicationWindow.window.text

    implicitWidth: 180
    implicitHeight: Math.max(52, ApplicationWindow.window.uiControlHeight + 18)

    radius: ApplicationWindow.window.uiRadius
    color: ApplicationWindow.window.cardAlt
    border.width: ApplicationWindow.window.uiBorderWidth
    border.color: ApplicationWindow.window.border

    Column {
        anchors.fill: parent
        anchors.margins: Math.max(6, ApplicationWindow.window.uiControlPaddingX - 2)
        spacing: 3

        Text {
            id: labelText
            width: parent.width
            text: cell.label
            color: ApplicationWindow.window.muted
            font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor
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
                font.pixelSize: ApplicationWindow.window.uiValueFontSize * ApplicationWindow.window.scaleFactor
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
                color: ApplicationWindow.window.muted
                font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
