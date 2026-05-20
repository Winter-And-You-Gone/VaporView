import QtQuick
import QtQuick.Controls.Basic

TextField {
    id: control

    readonly property var theme: ApplicationWindow.window.theme

    implicitHeight: theme.controlHeight
    padding: 0
    leftPadding: theme.controlPaddingX
    rightPadding: theme.controlPaddingX

    font.pixelSize: theme.font(theme.bodyFontSize)
    color: theme.text
    selectedTextColor: theme.primaryForeground
    selectionColor: theme.primary
    placeholderTextColor: theme.muted
    selectByMouse: true

    Component.onCompleted: {
        topPadding = 0
        bottomPadding = 0
        padding = 0
    }

    background: Rectangle {
        anchors.fill: parent
        radius: theme.radius
        color: control.hovered ? theme.surfaceAlt : theme.surface
        border.width: theme.borderWidth
        border.color: control.activeFocus ? theme.focus : theme.border
    }
}
