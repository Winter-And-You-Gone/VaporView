import QtQuick
import QtQuick.Controls.Basic
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
        if (stats.inputBps > 0) return t("rtk.connected")
        return t("rtk.statusRunning")
    }

    function diffStatusColor() {
        if (!rtkBackend.running) return ApplicationWindow.window.muted
        var stats = rtkBackend.stats || {}
        if (stats.inputBps > 0) return ApplicationWindow.window.ok
        return ApplicationWindow.window.warning
    }

    function diagJoined() {
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

    Flickable {
        id: rtkFlick
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        readonly property int gap: 8
        readonly property int pad: 12
        readonly property real availW: width - pad
        readonly property bool narrow: availW < 980

        contentWidth: width
        contentHeight: Math.max(mainContent.height, height)

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Item {
            id: mainContent
            width: rtkFlick.availW

            readonly property real leftW: rtkFlick.narrow
                ? width
                : Math.max(400, Math.min(width * 0.33, 520))
            readonly property real rightW: rtkFlick.narrow
                ? width
                : width - leftW - rtkFlick.gap

            height: Math.max(leftColumn.height, rightColumn.height)

            // ═══════ LEFT — configuration ═══════
            Column {
                id: leftColumn
                x: 0; y: 0
                width: mainContent.leftW
                spacing: rtkFlick.gap

                Card {
                    id: ntripCard
                    width: parent.width
                    height: implicitHeight
                    title: t("rtk.ntripConfig")

                    RtkCardColumn {
                        spacing: 8

                        Row {
                            width: parent.width; spacing: 8
                            Column { width: parent.width - 96; spacing: 2
                                RtkLabel { text: t("rtk.casterAddress") }
                                RtkTextField {
                                    width: parent.width
                                    text: page.textValue(rtkBackend.server)
                                    onEditingFinished: rtkBackend.server = text
                                }
                            }
                            Column { width: 88; spacing: 2
                                RtkLabel { text: t("rtk.port") }
                                RtkTextField {
                                    width: parent.width
                                    text: page.textValue(rtkBackend.port, "2101")
                                    onEditingFinished: rtkBackend.port = text
                                }
                            }
                        }

                        Row {
                            width: parent.width; spacing: 8
                            Column { width: parent.width - 108; spacing: 2
                                RtkLabel { text: t("rtk.mountPoint") }
                                RtkMountPointCombo { width: parent.width }
                            }
                            Column { width: 100; spacing: 2
                                RtkLabel { text: " " }
                                ToolbarButton {
                                    width: parent.width
                                    iconName: "search"
                                    text: rtkBackend.detectingMountPoints
                                        ? t("rtk.detecting") : t("rtk.detectMountPoints")
                                    enabled: !rtkBackend.detectingMountPoints
                                    onClicked: rtkBackend.detectMountPoints()
                                }
                            }
                        }

                        Row {
                            width: parent.width; spacing: 8
                            Column { width: (parent.width - 8) / 2; spacing: 2
                                RtkLabel { text: t("rtk.username") }
                                RtkTextField {
                                    width: parent.width
                                    text: page.textValue(rtkBackend.username)
                                    onEditingFinished: rtkBackend.username = text
                                }
                            }
                            Column { width: (parent.width - 8) / 2; spacing: 2
                                RtkLabel { text: t("rtk.password") }
                                RtkTextField {
                                    width: parent.width
                                    echoMode: TextInput.Normal
                                    text: page.textValue(rtkBackend.password)
                                    onEditingFinished: rtkBackend.password = text
                                }
                            }
                        }

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
                                implicitWidth: 16; implicitHeight: 16; radius: 4
                                color: autoReconnectCb.checked ? ApplicationWindow.window.primary : "transparent"
                                border.color: autoReconnectCb.checked ? ApplicationWindow.window.primary : ApplicationWindow.window.border
                            }
                        }

                        Row {
                            width: parent.width; spacing: 8
                            ToolbarButton {
                                width: (parent.width - 8) / 2; iconName: "scan"
                                text: t("rtk.testConnection")
                                onClicked: rtkBackend.testConnection()
                            }
                            ToolbarButton {
                                width: (parent.width - 8) / 2; iconName: "save"
                                text: t("rtk.saveConfig")
                                onClicked: rtkBackend.saveConfig()
                            }
                        }
                    }
                }

                Card {
                    id: rtcmCard
                    width: parent.width
                    height: implicitHeight
                    title: t("rtk.rtcmOutputConfig")

                    RtkCardColumn {
                        spacing: 8

                        Row {
                            width: parent.width; spacing: 8
                            Column { width: (parent.width - 8) * 0.6; spacing: 2
                                RtkLabel { text: t("rtk.outputPort") }
                                RtkPortComboBox { width: parent.width }
                            }
                            Column { width: (parent.width - 8) * 0.4; spacing: 2
                                RtkLabel { text: t("rtk.baudRate") }
                                RtkTextField {
                                    width: parent.width
                                    text: page.textValue(rtkBackend.outputBaud, "115200")
                                    inputMethodHints: Qt.ImhDigitsOnly
                                    onEditingFinished: rtkBackend.outputBaud = Math.max(1, Number(text))
                                }
                            }
                        }

                        RtkLabel { text: t("rtk.leverArm") + " (X, Y, Z)" }

                        Row {
                            width: parent.width; spacing: 8
                            LeverInput { label: "X"; text: page.uiLeverX; onEdit: page.uiLeverX = value; w: (parent.width - 16) / 3 }
                            LeverInput { label: "Y"; text: page.uiLeverY; onEdit: page.uiLeverY = value; w: (parent.width - 16) / 3 }
                            LeverInput { label: "Z"; text: page.uiLeverZ; onEdit: page.uiLeverZ = value; w: (parent.width - 16) / 3 }
                        }
                        ToolbarButton {
                            width: parent.width
                            text: t("rtk.applyLeverArm"); iconName: "activity"
                            onClicked: rtkBackend.applyMainAntennaLeverArm(
                                Number(leverXInput.text), Number(leverYInput.text), Number(leverZInput.text))
                        }

                        Row {
                            width: parent.width; spacing: 8
                            Column { width: (parent.width - 8) / 2; spacing: 2
                                RtkLabel { text: t("rtk.timeoutMs") }
                                RtkTextField {
                                    width: parent.width
                                    text: page.uiTimeoutMs; inputMethodHints: Qt.ImhDigitsOnly
                                    onEditingFinished: page.uiTimeoutMs = text
                                    Component.onCompleted: page.uiTimeoutMs = String(rtkBackend.timeoutMs)
                                }
                            }
                            Column { width: (parent.width - 8) / 2; spacing: 2
                                RtkLabel { text: t("rtk.reconnectMs") }
                                RtkTextField {
                                    width: parent.width
                                    text: page.uiReconnectMs; inputMethodHints: Qt.ImhDigitsOnly
                                    onEditingFinished: page.uiReconnectMs = text
                                    Component.onCompleted: page.uiReconnectMs = String(rtkBackend.reconnectMs)
                                }
                            }
                        }
                    }
                }
            }

            // ═══════ RIGHT — monitoring ═══════
            Column {
                id: rightColumn
                x: rtkFlick.narrow ? 0 : mainContent.leftW + rtkFlick.gap
                y: rtkFlick.narrow ? leftColumn.height + rtkFlick.gap : 0
                width: mainContent.rightW
                spacing: rtkFlick.gap

                Card {
                    id: ggaCard
                    width: parent.width
                    height: Math.max(210, Math.min(270, leftColumn.height * 0.42))
                    title: t("rtk.ggaMonitor")

                    RtkCardFix {
                        Row {
                            id: ggaTopRow
                            width: parent.width; spacing: 8

                            Text {
                                text: t("rtk.ggaSource") + ":"
                                color: ApplicationWindow.window.text
                                font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            GgaSourceCombo { id: ggaSourceCombo; width: 170 }

                            Text {
                                text: t("rtk.ggaGenerationRate") + ":"
                                color: ApplicationWindow.window.text
                                font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            GgaRateCombo { id: ggaRateCombo; width: 90 }

                            Item { width: 20 }

                            Text {
                                text: t("rtk.ggaActualRate") + ":"
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Text {
                                text: "0.0 Hz"
                                color: ApplicationWindow.window.ok
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                font.family: "Consolas"
                                anchors.verticalCenter: parent.verticalCenter
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
                            text: t("rtk.ggaStream")
                            color: ApplicationWindow.window.text
                            font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                            font.bold: true
                        }

                        Rectangle {
                            anchors.top: ggaTopRow.bottom
                            anchors.topMargin: 28
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            radius: 6
                            color: ApplicationWindow.window.secondary

                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: 8
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
                }

                Row {
                    id: operationRow
                    width: parent.width; spacing: 6

                    ToolbarButton {
                        iconName: "wifi"; height: 30
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
                        iconName: "square"; height: 30
                        text: t("rtk.stop"); enabled: rtkBackend.running; variant: "danger"
                        onClicked: rtkBackend.stop()
                    }
                    ToolbarButton {
                        iconName: "scan"; height: 30
                        text: t("rtk.testConnection")
                        onClicked: rtkBackend.testConnection()
                    }
                    ToolbarButton {
                        iconName: "trash-2"; height: 30
                        text: t("rtk.clearLog")
                        onClicked: rtkBackend.clearDiagnostics()
                    }
                    ToolbarButton {
                        iconName: "save"; height: 30
                        text: t("rtk.saveConfig")
                        onClicked: rtkBackend.saveConfig()
                    }
                    ToolbarButton {
                        iconName: "folder-open"; height: 30
                        text: t("rtk.loadConfig")
                        onClicked: {
                            rtkBackend.loadConfig()
                            page.uiTimeoutMs = String(rtkBackend.timeoutMs)
                            page.uiReconnectMs = String(rtkBackend.reconnectMs)
                        }
                    }
                }

                Card {
                    id: logCard
                    width: parent.width
                    height: Math.max(260, leftColumn.height - ggaCard.height - operationRow.height - rightColumn.spacing * 2)
                    title: t("rtk.serviceLog")

                    RtkCardFix {
                        MetricText {
                            anchors.left: parent.left; anchors.top: parent.top
                            label: t("rtk.rtcmThroughput")
                            value: (page.statNumber("inputBps", 0) + page.statNumber("outputBps", 0)) + " B/s"
                        }
                        MetricText {
                            anchors.left: parent.left; anchors.leftMargin: 170; anchors.top: parent.top
                            label: t("rtk.ggaUpdateTime")
                            value: rtkBackend.running ? "0.0 s" : "---"
                        }
                        MetricText {
                            anchors.left: parent.left; anchors.leftMargin: 330; anchors.top: parent.top
                            label: t("rtk.diffStatus")
                            value: page.diffStatusText()
                            valueColor: page.diffStatusColor()
                        }
                        MetricText {
                            anchors.left: parent.left; anchors.leftMargin: 470; anchors.top: parent.top
                            label: t("rtk.latency")
                            value: "--- ms"
                        }

                        Text {
                            anchors.top: parent.top; anchors.topMargin: 38
                            anchors.left: parent.left
                            text: t("rtk.diagLog")
                            color: ApplicationWindow.window.text
                            font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                            font.bold: true
                        }

                        ScrollView {
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.top: parent.top; anchors.topMargin: 56
                            anchors.bottom: parent.bottom
                            clip: true
                            ScrollBar.vertical: ScrollBar { id: logVBar }

                            TextArea {
                                text: page.diagJoined() ? page.diagJoined() : t("rtk.noDiagnosticLog")
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

    Timer {
        id: logScrollTimer; interval: 50
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

    // ═══════════════════════════════════════
    // Components
    // ═══════════════════════════════════════

    component RtkLabel: Text {
        color: ApplicationWindow.window.muted
        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
    }

    component RtkCardColumn: Column {
        x: 12; y: 10
        width: parent ? parent.width - 24 : 0
        height: implicitHeight
    }

    component RtkCardFix: Item {
        x: 12; y: 10
        width: parent ? parent.width - 24 : 0
        height: parent ? parent.height - 20 : 0
    }

    component MetricText: Column {
        property string label: ""
        property string value: ""
        property color valueColor: ApplicationWindow.window.text
        spacing: 2; width: 150

        Text {
            text: parent.label
            color: ApplicationWindow.window.muted
            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
        }
        Text {
            text: parent.value
            color: parent.valueColor
            font.pixelSize: 12 * ApplicationWindow.window.scaleFactor
            font.bold: true; font.family: "Consolas"
        }
    }

    component LeverInput: Column {
        property string label: ""
        property alias text: input.text
        signal edit(string value)
        property real w: 100
        spacing: 2; width: w

        RtkLabel { text: parent.label }
        RtkTextField {
            id: input; width: parent.width
            text: parent.parent.text
            validator: DoubleValidator { bottom: -10000; top: 10000; decimals: 4 }
            onEditingFinished: parent.edit(text)
        }
    }

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
        editable: true; model: rtkBackend.mountPointOptions
        Component.onCompleted: mpCombo.editText = Qt.binding(function() { return rtkBackend.mountpoint })
        onActivated: rtkBackend.setMountpoint(mpCombo.editText)
        onAccepted: rtkBackend.setMountpoint(mpCombo.editText)
        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor); implicitHeight: 34
        delegate: ItemDelegate {
            width: mpCombo.width; text: modelData
            font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
            highlighted: mpCombo.highlightedIndex === index
            background: Rectangle { color: highlighted ? ApplicationWindow.window.secondary : "transparent" }
            contentItem: Text { text: modelData; color: ApplicationWindow.window.text; font: parent.font; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
        }
        indicator: Text { anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 8; text: "▾"; color: ApplicationWindow.window.muted; font.pixelSize: 10 }
        contentItem: TextField { leftPadding: 10; rightPadding: 24; text: mpCombo.editText; color: ApplicationWindow.window.text; font: mpCombo.font; verticalAlignment: Text.AlignVCenter; background: null }
        background: Rectangle { implicitHeight: 34; radius: 7; color: mpCombo.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card; border.color: mpCombo.activeFocus ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8") : ApplicationWindow.window.border }
        popup: RtkPopup { popupWidth: mpCombo.width; model: mpCombo.delegateModel; index: mpCombo.highlightedIndex }
    }

    component RtkPortComboBox: ComboBox {
        id: portCombo
        model: rtkBackend.outputPortOptions; textRole: "label"; valueRole: "port"
        currentIndex: { var opts = rtkBackend.outputPortOptions; for (var i = 0; i < opts.length; ++i) if (opts[i].port === rtkBackend.outputPort) return i; return -1 }
        onActivated: { var opts = rtkBackend.outputPortOptions; if (portCombo.currentIndex >= 0 && portCombo.currentIndex < opts.length) rtkBackend.setOutputPort(opts[portCombo.currentIndex].port) }
        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor); implicitHeight: 34
        delegate: ItemDelegate {
            width: portCombo.width; text: modelData ? modelData.label || modelData.port || "" : ""
            font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor); highlighted: portCombo.highlightedIndex === index
            background: Rectangle { color: highlighted ? ApplicationWindow.window.secondary : "transparent" }
            contentItem: Text { text: parent.text; color: (modelData && modelData.occupied) ? ApplicationWindow.window.muted : ApplicationWindow.window.text; font: parent.font; verticalAlignment: Text.AlignVCenter; leftPadding: 8 }
        }
        indicator: Text { anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 8; text: "▾"; color: ApplicationWindow.window.muted; font.pixelSize: 10 }
        contentItem: Text { leftPadding: 10; rightPadding: 24; text: portCombo.displayText; color: ApplicationWindow.window.text; font: portCombo.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle { implicitHeight: 34; radius: 7; color: portCombo.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card; border.color: portCombo.activeFocus ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8") : ApplicationWindow.window.border }
        popup: RtkPopup { popupWidth: portCombo.width; model: portCombo.delegateModel; index: portCombo.highlightedIndex }
    }

    component GgaSourceCombo: ComboBox {
        id: ggaCombo
        model: page.ggaSourceModel; currentIndex: page.ggaSourceIndex; textRole: "text"
        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor); implicitHeight: 34; onActivated: page.ggaSourceIndex = currentIndex
        delegate: ItemDelegate { width: ggaCombo.width; text: ggaCombo.textRole ? model[ggaCombo.textRole] : modelData; font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor); highlighted: ggaCombo.highlightedIndex === index; background: Rectangle { color: highlighted ? ApplicationWindow.window.secondary : "transparent" }; contentItem: Text { text: parent.text; color: ApplicationWindow.window.text; font: parent.font; verticalAlignment: Text.AlignVCenter; leftPadding: 8 } }
        indicator: Text { anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 8; text: "▾"; color: ApplicationWindow.window.muted; font.pixelSize: 10 }
        contentItem: Text { leftPadding: 10; rightPadding: 24; text: ggaCombo.displayText; color: ApplicationWindow.window.text; font: ggaCombo.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle { implicitHeight: 34; radius: 7; color: ggaCombo.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card; border.color: ggaCombo.activeFocus ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8") : ApplicationWindow.window.border }
        popup: RtkPopup { popupWidth: ggaCombo.width; model: ggaCombo.delegateModel; index: ggaCombo.highlightedIndex }
    }

    component GgaRateCombo: ComboBox {
        id: rateCombo
        model: page.ggaRateModel; textRole: "text"
        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor); implicitHeight: 34
        Component.onCompleted: { for (var i = 0; i < page.ggaRateModel.length; ++i) if (page.ggaRateModel[i].value === rtkBackend.ggaGenerationRateHz) { rateCombo.currentIndex = i; break } }
        onActivated: { if (rateCombo.currentIndex >= 0) rtkBackend.ggaGenerationRateHz = page.ggaRateModel[rateCombo.currentIndex].value }
        delegate: ItemDelegate { width: rateCombo.width; text: modelData ? modelData.text || "" : ""; font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor); highlighted: rateCombo.highlightedIndex === index; background: Rectangle { color: highlighted ? ApplicationWindow.window.secondary : "transparent" }; contentItem: Text { text: parent.text; color: ApplicationWindow.window.text; font: parent.font; verticalAlignment: Text.AlignVCenter; leftPadding: 8 } }
        indicator: Text { anchors.verticalCenter: parent.verticalCenter; anchors.right: parent.right; anchors.rightMargin: 8; text: "▾"; color: ApplicationWindow.window.muted; font.pixelSize: 10 }
        contentItem: Text { leftPadding: 10; rightPadding: 24; text: rateCombo.displayText; color: ApplicationWindow.window.text; font: rateCombo.font; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle { implicitHeight: 34; radius: 7; color: rateCombo.hovered ? ApplicationWindow.window.secondary : ApplicationWindow.window.card; border.color: rateCombo.activeFocus ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8") : ApplicationWindow.window.border }
        popup: RtkPopup { popupWidth: rateCombo.width; model: rateCombo.delegateModel; index: rateCombo.highlightedIndex }
    }

    component RtkPopup: Popup {
        property real popupWidth: 100; property var model; property int index: 0
        y: parent.height + 2; width: popupWidth; implicitHeight: contentItem.implicitHeight + 8; padding: 4
        background: Rectangle { radius: 7; color: ApplicationWindow.window.card; border.color: ApplicationWindow.window.border }
        contentItem: ListView { clip: true; implicitHeight: contentHeight; model: parent.model; currentIndex: parent.index }
    }
}
