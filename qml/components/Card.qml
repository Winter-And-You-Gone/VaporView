import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: card
    property alias title: titleText.text
    property Item headerRight
    default property alias content: body.data

    radius: 8
    color: ApplicationWindow.window.card
    border.color: ApplicationWindow.window.border
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            id: header
            Layout.fillWidth: true
            Layout.preferredHeight: 32

            Rectangle {
                anchors.fill: parent
                radius: card.radius
                color: ApplicationWindow.window.cardHeader
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: card.radius
                color: ApplicationWindow.window.cardHeader
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: ApplicationWindow.window.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 8

                Text {
                    id: titleText
                    Layout.fillWidth: true
                    color: ApplicationWindow.window.text
                    font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }

                Item {
                    id: rightSlot
                    Layout.preferredWidth: card.headerRight ? card.headerRight.implicitWidth : 0
                    Layout.preferredHeight: 28
                    Component.onCompleted: {
                        if (card.headerRight) {
                            card.headerRight.parent = rightSlot
                            card.headerRight.anchors.centerIn = rightSlot
                        }
                    }
                }
            }
        }

        Item {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }
}
