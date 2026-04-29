import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl

Item {
    id: icon

    property string name: ""
    property string library: ApplicationWindow.window ? ApplicationWindow.window.iconLibrary : "lucide"
    property int libraryRevision: ApplicationWindow.window ? ApplicationWindow.window.iconLibraryRevision : 0
    property color iconColor: ApplicationWindow.window ? ApplicationWindow.window.text : "#020817"
    property real stroke: 1.8

    implicitWidth: 18
    implicitHeight: 18

    readonly property string resolvedLibrary: {
        if (library === "tabler" || library === "phosphor") return library
        return "lucide"
    }
    readonly property string resolvedName: name.length > 0 ? name : "settings"
    readonly property url iconSource: Qt.resolvedUrl("../assets/icons/" + resolvedLibrary + "/" + resolvedName + ".svg")

    ColorImage {
        anchors.fill: parent
        source: icon.iconSource
        color: icon.iconColor
        sourceSize.width: Math.max(1, icon.width)
        sourceSize.height: Math.max(1, icon.height)
        fillMode: Image.PreserveAspectFit
    }
}
