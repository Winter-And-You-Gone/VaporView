import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: rail
    property string currentPage: "home"
    signal navigate(string page)

    color: ApplicationWindow.window.card
    border.color: ApplicationWindow.window.border

    readonly property var items: [
        { key: "home", label: "nav.home", icon: "HOME" },
        { key: "devices", label: "nav.devices", icon: "CPU" },
        { key: "detailedData", label: "nav.detailedData", icon: "DATA" },
        { key: "waveform", label: "nav.waveform", icon: "WAV" },
        { key: "sessions", label: "nav.sessions", icon: "DIR" },
        { key: "rtk", label: "nav.rtk", icon: "RTK" },
        { key: "rawParser", label: "nav.rawParser", icon: "RAW" },
        { key: "settings", label: "nav.settings", icon: "SET" }
    ]

    Column {
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 8
        spacing: 5

        Repeater {
            model: rail.items
            delegate: Button {
                width: 48
                height: 48
                property bool active: rail.currentPage === modelData.key
                onClicked: rail.navigate(modelData.key)
                background: Rectangle {
                    radius: 6
                    color: active ? Qt.rgba(ApplicationWindow.window.primary.r,
                                            ApplicationWindow.window.primary.g,
                                            ApplicationWindow.window.primary.b,
                                            0.10)
                                  : hovered ? ApplicationWindow.window.secondary : "transparent"
                }
                contentItem: Column {
                    anchors.centerIn: parent
                    spacing: 1
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.icon
                        color: active ? ApplicationWindow.window.primary : ApplicationWindow.window.muted
                        font.pixelSize: 9 * ApplicationWindow.window.scaleFactor
                        font.bold: true
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 44
                        text: ApplicationWindow.window.t(modelData.label)
                        color: active ? ApplicationWindow.window.primary : ApplicationWindow.window.muted
                        font.pixelSize: 8 * ApplicationWindow.window.scaleFactor
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
