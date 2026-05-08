import QtQuick

Item {
    id: root
    property color arrowColor: ApplicationWindow.window.muted
    property real strokeWidth: Math.max(1.4, 1.6 * ApplicationWindow.window.scaleFactor)

    implicitWidth: Math.max(40, ApplicationWindow.window.uiControlHeight)
    implicitHeight: ApplicationWindow.window.uiControlHeight

    Item {
        id: glyph
        anchors.centerIn: parent
        width: Math.max(8, 8 * ApplicationWindow.window.scaleFactor)
        height: Math.max(5, 5 * ApplicationWindow.window.scaleFactor)

        // Left stroke of chevron: rotates from top-right to bottom-center
        Rectangle {
            width: glyph.width / 2 + root.strokeWidth
            height: root.strokeWidth
            radius: root.strokeWidth / 2
            color: root.arrowColor
            x: glyph.width / 2 - width + root.strokeWidth / 2
            y: glyph.height / 2 - height / 2
            transformOrigin: Item.Right
            rotation: 45
            antialiasing: true
        }
        // Right stroke of chevron: rotates from top-left to bottom-center
        Rectangle {
            width: glyph.width / 2 + root.strokeWidth
            height: root.strokeWidth
            radius: root.strokeWidth / 2
            color: root.arrowColor
            x: glyph.width / 2 - root.strokeWidth / 2
            y: glyph.height / 2 - height / 2
            transformOrigin: Item.Left
            rotation: -45
            antialiasing: true
        }
    }

    // Debug background — uncomment to verify arrow region is visible
    // Rectangle { anchors.fill: parent; color: "#11ff0000" }
}
