import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: control
    font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor
    color: ApplicationWindow.window.text
    selectedTextColor: ApplicationWindow.window.primaryForeground
    selectionColor: ApplicationWindow.window.primary
    placeholderTextColor: ApplicationWindow.window.muted
    leftPadding: ApplicationWindow.window.uiControlPaddingX
    rightPadding: ApplicationWindow.window.uiControlPaddingX
    selectByMouse: true

    background: Rectangle {
        implicitHeight: ApplicationWindow.window.uiControlHeight
        radius: ApplicationWindow.window.uiRadius
        color: control.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card
        border.width: ApplicationWindow.window.uiBorderWidth
        border.color: control.activeFocus
            ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8")
            : ApplicationWindow.window.border
    }
}
