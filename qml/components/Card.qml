import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: card

    property alias title: titleText.text
    property Item headerRight
    default property alias content: body.data

    readonly property var theme: ApplicationWindow.window.theme

    implicitHeight: theme.cardHeaderHeight + body.implicitHeight
    radius: theme.radius
    color: theme.surface
    border.width: 0
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            id: header

            implicitHeight: theme.cardHeaderHeight
            Layout.fillWidth: true
            Layout.preferredHeight: theme.cardHeaderHeight

            Rectangle {
                anchors.fill: parent
                radius: card.radius
                color: theme.surfaceHeader
                border.width: 0
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: card.radius
                color: theme.surfaceHeader
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: theme.borderWidth
                color: theme.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: theme.cardPadding
                anchors.rightMargin: theme.cardPadding
                spacing: theme.spacing

                Text {
                    id: titleText

                    Layout.fillWidth: true
                    color: theme.text
                    font.pixelSize: theme.font(theme.bodyFontSize)
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

            implicitHeight: childrenRect.height <= 0 ? 0 : childrenRect.y + childrenRect.height + theme.cardPadding
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    Rectangle {
        anchors.fill: parent
        z: 20
        radius: card.radius
        color: "transparent"
        border.color: theme.border
        border.width: theme.borderWidth
    }
}
