import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: card
    property alias title: titleText.text
    property Item headerRight
    default property alias content: body.data

    radius: 6
    color: ApplicationWindow.window.card
    border.color: ApplicationWindow.window.border
    clip: true

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: ApplicationWindow.window.cardAlt
            border.color: ApplicationWindow.window.border

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
