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

    property int uiRadius: 7
    property int uiControlHeight: 34
    property int uiButtonHeight: 30
    property int uiCardHeaderHeight: 32
    property int uiCardPadding: 12
    property int uiControlPaddingX: 10
    property int uiSpacing: 8
    property int uiBorderWidth: 1
    property int uiSmallFontSize: 10
    property int uiBodyFontSize: 11
    property int uiValueFontSize: 12
    property bool uiCompactMode: false
    property bool uiShowDebugOutlines: false

    Connections {
        target: appBackend
        function onUiStyleChanged() {
            root.uiRadius = appBackend.uiRadius
            root.uiControlHeight = appBackend.uiControlHeight
            root.uiButtonHeight = appBackend.uiButtonHeight
            root.uiCardHeaderHeight = appBackend.uiCardHeaderHeight
            root.uiCardPadding = appBackend.uiCardPadding
            root.uiControlPaddingX = appBackend.uiControlPaddingX
            root.uiSpacing = appBackend.uiSpacing
            root.uiBorderWidth = appBackend.uiBorderWidth
            root.uiSmallFontSize = appBackend.uiSmallFontSize
            root.uiBodyFontSize = appBackend.uiBodyFontSize
            root.uiValueFontSize = appBackend.uiValueFontSize
            root.uiCompactMode = appBackend.uiCompactMode
            root.uiShowDebugOutlines = appBackend.uiShowDebugOutlines
        }
    }

    readonly property color bg: dark ? "#020817" : "#ffffff"
    readonly property color card: dark ? "#020817" : "#ffffff"
    readonly property color cardAlt: dark ? "#1e293b" : "#f1f5f9"
    readonly property color cardHeader: dark ? "#0d1424" : "#f9fbfd"
    readonly property color secondary: cardAlt
    readonly property color border: dark ? "#1e293b" : "#e2e8f0"
    readonly property color chartPlot: card
    readonly property color chartGrid: dark ? "#334155" : "#e5edf6"
    readonly property color chartAxis: dark ? "#cbd5e1" : "#020817"
    readonly property color text: dark ? "#f8fafc" : "#020817"
    readonly property color muted: dark ? "#94a3b8" : "#64748b"
    readonly property color primary: "#0f172a"
    readonly property color primaryForeground: "#f8fafc"
    readonly property color primaryHover: "#1e293b"
    readonly property color navActive: dark ? "#e2e8f0" : primary
    readonly property color navActiveFill: dark ? Qt.rgba(0.89, 0.93, 0.98, 0.14) : Qt.rgba(0.06, 0.09, 0.16, 0.10)
    readonly property color danger: dark ? "#f87171" : "#ef4444"
    readonly property color dangerSoft: dark ? Qt.rgba(0.973, 0.443, 0.443, 0.10) : Qt.rgba(0.937, 0.267, 0.267, 0.10)
    readonly property color dangerSoftHover: dark ? Qt.rgba(0.973, 0.443, 0.443, 0.20) : Qt.rgba(0.937, 0.267, 0.267, 0.20)
    readonly property color dangerBorder: dark ? Qt.rgba(0.973, 0.443, 0.443, 0.30) : Qt.rgba(0.937, 0.267, 0.267, 0.30)
    readonly property color ok: dark ? "#4ade80" : "#22c55e"
    readonly property color warning: dark ? "#fbbf24" : "#f59e0b"
    readonly property color offline: dark ? "#94a3b8" : "#64748b"
    readonly property color waveformRaw: dark ? "#8fb3e6" : "#496083"
    readonly property color waveformHarmonic: waveformRaw

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
                    if (root.currentPage === "components") return componentsPage
                    return homePage
                }

                onLoaded: {
                    if (item) {
                        item.width = Qt.binding(function() { return pageLoader.width })
                        item.height = Qt.binding(function() { return pageLoader.height })
                    }
                }

                Binding {
                    target: pageLoader.item
                    property: "width"
                    value: pageLoader.width
                    when: pageLoader.item !== null
                }

                Binding {
                    target: pageLoader.item
                    property: "height"
                    value: pageLoader.height
                    when: pageLoader.item !== null
                }
            }
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
    Component { id: componentsPage; ComponentGalleryPage {} }
}
