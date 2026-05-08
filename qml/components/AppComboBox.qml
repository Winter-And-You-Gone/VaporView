import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: control
    font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor
    implicitHeight: ApplicationWindow.window.uiControlHeight

    delegate: ItemDelegate {
        width: control.width; height: 32
        text: modelData
        font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor
        highlighted: control.highlightedIndex === index
        background: Rectangle { color: highlighted ? ApplicationWindow.window.secondary : "transparent" }
        contentItem: Text { text: modelData; color: ApplicationWindow.window.text; font: parent.font; verticalAlignment: Text.AlignVCenter; leftPadding: 8; elide: Text.ElideRight }
    }
    indicator: Text { anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 8; text: "\u25BE"; color: ApplicationWindow.window.muted; font.pixelSize: 12 }
    contentItem: Text { leftPadding: ApplicationWindow.window.uiControlPaddingX; rightPadding: 28; text: control.displayText; color: ApplicationWindow.window.text; font: control.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
    background: Rectangle { implicitHeight: ApplicationWindow.window.uiControlHeight; radius: ApplicationWindow.window.uiRadius; color: control.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card; border.width: ApplicationWindow.window.uiBorderWidth; border.color: control.activeFocus ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8") : ApplicationWindow.window.border }
    popup: Popup { y: control.height + 2; width: control.width; padding: 4; implicitHeight: Math.min(contentItem.implicitHeight + 8, 220 * ApplicationWindow.window.scaleFactor); background: Rectangle { radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.card; border.color: ApplicationWindow.window.border; border.width: ApplicationWindow.window.uiBorderWidth }; contentItem: ListView { clip: true; implicitHeight: contentHeight; model: control.delegateModel; currentIndex: control.highlightedIndex; ScrollBar.vertical: ScrollBar { policy: contentHeight > height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded } } }
}
