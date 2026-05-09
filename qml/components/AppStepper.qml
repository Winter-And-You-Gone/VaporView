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

    implicitWidth: Math.max(150, ApplicationWindow.window.uiControlHeight * 3.5)
    implicitHeight: ApplicationWindow.window.uiControlHeight
    radius: ApplicationWindow.window.uiRadius
    color: ApplicationWindow.window.card
    border.width: ApplicationWindow.window.uiBorderWidth
    border.color: ApplicationWindow.window.border
    clip: true
    opacity: enabled ? 1.0 : 0.65

    function clamp(v) { var n = Number(v); if (isNaN(n)) n = minimumValue; return Math.max(minimumValue, Math.min(maximumValue, Math.round(n))) }
    function setClampedValue(v) { var next = clamp(v); if (next === value) return; value = next; valueEdited(value) }

    Row {
        anchors.fill: parent; spacing: 0
        Rectangle { id: minusButton; width: control.height; height: parent.height
            color: minusMouse.containsMouse && control.enabled && control.value > control.minimumValue ? ApplicationWindow.window.secondary : ApplicationWindow.window.cardAlt
            Text { anchors.centerIn: parent; text: "\u2212"; color: control.enabled && control.value > control.minimumValue ? ApplicationWindow.window.text : ApplicationWindow.window.muted; font.pixelSize: Math.round(18 * ApplicationWindow.window.scaleFactor); font.bold: true; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
            MouseArea { id: minusMouse; anchors.fill: parent; hoverEnabled: true; enabled: control.enabled && control.value > control.minimumValue; cursorShape: Qt.PointingHandCursor; onClicked: control.setClampedValue(control.value - control.stepSize) }
        }
        Rectangle { width: ApplicationWindow.window.uiBorderWidth; height: parent.height; color: ApplicationWindow.window.border }
        Item { id: valueArea; width: Math.max(64, control.width - control.height * 2 - ApplicationWindow.window.uiBorderWidth * 2); height: parent.height
            Text { visible: !control.editable; anchors.centerIn: parent; text: control.suffix.length > 0 ? control.value + " " + control.suffix : String(control.value); color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; font.family: "Consolas"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
        }
        Rectangle { width: ApplicationWindow.window.uiBorderWidth; height: parent.height; color: ApplicationWindow.window.border }
        Rectangle { id: plusButton; width: control.height; height: parent.height
            color: plusMouse.containsMouse && control.enabled && control.value < control.maximumValue ? ApplicationWindow.window.secondary : ApplicationWindow.window.cardAlt
            Text { anchors.centerIn: parent; text: "+"; color: control.enabled && control.value < control.maximumValue ? ApplicationWindow.window.text : ApplicationWindow.window.muted; font.pixelSize: Math.round(18 * ApplicationWindow.window.scaleFactor); font.bold: true; verticalAlignment: Text.AlignVCenter; horizontalAlignment: Text.AlignHCenter }
            MouseArea { id: plusMouse; anchors.fill: parent; hoverEnabled: true; enabled: control.enabled && control.value < control.maximumValue; cursorShape: Qt.PointingHandCursor; onClicked: control.setClampedValue(control.value + control.stepSize) }
        }
    }
}
