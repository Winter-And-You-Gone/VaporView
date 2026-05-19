import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Button {
    id: control

    property string variant: "secondary"
    property string iconName: ""
    property real iconSize: 16
    property color primaryColor: theme.primary
    property color dangerColor: theme.danger
    property color primaryForeground: theme.primaryForeground

    readonly property var theme: ApplicationWindow.window.theme
    readonly property color foreground: control.enabled
        ? (control.variant === "primary" ? control.primaryForeground
            : control.variant === "danger" ? control.dangerColor
            : theme.text)
        : theme.muted

    font.pixelSize: theme.font(theme.smallFontSize)
    font.weight: Font.DemiBold
    leftPadding: theme.controlPaddingX
    rightPadding: theme.controlPaddingX
    topPadding: 0
    bottomPadding: 0

    contentItem: RowLayout {
        anchors.verticalCenter: parent.verticalCenter
        spacing: control.iconName.length > 0 && control.text.length > 0 ? 6 : 0

        Item { Layout.preferredWidth: 0; Layout.fillWidth: true }

        LucideIcon {
            visible: control.iconName.length > 0
            Layout.preferredWidth: control.iconSize
            Layout.preferredHeight: control.iconSize
            name: control.iconName
            iconColor: control.foreground
            stroke: 2
        }

        Text {
            visible: control.text.length > 0
            text: control.text
            font: control.font
            color: control.foreground
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        Item { Layout.preferredWidth: 0; Layout.fillWidth: true }
    }

    background: Rectangle {
        implicitHeight: theme.buttonHeight
        radius: theme.radius
        color: !control.enabled ? theme.surfaceAlt
            : control.down ? downBase
            : control.hovered ? hoverBase
            : base
        border.color: control.variant === "primary" ? control.primaryColor
            : control.variant === "danger" ? theme.dangerBorder
            : theme.border
        property color base: control.variant === "primary" ? control.primaryColor
            : control.variant === "danger" ? theme.dangerSoft
            : theme.bg
        property color hoverBase: control.variant === "primary" ? theme.primaryHover
            : control.variant === "danger" ? theme.dangerSoftHover
            : theme.surfaceAlt
        property color downBase: control.variant === "primary" ? control.primaryColor
            : control.variant === "danger" ? theme.dangerSoftHover
            : theme.surfaceAlt
    }
}
