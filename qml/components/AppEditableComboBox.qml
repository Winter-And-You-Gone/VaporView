import QtQuick
import QtQuick.Controls.Basic

Item {
    id: control

    property var model: []
    property string text: ""
    property string placeholderText: ""
    property string displayRoleName: ""
    property bool acceptEmptyInput: false

    readonly property var theme: ApplicationWindow.window.theme
    readonly property int dropAreaW: 34

    signal accepted(string text)
    signal activated(int index, string text, var modelData)

    implicitHeight: theme.controlHeight
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
        if (v.length > 0 || control.acceptEmptyInput) { control.text = v; control.accepted(v) }
    }

    TextField {
        id: editor

        anchors.fill: parent
        padding: 0
        text: control.text
        placeholderText: control.placeholderText
        font.family: "Consolas"
        font.pixelSize: theme.font(theme.bodyFontSize)
        color: control.enabled ? theme.text : theme.muted
        selectedTextColor: theme.primaryForeground
        selectionColor: theme.primary
        placeholderTextColor: theme.muted
        leftPadding: theme.controlPaddingX
        rightPadding: 28
        selectByMouse: true
        verticalAlignment: TextInput.AlignVCenter

        Component.onCompleted: {
            topPadding = 0
            bottomPadding = 0
            padding = 0
        }

        background: Rectangle {
            anchors.fill: parent
            radius: theme.radius
            color: control.enabled ? theme.bg : theme.surfaceAlt
            border.color: editor.activeFocus || popup.visible ? theme.focus : theme.border
            border.width: theme.borderWidth
        }
        onEditingFinished: control.commit()
        onAccepted: control.commit()
    }

    LucideIcon {
        width: 14
        height: 14
        anchors.right: parent.right
        anchors.rightMargin: theme.controlPaddingX
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron-down"
        iconColor: control.enabled ? theme.text : theme.muted
        stroke: 2
        z: 20
    }

    MouseArea {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: control.dropAreaW
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton
        z: 19
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

        x: 0
        y: control.height + 2
        width: control.width
        padding: 1
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        readonly property bool empty: modelCount() === 0
        function modelCount() { if (!control.model) return 0; if (control.model.length !== undefined) return control.model.length; if (control.model.count !== undefined) return control.model.count; return 0 }
        implicitHeight: empty ? theme.controlHeight + 2 : Math.min(listView.contentHeight, 220)
        background: Rectangle {
            radius: theme.radius
            color: theme.surface
            border.color: theme.border
            border.width: theme.borderWidth
        }
        contentItem: Item {
            implicitHeight: popup.empty ? theme.controlHeight : listView.contentHeight
            ListView {
                id: listView

                anchors.fill: parent
                visible: !popup.empty
                clip: true
                implicitHeight: contentHeight
                model: control.model || []
                delegate: ItemDelegate {
                    id: opt

                    width: listView.width
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
                    onClicked: { var v = control.displayFor(modelData); editor.text = v; control.text = v; control.activated(index, v, modelData); control.accepted(v); popup.close() }
                }
            }
            Text {
                visible: popup.empty
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

    onTextChanged: { if (editor.text !== control.text) editor.text = control.text }
}
