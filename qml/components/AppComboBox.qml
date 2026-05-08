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

    indicator: Item { width: Math.max(32, control.height); height: control.height; x: control.width - width
        Text { anchors.centerIn: parent; text: "\u25BE"; color: ApplicationWindow.window.muted; font.pixelSize: 12 * ApplicationWindow.window.scaleFactor } }
    contentItem: Text { leftPadding: ApplicationWindow.window.uiControlPaddingX; rightPadding: Math.max(36, control.height); text: control.displayText; color: ApplicationWindow.window.text; font: control.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
    background: Rectangle { implicitHeight: control.implicitHeight; radius: ApplicationWindow.window.uiRadius; color: control.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card; border.width: ApplicationWindow.window.uiBorderWidth; border.color: control.activeFocus || control.popup.visible ? (ApplicationWindow.window.dark ? "#60a5fa" : "#2563eb") : ApplicationWindow.window.border }

    popup: Popup {
        id: comboPopup
        y: control.height + 2; width: control.width; padding: 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 220)
        background: Rectangle { radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.card; border.width: ApplicationWindow.window.uiBorderWidth; border.color: ApplicationWindow.window.border }
        contentItem: ListView { clip: true; implicitHeight: contentHeight; model: control.delegateModel; currentIndex: control.highlightedIndex }
    }
}
