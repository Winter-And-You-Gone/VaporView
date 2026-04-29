import QtQuick
import QtQuick.Controls.Basic

Button {
    id: control
    property string variant: "secondary"
    property color primaryColor: ApplicationWindow.window.primary
    property color dangerColor: ApplicationWindow.window.danger
    property color primaryForeground: ApplicationWindow.window.primaryForeground

    font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
    font.weight: Font.DemiBold
    leftPadding: 10
    rightPadding: 10
    topPadding: 5
    bottomPadding: 5

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled
               ? (control.variant === "primary" ? control.primaryForeground
                  : control.variant === "danger" ? control.dangerColor
                  : ApplicationWindow.window.text)
               : ApplicationWindow.window.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        implicitHeight: 28
        radius: 5
        color: !control.enabled ? ApplicationWindow.window.cardAlt
             : control.down ? downBase
             : control.hovered ? hoverBase
             : base
        border.color: control.variant === "primary" ? control.primaryColor
                    : control.variant === "danger" ? ApplicationWindow.window.dangerBorder
                    : ApplicationWindow.window.border
        property color base: control.variant === "primary" ? control.primaryColor
                          : control.variant === "danger" ? ApplicationWindow.window.dangerSoft
                          : ApplicationWindow.window.bg
        property color hoverBase: control.variant === "primary" ? ApplicationWindow.window.primaryHover
                               : control.variant === "danger" ? ApplicationWindow.window.dangerSoftHover
                               : ApplicationWindow.window.secondary
        property color downBase: control.variant === "primary" ? control.primaryColor
                              : control.variant === "danger" ? ApplicationWindow.window.dangerSoftHover
                              : ApplicationWindow.window.secondary
    }
}
