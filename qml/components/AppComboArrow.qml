import QtQuick

Item {
    id: root
    property color arrowColor: ApplicationWindow.window.muted
    property real arrowFontSize: Math.max(12, (ApplicationWindow.window.uiBodyFontSize + 1) * ApplicationWindow.window.scaleFactor)

    implicitWidth: Math.max(40, ApplicationWindow.window.uiControlHeight)
    implicitHeight: ApplicationWindow.window.uiControlHeight

    Text {
        anchors.centerIn: parent
        text: "\u25BC"
        color: root.arrowColor
        font.pixelSize: root.arrowFontSize
        font.bold: false
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
    }
}
