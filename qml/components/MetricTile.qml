import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: tile
    property string label: ""
    property string value: "---"
    property string unit: ""
    readonly property bool darkMode: appBackend.dark
    readonly property real uiScale: appBackend.fontScale / 100
    color: darkMode ? "#020817" : "#ffffff"
    border.width: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 6

        Text {
            Layout.fillWidth: true
            text: tile.label
            color: darkMode ? "#94a3b8" : "#64748b"
            opacity: 0.9
            font.pixelSize: 10 * tile.uiScale
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Text {
                text: tile.value
                color: darkMode ? "#f8fafc" : "#020817"
                font.pixelSize: 17 * tile.uiScale
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Text {
                text: tile.unit
                color: darkMode ? "#94a3b8" : "#64748b"
                opacity: 0.9
                font.pixelSize: 10 * tile.uiScale
                visible: tile.unit.length > 0
                verticalAlignment: Text.AlignBottom
            }
            Item { Layout.fillWidth: true }
        }
    }
}
