import QtQuick
import QtQuick.Controls.Basic

Item {
    id: control
    property var model: []
    property string text: ""
    property string placeholderText: ""
    property string displayRoleName: ""
    property bool acceptEmptyInput: false
    signal accepted(string text)
    signal activated(int index, string text, var modelData)

    implicitHeight: ApplicationWindow.window.uiControlHeight
    implicitWidth: 220
    readonly property int dropAreaW: 34

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
        if (v.length > 0 || control.acceptEmptyInput) { control.text = v; control.accepted(v) }
    }

    TextField {
        id: editor
        anchors.fill: parent
        text: control.text
        placeholderText: control.placeholderText
        font.family: "Consolas"
        font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor
        color: control.enabled ? ApplicationWindow.window.text : ApplicationWindow.window.muted
        selectedTextColor: ApplicationWindow.window.primaryForeground
        selectionColor: ApplicationWindow.window.primary
        placeholderTextColor: ApplicationWindow.window.muted
        leftPadding: 10; rightPadding: 28
        selectByMouse: true
        verticalAlignment: TextInput.AlignVCenter
        background: Rectangle { implicitHeight: ApplicationWindow.window.uiControlHeight; radius: ApplicationWindow.window.uiRadius; color: control.enabled ? ApplicationWindow.window.bg : ApplicationWindow.window.cardAlt; border.color: editor.activeFocus || popup.visible ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8") : ApplicationWindow.window.border; border.width: ApplicationWindow.window.uiBorderWidth }
        onEditingFinished: control.commit()
        onAccepted: control.commit()
    }

    LucideIcon {
        width: 14; height: 14
        anchors.right: parent.right; anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-down"
        iconColor: control.enabled ? ApplicationWindow.window.text : ApplicationWindow.window.muted
        stroke: 2; z: 20
    }

    MouseArea {
        anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.right: parent.right; width: control.dropAreaW
        hoverEnabled: true; cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton; z: 19
        onClicked: {
            if (popup.visible) {
                popup.close()
            } else {
                control.commit()
                popup.open()
            }
        }
    }

    Popup {
        id: popup
        x: 0; y: control.height + 2; width: control.width; padding: 1
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        readonly property bool empty: modelCount() === 0
        function modelCount() { if (!control.model) return 0; if (control.model.length !== undefined) return control.model.length; if (control.model.count !== undefined) return control.model.count; return 0 }
        implicitHeight: empty ? ApplicationWindow.window.uiControlHeight + 2 : Math.min(listView.contentHeight, 220)
        background: Rectangle { radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.card; border.color: ApplicationWindow.window.border; border.width: ApplicationWindow.window.uiBorderWidth }
        contentItem: Item {
            implicitHeight: popup.empty ? ApplicationWindow.window.uiControlHeight : listView.contentHeight
            ListView {
                id: listView; anchors.fill: parent; visible: !popup.empty; clip: true; implicitHeight: contentHeight
                model: control.model || []
                delegate: ItemDelegate {
                    id: opt
                    width: listView.width; height: ApplicationWindow.window.uiControlHeight
                    text: control.displayFor(modelData)
                    font.family: "Consolas"; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; font.bold: false
                    background: Rectangle { radius: ApplicationWindow.window.uiRadius; color: opt.hovered ? ApplicationWindow.window.secondary : "transparent" }
                    contentItem: Text { text: opt.text; color: ApplicationWindow.window.text; font: opt.font; verticalAlignment: Text.AlignVCenter; leftPadding: 10; rightPadding: 10; elide: Text.ElideRight }
                    onClicked: { var v = control.displayFor(modelData); editor.text = v; control.text = v; control.activated(index, v, modelData); control.accepted(v); popup.close() }
                }
            }
            Text { visible: popup.empty; anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.right: parent.right; anchors.leftMargin: 10; anchors.rightMargin: 10; text: appBackend.t("components.noOptions"); color: ApplicationWindow.window.muted; font.family: "Consolas"; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor; elide: Text.ElideRight }
        }
    }

    onTextChanged: { if (editor.text !== control.text) editor.text = control.text }
}
