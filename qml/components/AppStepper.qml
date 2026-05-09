import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: control
    property int value: 0
    property int minimumValue: 0
    property int maximumValue: 999999
    property int stepSize: 1
    property string suffix: ""
    property bool editable: false
    signal valueEdited(int value)

    implicitWidth: Math.max(150, ApplicationWindow.window.uiControlHeight * 3.8)
    implicitHeight: ApplicationWindow.window.uiControlHeight
    color: "transparent"
    border.width: 0
    radius: 0
    clip: false
    opacity: enabled ? 1.0 : 0.65

    function clamp(v) { var n = Number(v); if (isNaN(n)) n = minimumValue; return Math.max(minimumValue, Math.min(maximumValue, Math.round(n))) }
    function setClampedValue(v) { var next = clamp(v); if (next === value) return; value = next; valueEdited(value) }

    Row {
        anchors.fill: parent; spacing: 4

        Rectangle {
            id: minusButton
            width: control.height; height: parent.height
            radius: ApplicationWindow.window.uiRadius
            color: !control.enabled || control.value <= control.minimumValue ? ApplicationWindow.window.cardAlt
                : minusMouse.pressed ? ApplicationWindow.window.secondary : minusMouse.containsMouse ? ApplicationWindow.window.secondary : ApplicationWindow.window.bg
            border.width: ApplicationWindow.window.uiBorderWidth; border.color: ApplicationWindow.window.border
            Text { anchors.centerIn: parent; text: "\u2212"; color: control.enabled && control.value > control.minimumValue ? ApplicationWindow.window.text : ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiValueFontSize * ApplicationWindow.window.scaleFactor; font.bold: true; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
            MouseArea { id: minusMouse; anchors.fill: parent; hoverEnabled: true; enabled: control.enabled && control.value > control.minimumValue; cursorShape: Qt.PointingHandCursor; onClicked: control.setClampedValue(control.value - control.stepSize) }
        }

        Rectangle {
            id: valueArea
            width: Math.max(64, control.width - control.height * 2 - 4 * 2); height: parent.height
            radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.bg
            border.width: ApplicationWindow.window.uiBorderWidth; border.color: ApplicationWindow.window.border
            Text { visible: !control.editable; anchors.centerIn: parent; text: control.suffix.length > 0 ? control.value + " " + control.suffix : String(control.value); color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiValueFontSize * ApplicationWindow.window.scaleFactor; font.family: "Consolas"; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }

        Rectangle {
            id: plusButton
            width: control.height; height: parent.height
            radius: ApplicationWindow.window.uiRadius
            color: !control.enabled || control.value >= control.maximumValue ? ApplicationWindow.window.cardAlt
                : plusMouse.pressed ? ApplicationWindow.window.secondary : plusMouse.containsMouse ? ApplicationWindow.window.secondary : ApplicationWindow.window.bg
            border.width: ApplicationWindow.window.uiBorderWidth; border.color: ApplicationWindow.window.border
            Text { anchors.centerIn: parent; text: "+"; color: control.enabled && control.value < control.maximumValue ? ApplicationWindow.window.text : ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiValueFontSize * ApplicationWindow.window.scaleFactor; font.bold: true; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
            MouseArea { id: plusMouse; anchors.fill: parent; hoverEnabled: true; enabled: control.enabled && control.value < control.maximumValue; cursorShape: Qt.PointingHandCursor; onClicked: control.setClampedValue(control.value + control.stepSize) }
        }
    }
}
