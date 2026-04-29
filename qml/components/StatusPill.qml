import QtQuick

Rectangle {
    id: pill
    property string status: "online"
    property string label: ""
    readonly property bool isRecording: status === "recording" || status === "active" || status === "error"
    readonly property bool isOnline: status === "online"
    readonly property bool darkMode: ApplicationWindow.window ? ApplicationWindow.window.dark : false
    readonly property real uiScale: ApplicationWindow.window ? ApplicationWindow.window.scaleFactor : 1
    readonly property color onlineTone: darkMode ? "#4ade80" : "#22c55e"
    readonly property color recordingTone: darkMode ? "#f87171" : "#ef4444"
    readonly property color inactiveTone: darkMode ? "#94a3b8" : "#64748b"
    readonly property color onlineFill: darkMode ? Qt.rgba(0.29, 0.87, 0.50, 0.12)
                                                 : Qt.rgba(0.13, 0.77, 0.37, 0.10)
    readonly property color recordingFill: darkMode ? Qt.rgba(0.97, 0.44, 0.44, 0.12)
                                                    : Qt.rgba(0.94, 0.27, 0.27, 0.10)
    readonly property color inactiveFill: darkMode ? "#1e293b" : "#f1f5f9"
    readonly property color onlineStroke: darkMode ? Qt.rgba(0.29, 0.87, 0.50, 0.30)
                                                   : Qt.rgba(0.13, 0.77, 0.37, 0.30)
    readonly property color recordingStroke: darkMode ? Qt.rgba(0.97, 0.44, 0.44, 0.30)
                                                      : Qt.rgba(0.94, 0.27, 0.27, 0.30)
    readonly property color inactiveStroke: darkMode ? "#334155" : "#e2e8f0"
    readonly property color tone: isOnline ? onlineTone
                                : isRecording ? recordingTone
                                : inactiveTone
    readonly property color fill: isOnline ? onlineFill
                                : isRecording ? recordingFill
                                : inactiveFill
    readonly property color stroke: isOnline ? onlineStroke
                                  : isRecording ? recordingStroke
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
            font.pixelSize: 10 * pill.uiScale
            font.weight: Font.DemiBold
        }
    }
}
