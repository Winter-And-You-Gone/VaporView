import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: pill
    property string status: "online"
    property string label: ""

    readonly property bool isSuccess: status === "online" || status === "success"
    readonly property bool isWarning: status === "warning"
    readonly property bool isDanger: status === "recording" || status === "active" || status === "error" || status === "danger"
    readonly property bool isInactive: !isSuccess && !isWarning && !isDanger

    readonly property color tone: isSuccess ? ApplicationWindow.window.ok
        : isWarning ? ApplicationWindow.window.warning
        : isDanger ? ApplicationWindow.window.danger
        : ApplicationWindow.window.offline

    implicitWidth: dot.width + labelText.implicitWidth + 24
    implicitHeight: Math.max(20, ApplicationWindow.window.uiControlHeight * 0.65)
    radius: implicitHeight / 2
    color: Qt.rgba(tone.r, tone.g, tone.b, 0.12)
    border.width: ApplicationWindow.window.uiBorderWidth > 0 ? 1 : 0
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.25)

    Row {
        anchors.centerIn: parent; spacing: 6
        Rectangle { id: dot; width: 8; height: 8; radius: 4; anchors.verticalCenter: parent.verticalCenter; color: tone }
        Text { id: labelText; anchors.verticalCenter: parent.verticalCenter; text: pill.label; color: tone; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor; font.weight: Font.DemiBold }
    }
}
