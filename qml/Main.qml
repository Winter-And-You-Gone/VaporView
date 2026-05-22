import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "components"
import "pages"
import "theme"

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

    property int uiRadius: appBackend.uiRadius
    property int uiControlHeight: appBackend.uiControlHeight
    property int uiButtonHeight: appBackend.uiButtonHeight
    property int uiCardHeaderHeight: appBackend.uiCardHeaderHeight
    property int uiCardPadding: appBackend.uiCardPadding
    property int uiControlPaddingX: appBackend.uiControlPaddingX
    property int uiSpacing: appBackend.uiSpacing
    property int uiBorderWidth: appBackend.uiBorderWidth
    property int uiSmallFontSize: appBackend.uiSmallFontSize
    property int uiBodyFontSize: appBackend.uiBodyFontSize
    property int uiValueFontSize: appBackend.uiValueFontSize
    property bool uiCompactMode: appBackend.uiCompactMode
    property bool uiShowDebugOutlines: appBackend.uiShowDebugOutlines

    AppTheme {
        id: appTheme

        dark: root.dark
        scaleFactor: root.scaleFactor
        radius: appBackend.uiRadius
        controlHeight: appBackend.uiControlHeight
        buttonHeight: appBackend.uiButtonHeight
        cardHeaderHeight: appBackend.uiCardHeaderHeight
        cardPadding: appBackend.uiCardPadding
        controlPaddingX: appBackend.uiControlPaddingX
        spacing: appBackend.uiSpacing
        borderWidth: appBackend.uiBorderWidth
        smallFontSize: appBackend.uiSmallFontSize
        bodyFontSize: appBackend.uiBodyFontSize
        valueFontSize: appBackend.uiValueFontSize
    }

    readonly property var theme: appTheme

    readonly property color bg: theme.bg
    readonly property color card: theme.surface
    readonly property color cardAlt: theme.surfaceAlt
    readonly property color cardHeader: theme.surfaceHeader
    readonly property color secondary: theme.surfaceAlt
    readonly property color border: theme.border
    readonly property color chartPlot: theme.chartPlot
    readonly property color chartGrid: theme.chartGrid
    readonly property color chartAxis: theme.chartAxis
    readonly property color text: theme.text
    readonly property color muted: theme.muted
    readonly property color primary: theme.primary
    readonly property color primaryForeground: theme.primaryForeground
    readonly property color primaryHover: theme.primaryHover
    readonly property color navActive: theme.navActive
    readonly property color navActiveFill: theme.navActiveFill
    readonly property color danger: theme.danger
    readonly property color dangerSoft: theme.dangerSoft
    readonly property color dangerSoftHover: theme.dangerSoftHover
    readonly property color dangerBorder: theme.dangerBorder
    readonly property color ok: theme.ok
    readonly property color warning: theme.warning
    readonly property color offline: theme.offline
    readonly property color waveformRaw: theme.waveformRaw
    readonly property color waveformHarmonic: theme.waveformHarmonic

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
