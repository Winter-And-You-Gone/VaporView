import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: control

    property string displayRoleName: ""

    readonly property var theme: ApplicationWindow.window.theme

    implicitHeight: theme.controlHeight
    padding: 0

    font.family: "Consolas"
    font.pixelSize: theme.font(theme.bodyFontSize)

    Component.onCompleted: {
        topPadding = 0
        bottomPadding = 0
        padding = 0
    }

    function displayFor(md) {
        if (md === undefined || md === null) return ""
        if (typeof md === "string") return md
        if (displayRoleName.length > 0 && md[displayRoleName] !== undefined) return String(md[displayRoleName])
        if (md.label !== undefined) return String(md.label)
        if (md.text !== undefined) return String(md.text)
        if (md.port !== undefined) return String(md.port)
        return String(md)
    }

    contentItem: Text {
        leftPadding: theme.controlPaddingX
        rightPadding: 28
        text: control.displayText
        color: control.enabled ? theme.text : theme.muted
        font: control.font
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: LucideIcon {
        width: 14
        height: 14
        x: control.width - width - theme.controlPaddingX
        y: Math.round((control.height - height) / 2)
        name: "chevron-down"
        iconColor: control.enabled ? theme.text : theme.muted
        stroke: 2
    }

    background: Rectangle {
        anchors.fill: parent
        radius: theme.radius
        color: control.enabled ? theme.bg : theme.surfaceAlt
        border.color: theme.border
        border.width: theme.borderWidth
    }

    delegate: ItemDelegate {
        id: opt

        width: control.width
        height: theme.controlHeight
        text: control.displayFor(modelData)
        font.family: "Consolas"
        font.pixelSize: theme.font(theme.bodyFontSize)
        font.bold: false
        background: Rectangle {
            radius: theme.radius
            color: opt.hovered ? theme.surfaceAlt : "transparent"
        }
        contentItem: Text {
            text: opt.text
            color: theme.text
            font: opt.font
            verticalAlignment: Text.AlignVCenter
            leftPadding: theme.controlPaddingX
            rightPadding: theme.controlPaddingX
            elide: Text.ElideRight
        }
    }

    popup: Popup {
        id: comboPopup

        y: control.height + 2
        width: control.width
        padding: 1
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        readonly property bool empty: control.count === 0
        implicitHeight: comboPopup.empty ? theme.controlHeight + 2 : Math.min(contentItem.implicitHeight, 220)
        background: Rectangle {
            radius: theme.radius
            color: theme.surface
            border.color: theme.border
            border.width: theme.borderWidth
        }
        contentItem: Item {
            implicitHeight: comboPopup.empty ? theme.controlHeight : listView.contentHeight
            ListView {
                id: listView

                anchors.fill: parent
                visible: !comboPopup.empty
                clip: true
                implicitHeight: contentHeight
                highlightRangeMode: ListView.NoHighlightRange
                model: control.popup.visible ? control.delegateModel : null
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            }
            Text {
                visible: comboPopup.empty
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: theme.controlPaddingX
                anchors.rightMargin: theme.controlPaddingX
                text: appBackend.t("components.noOptions")
                color: theme.muted
                font.family: "Consolas"
                font.pixelSize: theme.font(theme.smallFontSize)
                elide: Text.ElideRight
            }
        }
    }
}
