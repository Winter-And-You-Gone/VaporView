import QtQuick

Rectangle {
    id: pill
    property string status: "online"
    property string label: ""
    readonly property bool isRecording: status === "recording" || status === "active" || status === "error"
    readonly property bool isOnline: status === "online"
    readonly property color tone: isOnline ? (ApplicationWindow.window.dark ? "#4ade80" : "#22c55e")
                                : isRecording ? (ApplicationWindow.window.dark ? "#f87171" : "#ef4444")
                                : (ApplicationWindow.window.dark ? "#fbbf24" : "#f59e0b")
    readonly property color fill: isOnline
                                  ? (ApplicationWindow.window.dark ? Qt.rgba(0.290, 0.871, 0.502, 0.10) : Qt.rgba(0.133, 0.773, 0.369, 0.10))
                                  : isRecording
                                    ? (ApplicationWindow.window.dark ? Qt.rgba(0.973, 0.443, 0.443, 0.10) : Qt.rgba(0.937, 0.267, 0.267, 0.10))
                                    : (ApplicationWindow.window.dark ? Qt.rgba(0.984, 0.749, 0.141, 0.10) : Qt.rgba(0.961, 0.620, 0.043, 0.10))
    readonly property color stroke: isOnline
                                    ? (ApplicationWindow.window.dark ? Qt.rgba(0.290, 0.871, 0.502, 0.20) : Qt.rgba(0.133, 0.773, 0.369, 0.20))
                                    : isRecording
                                      ? (ApplicationWindow.window.dark ? Qt.rgba(0.973, 0.443, 0.443, 0.20) : Qt.rgba(0.937, 0.267, 0.267, 0.20))
                                      : (ApplicationWindow.window.dark ? Qt.rgba(0.984, 0.749, 0.141, 0.20) : Qt.rgba(0.961, 0.620, 0.043, 0.20))

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
