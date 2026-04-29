import QtQuick

Rectangle {
    id: pill
    property string status: "online"
    property string label: ""
    readonly property bool isRecording: status === "recording" || status === "active" || status === "error"
    readonly property bool isOnline: status === "online"
    readonly property color fill: isOnline ? "#16a34a"
                                : isRecording ? "#e11d48"
                                : "#f59e0b"
    readonly property color tone: "#ffffff"
    readonly property color dotColor: isOnline ? "#bbf7d0"
                                     : isRecording ? "#ffe4e6"
                                     : "#fef3c7"
    readonly property color stroke: isOnline ? "#22c55e"
                                  : isRecording ? "#fb7185"
                                  : "#fbbf24"

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
            color: dotColor
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
