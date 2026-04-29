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
        { key: "home", label: "nav.home", icon: "layout-dashboard" },
        { key: "devices", label: "nav.devices", icon: "cpu" },
        { key: "detailedData", label: "nav.detailedData", icon: "table-properties" },
        { key: "waveform", label: "nav.waveform", icon: "activity" },
        { key: "sessions", label: "nav.sessions", icon: "folder-open" },
        { key: "rtk", label: "nav.rtk", icon: "satellite" },
        { key: "rawParser", label: "nav.rawParser", icon: "file-code" },
        { key: "settings", label: "nav.settings", icon: "settings" }
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
                    radius: 8
                    color: active ? Qt.rgba(ApplicationWindow.window.primary.r,
                                            ApplicationWindow.window.primary.g,
                                            ApplicationWindow.window.primary.b,
                                            0.10)
                                  : hovered ? ApplicationWindow.window.secondary : "transparent"
                }
                contentItem: Column {
                    anchors.centerIn: parent
                    spacing: 1
                    LucideIcon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 18
                        height: 18
                        name: modelData.icon
                        iconColor: active ? ApplicationWindow.window.primary : ApplicationWindow.window.muted
                        stroke: 1.8
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
