import QtQuick

QtObject {
    id: theme

    property bool dark: true
    property real scaleFactor: 1.0

    property int radius: 8
    property int controlHeight: 32
    property int buttonHeight: 30
    property int cardHeaderHeight: 36
    property int cardPadding: 12
    property int controlPaddingX: 10
    property int spacing: 8
    property int borderWidth: 1

    property int smallFontSize: 11
    property int bodyFontSize: 13
    property int valueFontSize: 17

    readonly property color bg: dark ? "#020817" : "#ffffff"
    readonly property color surface: dark ? "#020817" : "#ffffff"
    readonly property color surfaceAlt: dark ? "#1e293b" : "#f1f5f9"
    readonly property color surfaceHeader: dark ? "#0d1424" : "#f9fbfd"

    readonly property color border: dark ? "#1e293b" : "#e2e8f0"
    readonly property color text: dark ? "#f8fafc" : "#020817"
    readonly property color muted: dark ? "#94a3b8" : "#64748b"

    readonly property color primary: "#0f172a"
    readonly property color primaryForeground: "#f8fafc"
    readonly property color primaryHover: "#1e293b"
    readonly property color focus: dark ? "#60a5fa" : "#1d4ed8"

    readonly property color ok: dark ? "#4ade80" : "#22c55e"
    readonly property color warning: dark ? "#fbbf24" : "#f59e0b"
    readonly property color danger: dark ? "#f87171" : "#ef4444"
    readonly property color offline: dark ? "#94a3b8" : "#64748b"

    readonly property color dangerSoft: dark
        ? Qt.rgba(0.973, 0.443, 0.443, 0.10)
        : Qt.rgba(0.937, 0.267, 0.267, 0.10)

    readonly property color dangerSoftHover: dark
        ? Qt.rgba(0.973, 0.443, 0.443, 0.20)
        : Qt.rgba(0.937, 0.267, 0.267, 0.20)

    readonly property color dangerBorder: dark
        ? Qt.rgba(0.973, 0.443, 0.443, 0.30)
        : Qt.rgba(0.937, 0.267, 0.267, 0.30)

    readonly property color navActive: dark ? "#e2e8f0" : primary
    readonly property color navActiveFill: dark
        ? Qt.rgba(0.89, 0.93, 0.98, 0.14)
        : Qt.rgba(0.06, 0.09, 0.16, 0.10)

    readonly property color chartPlot: surface
    readonly property color chartGrid: dark ? "#334155" : "#e5edf6"
    readonly property color chartAxis: dark ? "#cbd5e1" : "#020817"

    readonly property color waveformRaw: dark ? "#8fb3e6" : "#496083"
    readonly property color waveformHarmonic: waveformRaw

    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    function font(size) {
        return Math.round(size * scaleFactor)
    }
}
