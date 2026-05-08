import QtQuick
import QtQuick.Controls.Basic

Item {
    id: control
    property var model: []
    property string text: ""
    property string placeholderText: ""
    property string displayRoleName: ""
    signal accepted(string text)
    signal activated(int index, string text, var modelData)

    implicitHeight: ApplicationWindow.window.uiControlHeight
    implicitWidth: 220

    function displayFor(md) {
        if (md === undefined || md === null) return ""
        if (typeof md === "string") return md
        if (displayRoleName.length > 0 && md[displayRoleName] !== undefined) return String(md[displayRoleName])
        if (md.label !== undefined) return String(md.label)
        if (md.text !== undefined) return String(md.text)
        if (md.port !== undefined) return String(md.port)
        return String(md)
    }

    function commit() {
        var v = String(editor.text || "").trim()
        if (v.length > 0) { control.text = v; control.accepted(v) }
    }

    TextField {
        id: editor
        anchors.fill: parent
        text: control.text
        placeholderText: control.placeholderText
        font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor
        color: ApplicationWindow.window.text
        selectedTextColor: ApplicationWindow.window.primaryForeground
        selectionColor: ApplicationWindow.window.primary
        placeholderTextColor: ApplicationWindow.window.muted
        leftPadding: ApplicationWindow.window.uiControlPaddingX
        rightPadding: 44
        selectByMouse: true
        verticalAlignment: TextInput.AlignVCenter
        background: Rectangle { implicitHeight: control.implicitHeight; radius: ApplicationWindow.window.uiRadius; color: editor.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card; border.width: ApplicationWindow.window.uiBorderWidth; border.color: editor.activeFocus ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8") : ApplicationWindow.window.border }
        onEditingFinished: control.commit()
        onAccepted: control.commit()
    }

    Rectangle {
        anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: 2; width: 36; height: parent.height - 4
        radius: Math.max(4, ApplicationWindow.window.uiRadius - 2)
        color: dropMa.containsMouse ? ApplicationWindow.window.secondary : "transparent"
        Text { anchors.centerIn: parent; text: "\u25BE"; color: ApplicationWindow.window.muted; font.pixelSize: 14 }
        MouseArea { id: dropMa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: { control.commit(); popup.open() } }
    }

    Popup {
        id: popup
        x: 0; y: control.height + 2; width: control.width; padding: 4
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        implicitHeight: Math.min(listView.contentHeight + 8, 220)
        background: Rectangle { radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.card; border.width: ApplicationWindow.window.uiBorderWidth; border.color: ApplicationWindow.window.border }
        contentItem: ListView {
            id: listView; clip: true; implicitHeight: contentHeight
            model: control.model || []
            delegate: ItemDelegate {
                id: opt
                width: listView.width
                height: Math.max(30, ApplicationWindow.window.uiControlHeight - 2)
                text: control.displayFor(modelData)
                font.pixelSize: Math.max(10, (ApplicationWindow.window.uiBodyFontSize - 1) * ApplicationWindow.window.scaleFactor)
                font.bold: false
                background: Rectangle { radius: Math.max(4, ApplicationWindow.window.uiRadius - 2); color: opt.hovered ? ApplicationWindow.window.secondary : "transparent" }
                contentItem: Text { text: opt.text; color: ApplicationWindow.window.text; font: opt.font; verticalAlignment: Text.AlignVCenter; leftPadding: ApplicationWindow.window.uiControlPaddingX; rightPadding: ApplicationWindow.window.uiControlPaddingX; elide: Text.ElideRight }
                onClicked: { var v = control.displayFor(modelData); editor.text = v; control.text = v; control.activated(index, v, modelData); control.accepted(v); popup.close() }
            }
        }
    }

    onTextChanged: { if (editor.text !== control.text) editor.text = control.text }
}
