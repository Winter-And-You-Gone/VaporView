import QtQuick

Item {
    id: root

    property color arrowColor: ApplicationWindow.window.muted
    property real arrowWidth: Math.max(8, 8 * ApplicationWindow.window.scaleFactor)
    property real arrowHeight: Math.max(5, 5 * ApplicationWindow.window.scaleFactor)

    implicitWidth: Math.max(40, ApplicationWindow.window.uiControlHeight)
    implicitHeight: ApplicationWindow.window.uiControlHeight

    Canvas {
        id: canvas
        width: root.arrowWidth
        height: root.arrowHeight
        anchors.centerIn: parent

        Component.onCompleted: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = root.arrowColor
            ctx.beginPath()
            ctx.moveTo(0, 0)
            ctx.lineTo(width, 0)
            ctx.lineTo(width / 2, height)
            ctx.closePath()
            ctx.fill()
        }
    }
}
