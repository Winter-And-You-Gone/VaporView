import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: card
    property alias title: titleText.text
    property Item headerRight
    default property alias content: body.data

    implicitHeight: ApplicationWindow.window.uiCardHeaderHeight + body.implicitHeight
    radius: ApplicationWindow.window.uiRadius
    color: ApplicationWindow.window.card
    border.width: 0
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            id: header
            implicitHeight: ApplicationWindow.window.uiCardHeaderHeight
            Layout.fillWidth: true
            Layout.preferredHeight: ApplicationWindow.window.uiCardHeaderHeight

            Rectangle {
                anchors.fill: parent
                radius: card.radius
                color: ApplicationWindow.window.cardHeader
                border.width: 0
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
                anchors.leftMargin: ApplicationWindow.window.uiCardPadding
                anchors.rightMargin: ApplicationWindow.window.uiCardPadding
                spacing: 8

                Text {
                    id: titleText
                    Layout.fillWidth: true
                    color: ApplicationWindow.window.text
                    font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor
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
            implicitHeight: childrenRect.height <= 0 ? 0 : childrenRect.y + childrenRect.height + 12
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    Rectangle {
        anchors.fill: parent
        z: 20
        radius: card.radius
        color: "transparent"
        border.color: ApplicationWindow.window.border
        border.width: 1
    }
}
