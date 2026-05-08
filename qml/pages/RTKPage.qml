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

    function diagnosticsText() {
        var lines = rtkBackend.diagnostics || []
        return lines.length > 0 ? lines.join("\n") : ""
    }

    property var ggaSourceModel: [
        { text: t("rtk.ggaSourceEpsilonMain"), value: "epsilon_main" },
        { text: t("rtk.ggaSourceManual"), value: "manual" },
        { text: t("rtk.ggaSourceExternalNetwork"), value: "external" },
    ]
    property int ggaSourceIndex: 0

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
                            text: t("rtk.applyLeverArm"); iconName: "arrow-down-up"
                            variant: "primary"
                            onClicked: rtkBackend.applyMainAntennaLeverArm(
                                Number(leverX.text), Number(leverY.text), Number(leverZ.text))
                            background: Rectangle {
                                implicitHeight: 28; radius: 8
                                color: "#1e293b"
                                border.color: ApplicationWindow.window.border
                            }
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
                            anchors.bottom: parent.bottom
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
                                font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
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
                        text: t("rtk.stop"); enabled: rtkBackend.running; variant: "danger"
                        onClicked: rtkBackend.stop()
                    }
                    ToolbarButton {
                        iconName: "scan"
                        height: 30
                        text: t("rtk.testConnection")
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

                        ScrollView {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.topMargin: 56
                            anchors.bottom: parent.bottom
                            clip: true
                            ScrollBar.vertical: ScrollBar { id: logVBar }

                            TextArea {
                                text: page.diagnosticsText() ? page.diagnosticsText() : t("rtk.noDiagnosticLog")
                                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                                color: ApplicationWindow.window.text
                                selectedTextColor: ApplicationWindow.window.primaryForeground
                                selectionColor: ApplicationWindow.window.primary
                                font.family: "Consolas"
                                font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                                background: Rectangle { color: "transparent" }
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
        onTriggered: logVBar.position = 1.0 - logVBar.size
    }
    Connections {
        target: rtkBackend
        function onDiagnosticsChanged() { logScrollTimer.restart() }
    }
    Connections {
        target: rtkBackend
        function onConfigChanged() {
            page.uiTimeoutMs = String(rtkBackend.timeoutMs)
            page.uiReconnectMs = String(rtkBackend.reconnectMs)
        }
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
        Text { text: parent.label; color: ApplicationWindow.window.muted; font.pixelSize: 10 * ApplicationWindow.window.scaleFactor }
        Text { text: parent.value; color: parent.valueColor; font.pixelSize: 12 * ApplicationWindow.window.scaleFactor; font.bold: true; font.family: "Consolas" } }

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

    component RtkMountPointCombo: ComboBox {
        id: mpCombo
        editable: true
        model: rtkBackend.mountPointOptions
        Component.onCompleted: mpCombo.editText = Qt.binding(function() { return rtkBackend.mountpoint })
        onActivated: rtkBackend.setMountpoint(mpCombo.editText)
        onAccepted: rtkBackend.setMountpoint(mpCombo.editText)

        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
        implicitHeight: 34

        delegate: ItemDelegate {
            width: mpCombo.width; text: modelData
            font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
            highlighted: mpCombo.highlightedIndex === index
            background: Rectangle { color: highlighted ? ApplicationWindow.window.secondary : "transparent" }
            contentItem: Text {
                text: modelData; color: ApplicationWindow.window.text
                font: parent.font; verticalAlignment: Text.AlignVCenter; leftPadding: 8
            }
        }
        indicator: Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right; anchors.rightMargin: 8
            text: "▾"; color: ApplicationWindow.window.muted; font.pixelSize: 10
        }
        contentItem: TextField {
            leftPadding: 10; rightPadding: 24
            text: mpCombo.editText; color: ApplicationWindow.window.text
            font: mpCombo.font; verticalAlignment: Text.AlignVCenter; background: null
        }
        background: Rectangle {
            implicitHeight: 34; radius: 7
            color: mpCombo.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card
            border.color: mpCombo.activeFocus
                ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8")
                : ApplicationWindow.window.border
        }
        popup: RtkPopup { popupWidth: mpCombo.width; model: mpCombo.delegateModel; index: mpCombo.highlightedIndex }
    }

    component RtkPortComboBox: ComboBox {
        id: portCombo
        model: rtkBackend.outputPortOptions; textRole: "label"; valueRole: "port"
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
        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
        implicitHeight: 34

        delegate: ItemDelegate {
            width: portCombo.width
            text: modelData ? modelData.label || modelData.port || "" : ""
            font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
            highlighted: portCombo.highlightedIndex === index
            background: Rectangle { color: highlighted ? ApplicationWindow.window.secondary : "transparent" }
            contentItem: Text {
                text: parent.text
                color: (modelData && modelData.occupied) ? ApplicationWindow.window.muted : ApplicationWindow.window.text
                font: parent.font; verticalAlignment: Text.AlignVCenter; leftPadding: 8
            }
        }
        indicator: Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right; anchors.rightMargin: 8
            text: "▾"; color: ApplicationWindow.window.muted; font.pixelSize: 10
        }
        contentItem: Text {
            leftPadding: 10; rightPadding: 24
            text: portCombo.displayText; color: ApplicationWindow.window.text
            font: portCombo.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        background: Rectangle {
            implicitHeight: 34; radius: 7
            color: portCombo.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card
            border.color: portCombo.activeFocus
                ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8")
                : ApplicationWindow.window.border
        }
        popup: RtkPopup { popupWidth: portCombo.width; model: portCombo.delegateModel; index: portCombo.highlightedIndex }
    }

    component GgaSourceCombo: ComboBox {
        id: ggaCombo
        model: page.ggaSourceModel; currentIndex: page.ggaSourceIndex; textRole: "text"
        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
        implicitHeight: 34; onActivated: page.ggaSourceIndex = currentIndex
        delegate: ItemDelegate {
            width: ggaCombo.width
            text: ggaCombo.textRole ? model[ggaCombo.textRole] : modelData
            font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
            highlighted: ggaCombo.highlightedIndex === index
            background: Rectangle { color: highlighted ? ApplicationWindow.window.secondary : "transparent" }
            contentItem: Text {
                text: parent.text; color: ApplicationWindow.window.text
                font: parent.font; verticalAlignment: Text.AlignVCenter; leftPadding: 8
            }
        }
        indicator: Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right; anchors.rightMargin: 8
            text: "▾"; color: ApplicationWindow.window.muted; font.pixelSize: 10
        }
        contentItem: Text {
            leftPadding: 10; rightPadding: 24
            text: ggaCombo.displayText; color: ApplicationWindow.window.text
            font: ggaCombo.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        background: Rectangle {
            implicitHeight: 34; radius: 7
            color: ggaCombo.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card
            border.color: ggaCombo.activeFocus
                ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8")
                : ApplicationWindow.window.border
        }
        popup: RtkPopup { popupWidth: ggaCombo.width; model: ggaCombo.delegateModel; index: ggaCombo.highlightedIndex }
    }

    component GgaRateCombo: ComboBox {
        id: rateCombo
        model: page.ggaRateModel; textRole: "text"
        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
        implicitHeight: 34
        Component.onCompleted: {
            for (var i = 0; i < page.ggaRateModel.length; ++i) {
                if (page.ggaRateModel[i].value === rtkBackend.ggaGenerationRateHz) {
                    rateCombo.currentIndex = i; break
                }
            }
        }
        onActivated: {
            if (rateCombo.currentIndex >= 0)
                rtkBackend.ggaGenerationRateHz = page.ggaRateModel[rateCombo.currentIndex].value
        }
        delegate: ItemDelegate {
            width: rateCombo.width
            text: modelData ? modelData.text || "" : ""
            font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
            highlighted: rateCombo.highlightedIndex === index
            background: Rectangle { color: highlighted ? ApplicationWindow.window.secondary : "transparent" }
            contentItem: Text {
                text: parent.text; color: ApplicationWindow.window.text
                font: parent.font; verticalAlignment: Text.AlignVCenter; leftPadding: 8
            }
        }
        indicator: Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right; anchors.rightMargin: 8
            text: "▾"; color: ApplicationWindow.window.muted; font.pixelSize: 10
        }
        contentItem: Text {
            leftPadding: 10; rightPadding: 24
            text: rateCombo.displayText; color: ApplicationWindow.window.text
            font: rateCombo.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
        }
        background: Rectangle {
            implicitHeight: 34; radius: 7
            color: rateCombo.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card
            border.color: rateCombo.activeFocus
                ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8")
                : ApplicationWindow.window.border
        }
        popup: RtkPopup { popupWidth: rateCombo.width; model: rateCombo.delegateModel; index: rateCombo.highlightedIndex }
    }

    component RtkPopup: Popup {
        property real popupWidth: 100
        property var model
        property int index: 0
        y: parent.height + 2; width: popupWidth
        implicitHeight: contentItem.implicitHeight + 8; padding: 4
        background: Rectangle {
            radius: 7; color: ApplicationWindow.window.card
            border.color: ApplicationWindow.window.border
        }
        contentItem: ListView {
            clip: true; implicitHeight: contentHeight
            model: parent.model
            currentIndex: parent.index
        }
    }
}
