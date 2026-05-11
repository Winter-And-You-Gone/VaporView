import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: control
    property string displayRoleName: ""
    implicitHeight: ApplicationWindow.window.uiControlHeight
    font.family: "Consolas"
    font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor

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
        leftPadding: 10; rightPadding: 28
        text: control.displayText
        color: control.enabled ? ApplicationWindow.window.text : ApplicationWindow.window.muted
        font: control.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }

    indicator: LucideIcon {
        width: 14; height: 14
        x: control.width - width - 10
        y: Math.round((control.height - height) / 2)
        name: "chevron-down"
        iconColor: control.enabled ? ApplicationWindow.window.text : ApplicationWindow.window.muted
        stroke: 2
    }

    background: Rectangle {
        implicitHeight: ApplicationWindow.window.uiControlHeight; radius: ApplicationWindow.window.uiRadius
        color: control.enabled ? ApplicationWindow.window.bg : ApplicationWindow.window.cardAlt
        border.color: ApplicationWindow.window.border; border.width: ApplicationWindow.window.uiBorderWidth
    }

    delegate: ItemDelegate {
        id: opt
        width: control.width; height: ApplicationWindow.window.uiControlHeight
        text: control.displayFor(modelData)
        font.family: "Consolas"; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; font.bold: false
        highlighted: control.highlightedIndex === index
        background: Rectangle { radius: ApplicationWindow.window.uiRadius; color: (opt.hovered || opt.highlighted) ? ApplicationWindow.window.secondary : "transparent" }
        contentItem: Text { text: opt.text; color: ApplicationWindow.window.text; font: opt.font; verticalAlignment: Text.AlignVCenter; leftPadding: 10; rightPadding: 10; elide: Text.ElideRight }
    }

    popup: Popup {
        id: comboPopup
        y: control.height + 2; width: control.width; padding: 1
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        readonly property bool empty: control.count === 0
        implicitHeight: comboPopup.empty ? ApplicationWindow.window.uiControlHeight + 2 : Math.min(contentItem.implicitHeight, 220)
        background: Rectangle { radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.card; border.color: ApplicationWindow.window.border; border.width: ApplicationWindow.window.uiBorderWidth }
        contentItem: Item {
            implicitHeight: comboPopup.empty ? ApplicationWindow.window.uiControlHeight : listView.contentHeight
            ListView { id: listView; anchors.fill: parent; visible: !comboPopup.empty; clip: true; implicitHeight: contentHeight; model: control.popup.visible ? control.delegateModel : null; currentIndex: control.highlightedIndex; ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded } }
            Text { visible: comboPopup.empty; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 10; anchors.rightMargin: 10; text: appBackend.t("components.noOptions"); color: ApplicationWindow.window.muted; font.family: "Consolas"; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor; elide: Text.ElideRight }
        }
    }
}
