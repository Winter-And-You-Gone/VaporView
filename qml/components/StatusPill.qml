import QtQuick

Rectangle {
    id: pill
    property string status: "online"
    property string label: ""
    readonly property bool isRecording: status === "recording" || status === "active" || status === "error"
    readonly property bool isOnline: status === "online"
    readonly property color tone: isOnline ? "#22c55e"
                                : isRecording ? "#ef4444"
                                : "#f59e0b"
    readonly property color fill: isOnline ? "#1A22C55E"
                                : isRecording ? "#1AEF4444"
                                : "#1AF59E0B"
    readonly property color stroke: isOnline ? "#3322C55E"
                                  : isRecording ? "#33EF4444"
                                  : "#33F59E0B"

    implicitWidth: dot.width + labelText.implicitWidth + 24
    implicitHeight: 22
    radius: 11
    color: fill
    border.color: stroke

    Row {
        anchors.centerIn: parent
        spacing: 6

        Rectangle {
            id: dot
            width: 8
            height: 8
            radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: tone
        }

        Text {
            id: labelText
            anchors.verticalCenter: parent.verticalCenter
            text: pill.label
            color: tone
            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
            font.weight: Font.DemiBold
        }
    }
}
