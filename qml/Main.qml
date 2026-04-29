import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"
import "pages"

ApplicationWindow {
    id: root
    width: 1440
    height: 860
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    title: "VaporView"

    property string currentPage: "home"
    property string lang: appBackend.language
    property string iconLibrary: "lucide"
    property int iconLibraryRevision: 0
    property bool dark: appBackend.dark
    property real scaleFactor: appBackend.fontScale / 100

    readonly property color bg: dark ? "#020817" : "#ffffff"
    readonly property color card: dark ? "#020817" : "#ffffff"
    readonly property color cardAlt: dark ? "#1e293b" : "#f1f5f9"
    readonly property color secondary: cardAlt
    readonly property color border: dark ? "#1e293b" : "#e2e8f0"
    readonly property color text: dark ? "#f8fafc" : "#020817"
    readonly property color muted: dark ? "#94a3b8" : "#64748b"
    readonly property color primary: "#0f172a"
    readonly property color primaryForeground: "#f8fafc"
    readonly property color primaryHover: "#1e293b"
    readonly property color danger: dark ? "#f87171" : "#ef4444"
    readonly property color dangerSoft: dark ? Qt.rgba(0.973, 0.443, 0.443, 0.10) : Qt.rgba(0.937, 0.267, 0.267, 0.10)
    readonly property color dangerSoftHover: dark ? Qt.rgba(0.973, 0.443, 0.443, 0.20) : Qt.rgba(0.937, 0.267, 0.267, 0.20)
    readonly property color dangerBorder: dark ? Qt.rgba(0.973, 0.443, 0.443, 0.30) : Qt.rgba(0.937, 0.267, 0.267, 0.30)
    readonly property color ok: dark ? "#4ade80" : "#22c55e"
    readonly property color warning: dark ? "#fbbf24" : "#f59e0b"
    readonly property color offline: dark ? "#94a3b8" : "#64748b"
    readonly property color waveformRaw: "#496083"
    readonly property color waveformHarmonic: "#5c78a3"

    function t(key) {
        appBackend.language
        return appBackend.t(key)
    }

    function normalizeIconLibrary(library) {
        var normalized = String(library || "lucide").toLowerCase()
        if (normalized === "tabler icons") normalized = "tabler"
        else if (normalized === "phosphor icons") normalized = "phosphor"
        if (normalized !== "tabler" && normalized !== "phosphor") normalized = "lucide"
        return normalized
    }

    function applyIconLibrary(library) {
        iconLibrary = normalizeIconLibrary(library)
        iconLibraryRevision += 1
    }

    color: bg

    Component.onCompleted: {
        applyIconLibrary(appBackend.loadIconLibrary())
    }

    Connections {
        target: appBackend
        function onNotificationRequested(level, message) {
            toastLevel = level
            toastText = message
            toastTimer.restart()
        }
    }

    property string toastText: ""
    property string toastLevel: "info"

    Timer {
        id: toastTimer
        interval: 3200
        onTriggered: toastText = ""
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar {
            Layout.fillWidth: true
            Layout.preferredHeight: 44
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            NavigationRail {
                Layout.preferredWidth: 60
                Layout.fillHeight: true
                currentPage: root.currentPage
                onNavigate: page => root.currentPage = page
            }

            Loader {
                id: pageLoader
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: {
                    if (root.currentPage === "devices") return devicesPage
                    if (root.currentPage === "detailedData") return detailedPage
                    if (root.currentPage === "waveform") return waveformPage
                    if (root.currentPage === "sessions") return sessionsPage
                    if (root.currentPage === "rtk") return rtkPage
                    if (root.currentPage === "rawParser") return rawParserPage
                    if (root.currentPage === "settings") return settingsPage
                    return homePage
                }
            }
        }
    }

    Rectangle {
        visible: toastText.length > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        width: Math.min(520, parent.width - 28)
        height: Math.max(42, toastLabel.implicitHeight + 18)
        radius: 6
        color: toastLevel === "error" ? "#7f1d1d" : toastLevel === "warning" ? "#7c4a03" : "#164e63"
        border.color: "#ffffff22"
        z: 20

        Text {
            id: toastLabel
            anchors.fill: parent
            anchors.margins: 10
            text: toastText
            color: "white"
            font.pixelSize: 12 * root.scaleFactor
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignVCenter
        }
    }

    Component { id: homePage; HomePage {} }
    Component { id: devicesPage; DevicesPage {} }
    Component { id: detailedPage; DetailedDataPage {} }
    Component { id: waveformPage; WaveformPage {} }
    Component { id: sessionsPage; SessionsPage {} }
    Component { id: rtkPage; RTKPage {} }
    Component { id: rawParserPage; RawParserPage {} }
    Component { id: settingsPage; SettingsPage {} }
}
