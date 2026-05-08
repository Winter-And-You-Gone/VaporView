import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: control
    property string displayRoleName: ""
    font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor
    implicitHeight: ApplicationWindow.window.uiControlHeight

    function displayFor(md) {
        if (md === undefined || md === null) return ""
        if (typeof md === "string") return md
        if (displayRoleName.length > 0 && md[displayRoleName] !== undefined) return String(md[displayRoleName])
        if (md.label !== undefined) return String(md.label)
        if (md.text !== undefined) return String(md.text)
        if (md.port !== undefined) return String(md.port)
        return String(md)
    }

    delegate: ItemDelegate {
        id: opt
        width: control.width
        height: Math.max(30, ApplicationWindow.window.uiControlHeight - 2)
        text: control.displayFor(modelData)
        font.pixelSize: Math.max(10, (ApplicationWindow.window.uiBodyFontSize - 1) * ApplicationWindow.window.scaleFactor)
        font.bold: false
        highlighted: control.highlightedIndex === index
        background: Rectangle { radius: Math.max(4, ApplicationWindow.window.uiRadius - 2); color: (opt.hovered || opt.highlighted) ? ApplicationWindow.window.secondary : "transparent" }
        contentItem: Text { text: opt.text; color: ApplicationWindow.window.text; font: opt.font; verticalAlignment: Text.AlignVCenter; leftPadding: ApplicationWindow.window.uiControlPaddingX; rightPadding: ApplicationWindow.window.uiControlPaddingX; elide: Text.ElideRight }
    }

    indicator: Item {
        width: Math.max(40, ApplicationWindow.window.uiControlHeight); height: control.height; x: control.width - width
        AppComboArrow { anchors.fill: parent }
    }
    contentItem: Text { leftPadding: ApplicationWindow.window.uiControlPaddingX; rightPadding: Math.max(44, control.height + 4); text: control.displayText; color: ApplicationWindow.window.text; font: control.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
    background: Rectangle { implicitHeight: control.implicitHeight; radius: ApplicationWindow.window.uiRadius; color: control.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card; border.width: ApplicationWindow.window.uiBorderWidth; border.color: control.activeFocus || control.popup.visible ? (ApplicationWindow.window.dark ? "#60a5fa" : "#2563eb") : ApplicationWindow.window.border }

    popup: Popup {
        id: comboPopup
        y: control.height + 2; width: control.width; padding: 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        readonly property bool empty: control.count === 0
        implicitHeight: empty ? (Math.max(30, ApplicationWindow.window.uiControlHeight - 2) + 12) : Math.min(listView.contentHeight + 8, 220)
        background: Rectangle { radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.card; border.width: ApplicationWindow.window.uiBorderWidth; border.color: ApplicationWindow.window.border }
        contentItem: Item {
            implicitHeight: comboPopup.empty ? Math.max(30, ApplicationWindow.window.uiControlHeight - 2) : listView.contentHeight
            ListView { id: listView; anchors.fill: parent; visible: !comboPopup.empty; clip: true; implicitHeight: contentHeight; model: control.delegateModel; currentIndex: control.highlightedIndex }
            Text {
                visible: comboPopup.empty
                anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.right: parent.right
                anchors.leftMargin: ApplicationWindow.window.uiControlPaddingX; anchors.rightMargin: ApplicationWindow.window.uiControlPaddingX
                text: appBackend.t("components.noOptions"); color: ApplicationWindow.window.muted
                font.pixelSize: Math.max(10, (ApplicationWindow.window.uiBodyFontSize - 1) * ApplicationWindow.window.scaleFactor)
                elide: Text.ElideRight; maximumLineCount: 1
            }
        }
    }
}
