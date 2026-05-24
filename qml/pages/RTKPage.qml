import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    id: page

    function t(key) {
        appBackend.language
        return appBackend.t(key)
    }

    function textValue(value, fallback) {
        if (value === undefined || value === null) return fallback || ""
        return String(value)
    }

    function statNumber(key, fallback) {
        var stats = rtkBackend.stats || {}
        var value = stats[key]
        if (value === undefined || value === null || isNaN(Number(value)))
            return fallback || 0
        return Number(value)
    }

    function diffStatusText() {
        if (!rtkBackend.running) return t("rtk.statusDisconnected")
        var stats = rtkBackend.stats || {}
        var msg = stats.message || ""
        if (msg.indexOf("connected") >= 0 || stats.inputBps > 0) return t("rtk.connected")
        return t("rtk.statusRunning")
    }

    function diffStatusColor() {
        if (!rtkBackend.running) return ApplicationWindow.window.muted
        var stats = rtkBackend.stats || {}
        if (stats.inputBps > 0) return ApplicationWindow.window.ok
        return ApplicationWindow.window.warning
    }

    property var ggaSourceModel: []
    property int ggaSourceIndex: 0
    property bool diagnosticLogAutoFollow: true
    property bool diagnosticLogProgrammaticScroll: false

    function rebuildDiagnosticLogModel(lines) {
        diagnosticLogModel.clear()
        for (var i = 0; i < lines.length; ++i)
            diagnosticLogModel.append({ entry: String(lines[i]) })
    }

    function syncDiagnosticLogModel() {
        var lines = rtkBackend.diagnostics || []
        var previousCount = diagnosticLogModel.count
        var changed = false
        if (lines.length === 0) {
            if (previousCount > 0)
                diagnosticLogModel.clear()
            return
        }

        if (lines.length > previousCount) {
            var prefixMatches = true
            for (var i = 0; i < previousCount; ++i) {
                if (diagnosticLogModel.get(i).entry !== String(lines[i])) {
                    prefixMatches = false
                    break
                }
            }
            if (prefixMatches) {
                for (var j = previousCount; j < lines.length; ++j)
                    diagnosticLogModel.append({ entry: String(lines[j]) })
                changed = true
            } else {
                rebuildDiagnosticLogModel(lines)
                changed = true
            }
        } else {
            var sameLines = lines.length === previousCount
            for (var k = 0; sameLines && k < lines.length; ++k)
                sameLines = diagnosticLogModel.get(k).entry === String(lines[k])
            if (!sameLines) {
                rebuildDiagnosticLogModel(lines)
                changed = true
            }
        }

        if (diagnosticLogAutoFollow && changed && diagnosticLogModel.count > 0)
            logScrollTimer.restart()
    }

    function diagnosticLogAtEnd() {
        if (!diagnosticsListView || diagnosticsListView.contentHeight <= diagnosticsListView.height)
            return true
        return diagnosticsListView.contentY >= Math.max(0, diagnosticsListView.contentHeight - diagnosticsListView.height - 2)
    }

    function refreshGgaSourceModel() {
        var selectedValue = "epsilon_main"
        if (ggaSourceIndex >= 0 && ggaSourceIndex < ggaSourceModel.length)
            selectedValue = ggaSourceModel[ggaSourceIndex].value

        var result = [{ text: t("rtk.ggaSourceEpsilonMain"), value: "epsilon_main" }]
        var epsilonPort = String(deviceBackend.selectedPort("epsilon") || "")
        var ptbPort = String(deviceBackend.selectedPort("ptb") || "")
        var hmpPort = String(deviceBackend.selectedPort("hmp") || "")
        var lidarPort = String(deviceBackend.selectedPort("lidar") || "")
        var rtkOutPort = String(rtkBackend.outputPort || "")
        var ports = deviceBackend.ports || []
        for (var i = 0; i < ports.length; ++i) {
            var port = String(ports[i] || "")
            if (port.length > 0 && port !== epsilonPort) {
                var suffix = ""
                if (port === ptbPort) suffix = t("devices.ptb210")
                else if (port === hmpPort) suffix = t("devices.hmp")
                else if (port === lidarPort) suffix = t("devices.tfa1500")
                else if (port === rtkOutPort) suffix = t("rtk.outputPort")
                var text = suffix.length > 0 ? port + " " + suffix : port
                result.push({ text: text, value: "serial:" + port })
            }
        }

        ggaSourceModel = result
        ggaSourceIndex = 0
        for (var j = 0; j < result.length; ++j) {
            if (result[j].value === selectedValue) {
                ggaSourceIndex = j
                break
            }
        }
    }

    Component.onCompleted: {
        refreshGgaSourceModel()
        syncDiagnosticLogModel()
    }

    ListModel { id: diagnosticLogModel }

    property var ggaRateModel: [
        { text: "1 Hz", value: 1 },
        { text: "2 Hz", value: 2 },
        { text: "5 Hz", value: 5 },
        { text: "10 Hz", value: 10 },
        { text: "20 Hz", value: 20 },
    ]

    property string uiTimeoutMs: String(rtkBackend.timeoutMs)
    property string uiReconnectMs: String(rtkBackend.reconnectMs)
    property string uiLeverX: "0"
    property string uiLeverY: "0"
    property string uiLeverZ: "0"

    readonly property var mountPointModel: {
        var result = []
        function add(v) { v = String(v||"").trim(); if (v.length===0) return; if (result.indexOf(v)<0) result.push(v) }
        add(rtkBackend.mountpoint && rtkBackend.mountpoint.length > 0 ? rtkBackend.mountpoint : "AUTO")
        add("AUTO")
        var opts = rtkBackend.mountPointOptions || []
        for (var i=0; i<opts.length; ++i) add(opts[i])
        return result
    }

    // ══════════════════════════════════════════════
    // Flickable root — top-aligned, no fillHeight stretch
    // ══════════════════════════════════════════════
    Flickable {
        id: rtkFlick
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        readonly property int scrollPad: 12

        contentWidth: width
        contentHeight: Math.max(mainRow.implicitHeight, height)

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }

        RowLayout {
            id: mainRow
            x: 0
            y: 0
            width: rtkFlick.width - rtkFlick.scrollPad
            height: implicitHeight
            spacing: 8

            // ═══════ LEFT COLUMN — Configuration ═══════
            ColumnLayout {
                id: leftColumn
                Layout.preferredWidth: Math.max(360, Math.min(mainRow.width * 0.34, 520))
                Layout.minimumWidth: 340
                Layout.alignment: Qt.AlignTop
                spacing: 8

                // ── NTRIP Config ──
                Card {
                    Layout.fillWidth: true
                    title: t("rtk.ntripConfig")

                    RtkCardColumn {
                        id: ntripContent

                        // Row 1: Server + Port
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ColumnLayout { Layout.fillWidth: true; spacing: 2
                                RtkLabel { text: t("rtk.casterAddress") }
                                RtkTextField {
                                    Layout.fillWidth: true
                                    text: page.textValue(rtkBackend.server)
                                    onEditingFinished: rtkBackend.server = text
                                }
                            }
                            ColumnLayout { Layout.preferredWidth: 90; spacing: 2
                                RtkLabel { text: t("rtk.port") }
                                RtkTextField {
                                    Layout.fillWidth: true
                                    text: page.textValue(rtkBackend.port, "2101")
                                    onEditingFinished: rtkBackend.port = text
                                }
                            }
                        }

                        // Row 2: Mount Point + Detect
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ColumnLayout { Layout.fillWidth: true; spacing: 2
                                RtkLabel { text: t("rtk.mountPoint") }
                                RtkMountPointCombo { Layout.fillWidth: true }
                            }
                            ColumnLayout { Layout.preferredWidth: 100; spacing: 2
                                RtkLabel { text: " " }
                                ToolbarButton {
                                    Layout.fillWidth: true
                                    height: 30
                                    iconName: "search"
                                    text: rtkBackend.detectingMountPoints
                                        ? t("rtk.detecting") : t("rtk.detectMountPoints")
                                    enabled: !rtkBackend.detectingMountPoints
                                    onClicked: rtkBackend.detectMountPoints()
                                }
                            }
                        }

                        // Row 3: Username + Password
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ColumnLayout { Layout.fillWidth: true; spacing: 2
                                RtkLabel { text: t("rtk.username") }
                                RtkTextField {
                                    Layout.fillWidth: true
                                    text: page.textValue(rtkBackend.username)
                                    onEditingFinished: rtkBackend.username = text
                                }
                            }
                            ColumnLayout { Layout.fillWidth: true; spacing: 2
                                RtkLabel { text: t("rtk.password") }
                                RtkTextField {
                                    Layout.fillWidth: true
                                    echoMode: TextInput.Normal
                                    text: page.textValue(rtkBackend.password)
                                    onEditingFinished: rtkBackend.password = text
                                }
                            }
                        }

                        // Row 4: Auto Reconnect
                        // Row 4: Auto Reconnect
                        Row {
                            spacing: 6
                            Rectangle {
                                width: 16; height: 16; radius: 3
                                anchors.verticalCenter: parent.verticalCenter
                                color: rtkBackend.autoReconnect ? ApplicationWindow.window.primary : "transparent"
                                border.color: rtkBackend.autoReconnect ? ApplicationWindow.window.primary : ApplicationWindow.window.border
                                Text {
                                    anchors.centerIn: parent
                                    text: "✓"
                                    color: rtkBackend.autoReconnect ? ApplicationWindow.window.primaryForeground : "transparent"
                                    font.pixelSize: 15 * ApplicationWindow.window.scaleFactor
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: rtkBackend.autoReconnect = !rtkBackend.autoReconnect
                                }
                            }
                            Text {
                                text: t("rtk.autoReconnect")
                                color: ApplicationWindow.window.text
                                font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                anchors.verticalCenter: parent.verticalCenter
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: rtkBackend.autoReconnect = !rtkBackend.autoReconnect
                                }
                            }
                        }
                    }
                }

                // ── RTCM Output Config ──
                Card {
                    Layout.fillWidth: true
                    title: t("rtk.rtcmOutputConfig")

                    RtkCardColumn {
                        id: rtcmContent

                        // Row 1: Output Port + Baud Rate
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ColumnLayout { Layout.fillWidth: true; spacing: 2
                                RtkLabel { text: t("rtk.outputPort") }
                                RtkPortComboBox { Layout.fillWidth: true }
                            }
                            ColumnLayout { Layout.preferredWidth: 100; spacing: 2
                                RtkLabel { text: t("rtk.baudRate") }
                                RtkTextField {
                                    Layout.fillWidth: true
                                    text: page.textValue(rtkBackend.outputBaud, "115200")
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    onEditingFinished: rtkBackend.outputBaud = Math.max(1, Number(text))
                                }
                            }
                        }

                        // Row 2: Lever Arm X/Y/Z + Apply
                        RtkLabel { text: t("rtk.leverArm") }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                text: "X"; color: ApplicationWindow.window.text
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor; font.bold: true
                                Layout.alignment: Qt.AlignVCenter
                            }
                            RtkTextField {
                                id: leverX; Layout.fillWidth: true
                                text: page.uiLeverX
                                validator: DoubleValidator { bottom: -10000; top: 10000; decimals: 4 }
                                onEditingFinished: page.uiLeverX = text
                            }
                            Text {
                                text: "Y"; color: ApplicationWindow.window.text
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor; font.bold: true
                                Layout.alignment: Qt.AlignVCenter
                            }
                            RtkTextField {
                                id: leverY; Layout.fillWidth: true
                                text: page.uiLeverY
                                validator: DoubleValidator { bottom: -10000; top: 10000; decimals: 4 }
                                onEditingFinished: page.uiLeverY = text
                            }
                            Text {
                                text: "Z"; color: ApplicationWindow.window.text
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor; font.bold: true
                                Layout.alignment: Qt.AlignVCenter
                            }
                            RtkTextField {
                                id: leverZ; Layout.fillWidth: true
                                text: page.uiLeverZ
                                validator: DoubleValidator { bottom: -10000; top: 10000; decimals: 4 }
                                onEditingFinished: page.uiLeverZ = text
                            }
                        }
                        ToolbarButton {
                            Layout.fillWidth: true
                            text: t("rtk.applyLeverArm"); iconName: "ruler"
                            variant: "primary"
                            onClicked: rtkBackend.applyMainAntennaLeverArm(
                                Number(leverX.text), Number(leverY.text), Number(leverZ.text))
                        }

                        // Row 3: Timeout + Reconnect
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            ColumnLayout { Layout.fillWidth: true; spacing: 2
                                RtkLabel { text: t("rtk.timeoutMs") }
                                RtkTextField {
                                    Layout.fillWidth: true
                                    text: page.uiTimeoutMs
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    onEditingFinished: {
                                        page.uiTimeoutMs = text
                                        rtkBackend.timeoutMs = Math.max(1, Number(text))
                                    }
                                    Component.onCompleted: page.uiTimeoutMs = String(rtkBackend.timeoutMs)
                                }
                            }
                            ColumnLayout { Layout.fillWidth: true; spacing: 2
                                RtkLabel { text: t("rtk.reconnectMs") }
                                RtkTextField {
                                    Layout.fillWidth: true
                                    text: page.uiReconnectMs
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    onEditingFinished: {
                                        page.uiReconnectMs = text
                                        rtkBackend.reconnectMs = Math.max(1, Number(text))
                                    }
                                    Component.onCompleted: page.uiReconnectMs = String(rtkBackend.reconnectMs)
                                }
                            }
                        }
                    }
                }
            }

            // ═══════ RIGHT COLUMN — Monitoring & Diagnostics ═══════
            ColumnLayout {
                id: rightColumn
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 8

                // ── GGA Monitor ──
                Card {
                    id: ggaCard
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(210, Math.min(270, leftColumn.implicitHeight * 0.42))
                    Layout.minimumHeight: 180
                    Layout.maximumHeight: 280
                    title: t("rtk.ggaMonitor")

                    RtkCardFix {
                        // GGA Source + Generation Rate + Actual Rate
                        RowLayout {
                            id: ggaTopRow
                            width: parent.width
                            spacing: 8

                            Text {
                                text: t("rtk.ggaSource") + ":"
                                color: ApplicationWindow.window.text
                                font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                Layout.alignment: Qt.AlignVCenter
                            }
                            GgaSourceCombo { id: ggaSourceCombo; Layout.preferredWidth: 180 }
                            Text {
                                text: t("rtk.ggaGenerationRate") + ":"
                                color: ApplicationWindow.window.text
                                font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                Layout.alignment: Qt.AlignVCenter
                            }
                            GgaRateCombo { id: ggaRateCombo; Layout.preferredWidth: 90 }
                            Item { Layout.fillWidth: true }
                            Text {
                                text: t("rtk.ggaActualRate") + ":"
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                Layout.alignment: Qt.AlignVCenter
                            }
                            Text {
                                text: "0.0 Hz"
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                font.family: "Consolas"
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }

                        Rectangle {
                            width: parent.width; height: 1
                            color: ApplicationWindow.window.border
                            anchors.top: ggaTopRow.bottom
                            anchors.topMargin: 6
                        }

                        Text {
                            anchors.top: ggaTopRow.bottom
                            anchors.topMargin: 10
                            anchors.left: parent.left
                            text: t("rtk.ggaStream")
                            color: ApplicationWindow.window.text
                            font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                            font.bold: true
                        }

                        ScrollView {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: ggaTopRow.bottom
                            anchors.topMargin: 26
                            height: Math.round(80 * ApplicationWindow.window.scaleFactor)
                            clip: true
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            TextArea {
                                text: rtkBackend.running
                                    ? (rtkBackend.diagnostics.length > 0
                                        ? rtkBackend.diagnostics[rtkBackend.diagnostics.length - 1]
                                        : t("rtk.noDataAvailable"))
                                    : t("rtk.noDataAvailable")
                                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                                color: ApplicationWindow.window.text
                                font.family: "Consolas"
                                font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor
                                background: Rectangle { color: "transparent" }
                            }
                        }
                    }
                }

                // ── Operation Buttons ──
                Row {
                    id: operationRow; spacing: 6

                    ToolbarButton {
                        iconName: "wifi"
                        height: 30
                        text: t("rtk.start"); enabled: !rtkBackend.running; variant: "primary"
                        onClicked: {
                            rtkBackend.timeoutMs = Math.max(1, Number(page.uiTimeoutMs))
                            rtkBackend.reconnectMs = Math.max(1, Number(page.uiReconnectMs))
                            if (ggaRateCombo.currentIndex >= 0)
                                rtkBackend.ggaGenerationRateHz = page.ggaRateModel[ggaRateCombo.currentIndex].value
                            rtkBackend.start()
                        }
                    }
                    ToolbarButton {
                        iconName: "square"
                        height: 30
                        text: t("rtk.stop"); enabled: rtkBackend.running;
                        onClicked: rtkBackend.stop()
                    }
                    ToolbarButton {
                        iconName: "flask-conical"
                        height: 30
                        text: rtkBackend.testingConnection ? t("rtk.testingConnection") : t("rtk.testConnection")
                        enabled: !rtkBackend.testingConnection
                        onClicked: rtkBackend.testConnection()
                    }
                    ToolbarButton {
                        iconName: "trash-2"
                        height: 30
                        text: t("rtk.clearLog")
                        onClicked: rtkBackend.clearDiagnostics()
                    }
                    ToolbarButton {
                        iconName: "save"
                        height: 30
                        text: t("rtk.saveConfig")
                        onClicked: rtkBackend.saveConfig()
                    }
                    ToolbarButton {
                        iconName: "folder-open"
                        height: 30
                        text: t("rtk.loadConfig")
                        onClicked: {
                            rtkBackend.loadConfig()
                            page.uiTimeoutMs = String(rtkBackend.timeoutMs)
                            page.uiReconnectMs = String(rtkBackend.reconnectMs)
                        }
                    }
                }

                // ── RTK Service Log / Diagnostics ──
                Card {
                    id: diagCard
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(260, leftColumn.implicitHeight * 0.52)
                    Layout.minimumHeight: 240
                    title: t("rtk.serviceLog")

                    RtkCardFix {
                        MetricText { anchors.left: parent.left; anchors.top: parent.top; label: t("rtk.rtcmThroughput"); value: (page.statNumber("inputBps",0) + page.statNumber("outputBps",0)) + " B/s" }
                        MetricText { anchors.left: parent.left; anchors.leftMargin: 170; anchors.top: parent.top; label: t("rtk.ggaUpdateTime"); value: rtkBackend.running ? "0.0 s" : "---" }
                        MetricText { anchors.left: parent.left; anchors.leftMargin: 330; anchors.top: parent.top; label: t("rtk.diffStatus"); value: page.diffStatusText(); valueColor: page.diffStatusColor() }
                        MetricText { anchors.left: parent.left; anchors.leftMargin: 470; anchors.top: parent.top; label: t("rtk.latency"); value: "--- ms" }

                        Text {
                            anchors.top: parent.top
                            anchors.topMargin: 38
                            anchors.left: parent.left
                            text: t("rtk.diagLog")
                            color: ApplicationWindow.window.text
                            font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                            font.bold: true
                        }

                        ListView {
                            id: diagnosticsListView
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.topMargin: 56
                            anchors.bottom: parent.bottom
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            model: diagnosticLogModel
                            spacing: 2
                            ScrollBar.vertical: ScrollBar { id: logVBar }

                            delegate: TextEdit {
                                required property string entry

                                width: diagnosticsListView.width
                                text: entry
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.Wrap
                                color: ApplicationWindow.window.text
                                selectedTextColor: ApplicationWindow.window.primaryForeground
                                selectionColor: ApplicationWindow.window.primary
                                font.family: "Consolas"
                                font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor
                                textFormat: TextEdit.PlainText
                            }

                            onMovementStarted: page.diagnosticLogAutoFollow = false
                            onMovementEnded: page.diagnosticLogAutoFollow = page.diagnosticLogAtEnd()
                            onContentYChanged: {
                                if (!page.diagnosticLogProgrammaticScroll && !moving && !dragging)
                                    page.diagnosticLogAutoFollow = page.diagnosticLogAtEnd()
                            }

                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                visible: diagnosticLogModel.count === 0
                                text: t("rtk.noDiagnosticLog")
                                color: ApplicationWindow.window.muted
                                font.family: "Consolas"
                                font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Diagnostics auto-scroll ──
    Timer {
        id: logScrollTimer
        interval: 50
        onTriggered: {
            page.diagnosticLogProgrammaticScroll = true
            diagnosticsListView.positionViewAtEnd()
            Qt.callLater(function() { page.diagnosticLogProgrammaticScroll = false })
        }
    }
    Connections {
        target: rtkBackend
        function onDiagnosticsChanged() { page.syncDiagnosticLogModel() }
    }
    Connections {
        target: rtkBackend
        function onConfigChanged() {
            page.uiTimeoutMs = String(rtkBackend.timeoutMs)
            page.uiReconnectMs = String(rtkBackend.reconnectMs)
        }
    }
    Connections {
        target: deviceBackend
        function onPortsChanged() { page.refreshGgaSourceModel() }
    }
    Connections {
        target: appBackend
        function onLanguageChanged() { page.refreshGgaSourceModel() }
    }

    // ═══════════════════════════════════════════════
    // Reusable inline components
    // ═══════════════════════════════════════════════

    component RtkLabel: Text {
        color: ApplicationWindow.window.muted
        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
    }

    // Content container for natural-height cards — x=12, y=10, width minus 2*12
    component RtkCardColumn: ColumnLayout {
        id: col
        width: parent ? parent.width - 24 : 0
        x: 12; y: 10
        spacing: 8
    }

    // Fixed-height content wrapper — fills the card body with 12px horizontal + 10px vertical paddding
    component RtkCardFix: Item {
        id: fix
        x: 12; y: 10
        width: parent ? parent.width - 24 : 0
        height: parent ? parent.height - 20 : 0
    }

    component MetricText: Column { property string label: ""; property string value: ""; property color valueColor: ApplicationWindow.window.text; spacing: 2; width: 150
        Text { text: parent.label; color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
        Text { text: parent.value; color: parent.valueColor; font.pixelSize: ApplicationWindow.window.uiValueFontSize * ApplicationWindow.window.scaleFactor; font.bold: true; font.family: "Consolas" } }

    component RtkTextField: TextField {
        id: field
        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
        color: ApplicationWindow.window.text
        selectedTextColor: ApplicationWindow.window.primaryForeground
        selectionColor: ApplicationWindow.window.primary
        placeholderTextColor: ApplicationWindow.window.muted
        leftPadding: 10; rightPadding: 10
        background: Rectangle {
            implicitHeight: 34; radius: 7
            color: field.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card
            border.color: field.activeFocus
                ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8")
                : ApplicationWindow.window.border
        }
    }

    component RtkMountPointCombo: AppEditableComboBox {
        id: mpCombo
        model: page.mountPointModel
        text: rtkBackend.mountpoint && rtkBackend.mountpoint.length > 0 ? rtkBackend.mountpoint : "AUTO"
        acceptEmptyInput: true

        Connections {
            target: rtkBackend
            function onConfigChanged() { mpCombo.syncFromBackend() }
            function onMountPointOptionsChanged() { mpCombo.syncFromBackend() }
        }

        function syncFromBackend() {
            mpCombo.text = rtkBackend.mountpoint && rtkBackend.mountpoint.length > 0 ? rtkBackend.mountpoint : "AUTO"
        }

        function commitMountPoint(value) {
            var v = String(value || mpCombo.text || "").trim()
            if (v.length === 0) v = "AUTO"
            if (rtkBackend.mountpoint !== v) rtkBackend.setMountpoint(v)
            if (mpCombo.text !== v) mpCombo.text = v
        }

        onAccepted: function(text) { commitMountPoint(text) }
    }

    component RtkPortComboBox: AppComboBox {
        id: portCombo
        model: rtkBackend.outputPortOptions
        textRole: "label"
        valueRole: "port"
        displayRoleName: "label"
        currentIndex: {
            var opts = rtkBackend.outputPortOptions
            for (var i = 0; i < opts.length; ++i)
                if (opts[i].port === rtkBackend.outputPort) return i
            return -1
        }
        onActivated: {
            var opts = rtkBackend.outputPortOptions
            if (portCombo.currentIndex >= 0 && portCombo.currentIndex < opts.length)
                rtkBackend.setOutputPort(opts[portCombo.currentIndex].port)
        }

        delegate: ItemDelegate {
            width: portCombo.width
            height: ApplicationWindow.window.uiControlHeight
            text: portCombo.displayFor(modelData)
            font.family: "Consolas"
            font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor
            background: Rectangle { radius: ApplicationWindow.window.uiRadius; color: hovered ? ApplicationWindow.window.secondary : "transparent" }
            contentItem: Text {
                text: parent.text
                color: (modelData && modelData.occupied) ? ApplicationWindow.window.muted : ApplicationWindow.window.text
                font: parent.font; verticalAlignment: Text.AlignVCenter; leftPadding: 10; rightPadding: 10; elide: Text.ElideRight
            }
        }
    }

    component GgaSourceCombo: AppComboBox {
        id: ggaCombo
        model: page.ggaSourceModel
        currentIndex: Math.min(page.ggaSourceIndex, Math.max(0, page.ggaSourceModel.length - 1))
        textRole: "text"
        displayRoleName: "text"
        onActivated: page.ggaSourceIndex = currentIndex
    }

    component GgaRateCombo: AppComboBox {
        id: rateCombo
        model: page.ggaRateModel
        textRole: "text"
        displayRoleName: "text"
        Component.onCompleted: {
            var found = false
            for (var i = 0; i < page.ggaRateModel.length; ++i) {
                if (Number(page.ggaRateModel[i].value) === Number(rtkBackend.ggaGenerationRateHz)) {
                    rateCombo.currentIndex = i; found = true; break
                }
            }
            if (!found) rateCombo.currentIndex = 0
        }
        onActivated: {
            if (rateCombo.currentIndex >= 0)
                rtkBackend.ggaGenerationRateHz = page.ggaRateModel[rateCombo.currentIndex].value
        }
    }
}
