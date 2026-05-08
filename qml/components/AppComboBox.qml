import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: control
    font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor
    implicitHeight: ApplicationWindow.window.uiControlHeight

    indicator: Text { anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 8; text: "\u25BE"; color: ApplicationWindow.window.muted; font.pixelSize: 12 }
    contentItem: Text { leftPadding: ApplicationWindow.window.uiControlPaddingX; rightPadding: 28; text: control.displayText; color: ApplicationWindow.window.text; font: control.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
    background: Rectangle { implicitHeight: ApplicationWindow.window.uiControlHeight; radius: ApplicationWindow.window.uiRadius; color: control.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card; border.width: ApplicationWindow.window.uiBorderWidth; border.color: control.activeFocus ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8") : ApplicationWindow.window.border }
}
