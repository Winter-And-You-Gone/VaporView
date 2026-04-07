import QtQuick
import QtQuick.Layouts
import FluentUI 1.0

FluFrame {
    id: root

    property string title: ""
    property string subtitle: ""
    property string rateText: ""
    property color accentColor: "#2563eb"
    property bool active: false
    property var fields: []

    Layout.fillWidth: true
    Layout.minimumWidth: 320
    Layout.preferredWidth: 360
    Layout.preferredHeight: contentLayout.implicitHeight + 36
    padding: 0

    ColumnLayout {
        id: contentLayout
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Rectangle {
                width: 12
                height: 12
                radius: 6
                color: root.active ? root.accentColor : "#94a3b8"
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                FluText {
                    text: root.title
                    font: FluTextStyle.BodyStrong
                }

                FluText {
                    text: root.subtitle
                    color: "#64748b"
                    visible: text.length > 0
                }
            }

            FluText {
                text: root.rateText
                color: root.active ? root.accentColor : "#64748b"
                font: FluTextStyle.Caption
            }
        }

        Repeater {
            model: root.fields

            delegate: RowLayout {
                Layout.fillWidth: true
                spacing: 12

                FluText {
                    Layout.minimumWidth: 112
                    Layout.preferredWidth: 128
                    text: modelData.label
                    color: "#64748b"
                    wrapMode: Text.WordWrap
                }

                FluText {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    text: modelData.value
                    font: FluTextStyle.BodyStrong
                    wrapMode: Text.WrapAnywhere
                }
            }
        }
    }
}
