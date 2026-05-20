import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: control

    readonly property var theme: ApplicationWindow.window.theme

    font.pixelSize: theme.font(theme.bodyFontSize)
    color: theme.text
    selectedTextColor: theme.primaryForeground
    selectionColor: theme.primary
    placeholderTextColor: theme.muted
    leftPadding: theme.controlPaddingX
    rightPadding: theme.controlPaddingX
    selectByMouse: true

    background: Rectangle {
        implicitHeight: theme.controlHeight
        radius: theme.radius
        color: control.hovered ? theme.surfaceAlt : theme.surface
        border.width: theme.borderWidth
        border.color: control.activeFocus ? theme.focus : theme.border
    }
}
