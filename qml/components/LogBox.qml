import QtQuick
import QtQuick.Controls.Basic
Rectangle {
    id: box
    property string text: ""; property string placeholderText: ""; property bool wrapMode: true
    radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.secondary
    border.width: ApplicationWindow.window.uiBorderWidth; border.color: ApplicationWindow.window.border
    ScrollView { anchors.fill: parent; anchors.margins: ApplicationWindow.window.uiControlPaddingX; clip: true; ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        TextArea { text: box.text.length > 0 ? box.text : (box.placeholderText || ""); readOnly: true; selectByMouse: true; wrapMode: box.wrapMode ? TextEdit.Wrap : TextEdit.NoWrap; color: box.text.length > 0 ? ApplicationWindow.window.text : ApplicationWindow.window.muted; font.family: "Consolas"; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor; background: Rectangle { color: "transparent" } }
    }
}
