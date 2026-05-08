import QtQuick
Rectangle {
    id: cell
    property string label: ""; property string value: ""; property string unit: ""; property color valueColor: ApplicationWindow.window.text
    implicitHeight: 52; radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.cardAlt
    border.width: ApplicationWindow.window.uiBorderWidth; border.color: ApplicationWindow.window.border
    Column { anchors.centerIn: parent; spacing: 2
        Text { text: cell.label; color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
        Row { spacing: 3; anchors.horizontalCenter: parent.horizontalCenter
            Text { text: cell.value; color: cell.valueColor; font.pixelSize: ApplicationWindow.window.uiValueFontSize * ApplicationWindow.window.scaleFactor; font.bold: true; font.family: "Consolas"; elide: Text.ElideRight; maximumLineCount: 1 }
            Text { visible: cell.unit.length > 0; text: cell.unit; color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
        }
    }
}
