import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    id: page

    // ── Helpers ──
    function t(key) {
        appBackend.language
        return appBackend.t(key)
    }

    function textValue(value, fallback) {
        if (value === undefined || value === null)
            return fallback || ""
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

    // GGA source model
    property var ggaSourceModel: [
        { text: t("rtk.ggaSourceEpsilonMain"), value: "epsilon_main" },
        { text: t("rtk.ggaSourceManual"), value: "manual" },
        { text: t("rtk.ggaSourceExternalNetwork"), value: "external" },
    ]
    property int ggaSourceIndex: 0

    // GGA generation rate model
    property var ggaRateModel: [
        { text: "1 Hz", value: 1 },
        { text: "2 Hz", value: 2 },
        { text: "5 Hz", value: 5 },
        { text: "10 Hz", value: 10 },
        { text: "20 Hz", value: 20 },
    ]

    // Local state for fields without backend bindings
    property string uiTimeoutMs: String(rtkBackend.timeoutMs)
    property string uiReconnectMs: String(rtkBackend.reconnectMs)
    property string uiLeverX: "0"
    property string uiLeverY: "0"
    property string uiLeverZ: "0"

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // ════════════════════════════════════════════════════════
        // LEFT COLUMN — Configuration (~1/3 width)
        // ════════════════════════════════════════════════════════
        ColumnLayout {
            Layout.preferredWidth: Math.round(parent.width * 0.34)
            Layout.minimumWidth: 340
            Layout.maximumWidth: Math.round(parent.width * 0.42)
            Layout.fillHeight: true
            spacing: 12

            // ── CARD: NTRIP Config ──
            Card {
                id: ntripCard
                Layout.fillWidth: true
                title: t("rtk.ntripConfig")

                ColumnLayout {
                    width: parent.width
                    spacing: 8

                    // Row 1: Server + Port
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text {
                                text: t("rtk.casterAddress")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkTextField {
                                Layout.fillWidth: true
                                text: page.textValue(rtkBackend.server)
                                onEditingFinished: rtkBackend.server = text
                            }
                        }
                        ColumnLayout { Layout.preferredWidth: 90; spacing: 2
                            Text {
                                text: t("rtk.port")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkTextField {
                                Layout.fillWidth: true
                                text: page.textValue(rtkBackend.port, "2101")
                                onEditingFinished: rtkBackend.port = text
                            }
                        }
                    }

                    // Row 2: Mount Point + Detect button
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text {
                                text: t("rtk.mountPoint")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkMountPointCombo {
                                Layout.fillWidth: true
                            }
                        }
                        ColumnLayout { Layout.preferredWidth: 100; spacing: 2
                            Text { text: " "; font.pixelSize: 10 * ApplicationWindow.window.scaleFactor }
                            ToolbarButton {
                                Layout.fillWidth: true
                                iconName: "search"
                                text: rtkBackend.detectingMountPoints
                                    ? t("rtk.detecting")
                                    : t("rtk.detectMountPoints")
                                enabled: !rtkBackend.detectingMountPoints
                                onClicked: rtkBackend.detectMountPoints()
                            }
                        }
                    }

                    // Row 3: Username + Password (plain text)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text {
                                text: t("rtk.username")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkTextField {
                                Layout.fillWidth: true
                                text: page.textValue(rtkBackend.username)
                                onEditingFinished: rtkBackend.username = text
                            }
                        }
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text {
                                text: t("rtk.password")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkTextField {
                                Layout.fillWidth: true
                                echoMode: TextInput.Normal
                                text: page.textValue(rtkBackend.password)
                                onEditingFinished: rtkBackend.password = text
                            }
                        }
                    }

                    // Row 4: Auto Reconnect
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        CheckBox {
                            id: autoReconnectCb
                            text: t("rtk.autoReconnect")
                            checked: rtkBackend.autoReconnect
                            onCheckedChanged: rtkBackend.autoReconnect = checked
                            contentItem: Text {
                                text: autoReconnectCb.text
                                color: ApplicationWindow.window.text
                                font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                leftPadding: autoReconnectCb.indicator.width + autoReconnectCb.spacing
                                verticalAlignment: Text.AlignVCenter
                            }
                            indicator: Rectangle {
                                implicitWidth: 16
                                implicitHeight: 16
                                radius: 4
                                color: autoReconnectCb.checked ? ApplicationWindow.window.primary : "transparent"
                                border.color: autoReconnectCb.checked ? ApplicationWindow.window.primary : ApplicationWindow.window.border
                            }
                        }
                    }

                    // Row 5: Test Connection + Save Config (inside NTRIP card per Dyad)
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ToolbarButton {
                            Layout.fillWidth: true
                            iconName: "scan"
                            text: t("rtk.testConnection")
                            onClicked: rtkBackend.testConnection()
                        }
                        ToolbarButton {
                            Layout.fillWidth: true
                            iconName: "save"
                            text: t("rtk.saveConfig")
                            onClicked: rtkBackend.saveConfig()
                        }
                    }
                }
            }

            // ── CARD: RTCM Output Config ──
            Card {
                id: rtcmCard
                Layout.fillWidth: true
                title: t("rtk.rtcmOutputConfig")

                ColumnLayout {
                    width: parent.width
                    spacing: 8

                    // Row 1: Output Port + Baud Rate
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text {
                                text: t("rtk.outputPort")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkPortComboBox {
                                Layout.fillWidth: true
                            }
                        }
                        ColumnLayout { Layout.preferredWidth: 100; spacing: 2
                            Text {
                                text: t("rtk.baudRate")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkTextField {
                                Layout.fillWidth: true
                                text: page.textValue(rtkBackend.outputBaud, "115200")
                                inputMethodHints: Qt.ImhDigitsOnly
                                onEditingFinished: rtkBackend.outputBaud = Math.max(1, Number(text))
                            }
                        }
                    }

                    // Row 2: Lever Arm X/Y/Z + Apply button
                    Text {
                        text: t("rtk.leverArm")
                        color: ApplicationWindow.window.muted
                        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Item { Layout.fillWidth: true }
                        Text {
                            text: "X"
                            color: ApplicationWindow.window.text
                            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            font.bold: true
                            Layout.alignment: Qt.AlignVCenter
                        }
                        RtkTextField {
                            id: leverX
                            Layout.preferredWidth: 64
                            text: page.uiLeverX
                            validator: DoubleValidator { bottom: -10000; top: 10000; decimals: 4 }
                            onEditingFinished: page.uiLeverX = text
                        }
                        Text {
                            text: "Y"
                            color: ApplicationWindow.window.text
                            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            font.bold: true
                            Layout.alignment: Qt.AlignVCenter
                        }
                        RtkTextField {
                            id: leverY
                            Layout.preferredWidth: 64
                            text: page.uiLeverY
                            validator: DoubleValidator { bottom: -10000; top: 10000; decimals: 4 }
                            onEditingFinished: page.uiLeverY = text
                        }
                        Text {
                            text: "Z"
                            color: ApplicationWindow.window.text
                            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            font.bold: true
                            Layout.alignment: Qt.AlignVCenter
                        }
                        RtkTextField {
                            id: leverZ
                            Layout.preferredWidth: 64
                            text: page.uiLeverZ
                            validator: DoubleValidator { bottom: -10000; top: 10000; decimals: 4 }
                            onEditingFinished: page.uiLeverZ = text
                        }
                        Item { Layout.fillWidth: true }
                    }
                    ToolbarButton {
                        Layout.fillWidth: true
                        text: t("rtk.applyLeverArm")
                        iconName: "activity"
                        onClicked: rtkBackend.applyMainAntennaLeverArm(
                            Number(leverX.text), Number(leverY.text), Number(leverZ.text))
                    }

                    // Row 3: Timeout + Reconnect
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text {
                                text: t("rtk.timeoutMs")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkTextField {
                                Layout.fillWidth: true
                                text: page.uiTimeoutMs
                                inputMethodHints: Qt.ImhDigitsOnly
                                onEditingFinished: {
                                    page.uiTimeoutMs = text
                                    rtkBackend.timeoutMs = Math.max(1, Number(text))
                                }
                                Component.onCompleted: {
                                    page.uiTimeoutMs = String(rtkBackend.timeoutMs)
                                }
                            }
                        }
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text {
                                text: t("rtk.reconnectMs")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkTextField {
                                Layout.fillWidth: true
                                text: page.uiReconnectMs
                                inputMethodHints: Qt.ImhDigitsOnly
                                onEditingFinished: {
                                    page.uiReconnectMs = text
                                    rtkBackend.reconnectMs = Math.max(1, Number(text))
                                }
                                Component.onCompleted: {
                                    page.uiReconnectMs = String(rtkBackend.reconnectMs)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ════════════════════════════════════════════════════════
        // RIGHT COLUMN — Monitoring & Diagnostics (~2/3 width)
        // ════════════════════════════════════════════════════════
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // ── CARD: GGA Monitor ──
            Card {
                id: ggaCard
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(200, parent.height * 0.32)
                Layout.minimumHeight: 160
                title: t("rtk.ggaMonitor")

                ColumnLayout {
                    width: parent.width
                    spacing: 6

                    // Row: GGA Source + Generation Rate + Actual Rate
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: t("rtk.ggaSource") + ":"
                            color: ApplicationWindow.window.text
                            font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                            Layout.alignment: Qt.AlignVCenter
                        }

                        GgaSourceCombo {
                            id: ggaSourceCombo
                            Layout.preferredWidth: 180
                        }

                        Text {
                            text: t("rtk.ggaGenerationRate") + ":"
                            color: ApplicationWindow.window.text
                            font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                            Layout.alignment: Qt.AlignVCenter
                        }

                        GgaRateCombo {
                            id: ggaRateCombo
                            Layout.preferredWidth: 90
                        }

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

                    // Divider
                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: ApplicationWindow.window.border
                    }

                    // GGA Stream label
                    Text {
                        text: t("rtk.ggaStream")
                        color: ApplicationWindow.window.text
                        font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                        font.bold: true
                    }

                    // GGA content area
                    ScrollView {
                        id: ggaScrollView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

                        TextArea {
                            text: rtkBackend.running
                                ? (rtkBackend.diagnostics.length > 0
                                    ? rtkBackend.diagnostics[rtkBackend.diagnostics.length - 1]
                                    : t("rtk.noDataAvailable"))
                                : t("rtk.noDataAvailable")
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
                            color: ApplicationWindow.window.text
                            font.family: "Consolas"
                            font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                            background: Rectangle { color: "transparent" }
                        }
                    }
                }
            }

            // ── OPERATION BUTTONS ROW ──
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "wifi"
                    text: t("rtk.start")
                    enabled: !rtkBackend.running
                    variant: "primary"
                    onClicked: {
                        rtkBackend.timeoutMs = Math.max(1, Number(page.uiTimeoutMs))
                        rtkBackend.reconnectMs = Math.max(1, Number(page.uiReconnectMs))
                        if (ggaRateCombo.currentIndex >= 0)
                            rtkBackend.ggaGenerationRateHz = page.ggaRateModel[ggaRateCombo.currentIndex].value
                        rtkBackend.start()
                    }
                }
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "square"
                    text: t("rtk.stop")
                    enabled: rtkBackend.running
                    variant: "danger"
                    onClicked: rtkBackend.stop()
                }
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "scan"
                    text: t("rtk.testConnection")
                    onClicked: rtkBackend.testConnection()
                }
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "trash-2"
                    text: t("rtk.clearLog")
                    onClicked: rtkBackend.clearDiagnostics()
                }
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "save"
                    text: t("rtk.saveConfig")
                    onClicked: rtkBackend.saveConfig()
                }
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "folder-open"
                    text: t("rtk.loadConfig")
                    onClicked: {
                        rtkBackend.loadConfig()
                        page.uiTimeoutMs = String(rtkBackend.timeoutMs)
                        page.uiReconnectMs = String(rtkBackend.reconnectMs)
                    }
                }
            }

            // ── CARD: Diagnostics ──
            Card {
                id: diagCard
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: t("rtk.diagnostics")

                ColumnLayout {
                    width: parent.width
                    spacing: 6

                    // Metrics row: RTCM Throughput, GGA Update, Diff Status, Latency
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        // RTCM Throughput
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            radius: 6
                            color: ApplicationWindow.window.cardAlt
                            border.color: ApplicationWindow.window.border
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2
                                Text {
                                    text: t("rtk.rtcmThroughput")
                                    color: ApplicationWindow.window.muted
                                    font.pixelSize: 9 * ApplicationWindow.window.scaleFactor
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Text {
                                    text: {
                                        var inputBps = page.statNumber("inputBps", 0)
                                        var outputBps = page.statNumber("outputBps", 0)
                                        return (inputBps + outputBps) + " B/s"
                                    }
                                    color: ApplicationWindow.window.text
                                    font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                    font.family: "Consolas"
                                    font.bold: true
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }

                        // GGA Update Time
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            radius: 6
                            color: ApplicationWindow.window.cardAlt
                            border.color: ApplicationWindow.window.border
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2
                                Text {
                                    text: t("rtk.ggaUpdateTime")
                                    color: ApplicationWindow.window.muted
                                    font.pixelSize: 9 * ApplicationWindow.window.scaleFactor
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Text {
                                    text: rtkBackend.running ? "0.0 s" : "---"
                                    color: ApplicationWindow.window.text
                                    font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                    font.family: "Consolas"
                                    font.bold: true
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }

                        // Diff Status
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            radius: 6
                            color: ApplicationWindow.window.cardAlt
                            border.color: ApplicationWindow.window.border
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2
                                Text {
                                    text: t("rtk.diffStatus")
                                    color: ApplicationWindow.window.muted
                                    font.pixelSize: 9 * ApplicationWindow.window.scaleFactor
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Text {
                                    text: page.diffStatusText()
                                    color: page.diffStatusColor()
                                    font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                    font.bold: true
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }

                        // Latency
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            radius: 6
                            color: ApplicationWindow.window.cardAlt
                            border.color: ApplicationWindow.window.border
                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2
                                Text {
                                    text: t("rtk.latency")
                                    color: ApplicationWindow.window.muted
                                    font.pixelSize: 9 * ApplicationWindow.window.scaleFactor
                                    Layout.alignment: Qt.AlignHCenter
                                }
                                Text {
                                    text: "--- ms"
                                    color: ApplicationWindow.window.text
                                    font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                    font.family: "Consolas"
                                    font.bold: true
                                    Layout.alignment: Qt.AlignHCenter
                                }
                            }
                        }
                    }

                    // Diagnostic Log label
                    Text {
                        text: t("rtk.diagLog")
                        color: ApplicationWindow.window.text
                        font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                        font.bold: true
                    }

                    // Diagnostic log area
                    ScrollView {
                        id: diagScrollView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ScrollBar.vertical: ScrollBar { id: logVBar }

                        TextArea {
                            text: page.diagnosticsText()
                                ? page.diagnosticsText()
                                : t("rtk.noDiagnosticLog")
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.Wrap
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

    // Diagnostics auto-scroll timer
    Timer {
        id: logScrollTimer
        interval: 50
        onTriggered: logVBar.position = 1.0 - logVBar.size
    }

    Connections {
        target: rtkBackend
        function onDiagnosticsChanged() { logScrollTimer.restart() }
    }

    // Sync local state when config is loaded or changed externally
    Connections {
        target: rtkBackend
        function onConfigChanged() {
            page.uiTimeoutMs = String(rtkBackend.timeoutMs)
            page.uiReconnectMs = String(rtkBackend.reconnectMs)
        }
    }

    // ── Inline components ──

    component RtkTextField: TextField {
        id: field
        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
        color: ApplicationWindow.window.text
        selectedTextColor: ApplicationWindow.window.primaryForeground
        selectionColor: ApplicationWindow.window.primary
        placeholderTextColor: ApplicationWindow.window.muted
        leftPadding: 10
        rightPadding: 10
        background: Rectangle {
            implicitHeight: 34
            radius: 7
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
            width: mpCombo.width
            text: modelData
            font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
            highlighted: mpCombo.highlightedIndex === index
            background: Rectangle {
                color: highlighted ? ApplicationWindow.window.secondary : "transparent"
            }
            contentItem: Text {
                text: modelData
                color: ApplicationWindow.window.text
                font: parent.font
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
            }
        }

        indicator: Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 8
            text: "▾"
            color: ApplicationWindow.window.muted
            font.pixelSize: 10
        }

        contentItem: TextField {
            leftPadding: 10
            rightPadding: 24
            text: mpCombo.editText
            color: ApplicationWindow.window.text
            font: mpCombo.font
            verticalAlignment: Text.AlignVCenter
            background: null
        }

        background: Rectangle {
            implicitHeight: 34
            radius: 7
            color: mpCombo.hovered
                ? ApplicationWindow.window.secondary
                : ApplicationWindow.window.card
            border.color: mpCombo.activeFocus
                ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8")
                : ApplicationWindow.window.border
        }

        popup: Popup {
            y: mpCombo.height + 2
            width: mpCombo.width
            implicitHeight: contentItem.implicitHeight + 8
            padding: 4
            background: Rectangle {
                radius: 7
                color: ApplicationWindow.window.card
                border.color: ApplicationWindow.window.border
            }
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: mpCombo.delegateModel
                currentIndex: mpCombo.highlightedIndex
            }
        }
    }

    component RtkPortComboBox: ComboBox {
        id: portCombo
        model: rtkBackend.outputPortOptions
        textRole: "label"
        valueRole: "port"

        currentIndex: {
            var opts = rtkBackend.outputPortOptions
            for (var i = 0; i < opts.length; ++i) {
                if (opts[i].port === rtkBackend.outputPort)
                    return i
            }
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
            background: Rectangle {
                color: highlighted ? ApplicationWindow.window.secondary : "transparent"
            }
            contentItem: Text {
                text: modelData ? modelData.label || modelData.port || "" : ""
                color: (modelData && modelData.occupied)
                    ? ApplicationWindow.window.muted
                    : ApplicationWindow.window.text
                font: parent.font
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
            }
        }

        indicator: Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 8
            text: "▾"
            color: ApplicationWindow.window.muted
            font.pixelSize: 10
        }

        contentItem: Text {
            leftPadding: 10
            rightPadding: 24
            text: portCombo.displayText
            color: ApplicationWindow.window.text
            font: portCombo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            implicitHeight: 34
            radius: 7
            color: portCombo.hovered
                ? ApplicationWindow.window.secondary
                : ApplicationWindow.window.card
            border.color: portCombo.activeFocus
                ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8")
                : ApplicationWindow.window.border
        }

        popup: Popup {
            y: portCombo.height + 2
            width: portCombo.width
            implicitHeight: contentItem.implicitHeight + 8
            padding: 4
            background: Rectangle {
                radius: 7
                color: ApplicationWindow.window.card
                border.color: ApplicationWindow.window.border
            }
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: portCombo.delegateModel
                currentIndex: portCombo.highlightedIndex
            }
        }
    }

    component GgaSourceCombo: ComboBox {
        id: ggaCombo
        model: page.ggaSourceModel
        currentIndex: page.ggaSourceIndex
        textRole: "text"

        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
        implicitHeight: 34

        onActivated: page.ggaSourceIndex = currentIndex

        delegate: ItemDelegate {
            width: ggaCombo.width
            text: ggaCombo.textRole ? model[ggaCombo.textRole] : modelData
            font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
            highlighted: ggaCombo.highlightedIndex === index
            background: Rectangle {
                color: highlighted ? ApplicationWindow.window.secondary : "transparent"
            }
            contentItem: Text {
                text: parent.text
                color: ApplicationWindow.window.text
                font: parent.font
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
            }
        }

        indicator: Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 8
            text: "▾"
            color: ApplicationWindow.window.muted
            font.pixelSize: 10
        }

        contentItem: Text {
            leftPadding: 10
            rightPadding: 24
            text: ggaCombo.displayText
            color: ApplicationWindow.window.text
            font: ggaCombo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            implicitHeight: 34
            radius: 7
            color: ggaCombo.hovered
                ? ApplicationWindow.window.secondary
                : ApplicationWindow.window.card
            border.color: ggaCombo.activeFocus
                ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8")
                : ApplicationWindow.window.border
        }

        popup: Popup {
            y: ggaCombo.height + 2
            width: ggaCombo.width
            implicitHeight: contentItem.implicitHeight + 8
            padding: 4
            background: Rectangle {
                radius: 7
                color: ApplicationWindow.window.card
                border.color: ApplicationWindow.window.border
            }
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: ggaCombo.delegateModel
                currentIndex: ggaCombo.highlightedIndex
            }
        }
    }

    component GgaRateCombo: ComboBox {
        id: rateCombo
        model: page.ggaRateModel
        textRole: "text"

        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
        implicitHeight: 34

        Component.onCompleted: {
            for (var i = 0; i < page.ggaRateModel.length; ++i) {
                if (page.ggaRateModel[i].value === rtkBackend.ggaGenerationRateHz) {
                    rateCombo.currentIndex = i
                    break
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
            background: Rectangle {
                color: highlighted ? ApplicationWindow.window.secondary : "transparent"
            }
            contentItem: Text {
                text: parent.text
                color: ApplicationWindow.window.text
                font: parent.font
                verticalAlignment: Text.AlignVCenter
                leftPadding: 8
            }
        }

        indicator: Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.right: parent.right
            anchors.rightMargin: 8
            text: "▾"
            color: ApplicationWindow.window.muted
            font.pixelSize: 10
        }

        contentItem: Text {
            leftPadding: 10
            rightPadding: 24
            text: rateCombo.displayText
            color: ApplicationWindow.window.text
            font: rateCombo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            implicitHeight: 34
            radius: 7
            color: rateCombo.hovered
                ? ApplicationWindow.window.secondary
                : ApplicationWindow.window.card
            border.color: rateCombo.activeFocus
                ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8")
                : ApplicationWindow.window.border
        }

        popup: Popup {
            y: rateCombo.height + 2
            width: rateCombo.width
            implicitHeight: contentItem.implicitHeight + 8
            padding: 4
            background: Rectangle {
                radius: 7
                color: ApplicationWindow.window.card
                border.color: ApplicationWindow.window.border
            }
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: rateCombo.delegateModel
                currentIndex: rateCombo.highlightedIndex
            }
        }
    }
}
