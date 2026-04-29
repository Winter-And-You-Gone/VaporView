import QtQuick

Rectangle {
    id: pill
    property string status: "online"
    property string label: ""
    readonly property bool isRecording: status === "recording" || status === "active" || status === "error"
    readonly property bool isOnline: status === "online"
    readonly property color inactiveTone: ApplicationWindow.window.offline
    readonly property color inactiveFill: ApplicationWindow.window.dark
                                          ? Qt.rgba(0.58, 0.64, 0.72, 0.10)
                                          : Qt.rgba(0.39, 0.45, 0.55, 0.10)
    readonly property color inactiveStroke: ApplicationWindow.window.dark
                                            ? Qt.rgba(0.58, 0.64, 0.72, 0.30)
                                            : Qt.rgba(0.39, 0.45, 0.55, 0.30)
    readonly property color tone: isOnline ? "#22c55e"
                                : isRecording ? "#ef4444"
                                : inactiveTone
    readonly property color fill: isOnline ? "#1A22C55E"
                                : isRecording ? "#1AEF4444"
                                : inactiveFill
    readonly property color stroke: isOnline ? "#3322C55E"
                                  : isRecording ? "#33EF4444"
                                  : inactiveStroke

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
