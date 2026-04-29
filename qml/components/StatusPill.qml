import QtQuick

Rectangle {
    id: pill
    property string status: "online"
    property string label: ""
    readonly property bool isRecording: status === "recording" || status === "active" || status === "error"
    readonly property bool isOnline: status === "online"
    readonly property color tone: isOnline ? (ApplicationWindow.window.dark ? "#4ade80" : "#15803d")
                                : isRecording ? (ApplicationWindow.window.dark ? "#fb7185" : "#be123c")
                                : (ApplicationWindow.window.dark ? "#fbbf24" : "#b45309")
    readonly property color fill: isOnline ? (ApplicationWindow.window.dark ? "#052e16" : "#dcfce7")
                                : isRecording ? (ApplicationWindow.window.dark ? "#4c0519" : "#ffe4e6")
                                : (ApplicationWindow.window.dark ? "#451a03" : "#fef3c7")
    readonly property color stroke: isOnline ? (ApplicationWindow.window.dark ? "#166534" : "#86efac")
                                  : isRecording ? (ApplicationWindow.window.dark ? "#9f1239" : "#fda4af")
                                  : (ApplicationWindow.window.dark ? "#92400e" : "#fcd34d")

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
