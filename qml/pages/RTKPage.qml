import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    id: page

    // ── Helpers ──
    function textValue(value, fallback) {
        if (value === undefined || value === null)
            return fallback || ""
        return String(value)
    }

    function statValue(key) {
        var stats = rtkBackend.stats || {}
        var value = stats[key]
        if (value === undefined || value === null || isNaN(Number(value)))
            return 0
        return Number(value)
    }

    function diagnosticsText() {
        var lines = rtkBackend.diagnostics || []
        return lines.length > 0 ? lines.join("\n") : ""
    }

    // ── GGA source model ──
    // TODO: Add ggaSourceOptions / ggaSource as Q_PROPERTY on RtkBackend
    property var ggaSourceModel: [
        { text: ApplicationWindow.window.t("rtk.ggaSourceEpsilonMain"), value: "epsilon_main" },
        { text: ApplicationWindow.window.t("rtk.ggaSourceEpsilonAux"), value: "epsilon_aux" },
        { text: ApplicationWindow.window.t("rtk.ggaSourceAuto"), value: "auto" },
    ]
    property int ggaSourceIndex: 0
    property bool ggaReading: false

    // Local state for fields without Q_PROPERTY
    // TODO: Add timeoutMs/reconnectMs as Q_PROPERTY on RtkBackend
    property string uiTimeoutMs: "5000"
    property string uiReconnectMs: "1000"

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // ════════════════════════════════════════════════════════
        // LEFT COLUMN  —  narrow config  (~36%)
        // ════════════════════════════════════════════════════════
        ColumnLayout {
            Layout.preferredWidth: Math.round(parent.width * 0.36)
            Layout.maximumWidth: Math.round(parent.width * 0.40)
            Layout.fillHeight: true
            spacing: 12

            // ── CARD: NTRIP Server Config ──
            Card {
                Layout.fillWidth: true
                height: implicitHeight
                title: ApplicationWindow.window.t("rtk.ntripConfig")

                ColumnLayout {
                    width: parent.width
                    spacing: 8

                    Item { width: 1; height: 4 }

                    Text {
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        text: ApplicationWindow.window.t("rtk.casterAddress")
                        color: ApplicationWindow.window.muted
                        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                    }
                    RtkTextField {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        text: page.textValue(rtkBackend.server)
                        onEditingFinished: rtkBackend.server = text
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        spacing: 8
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text {
                                text: ApplicationWindow.window.t("rtk.port")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkTextField {
                                Layout.fillWidth: true
                                text: page.textValue(rtkBackend.port, "2101")
                                onEditingFinished: rtkBackend.port = text
                            }
                        }
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text {
                                text: ApplicationWindow.window.t("rtk.mountPoint")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkMountPointCombo {
                                Layout.fillWidth: true
                            }
                            ToolbarButton {
                                id: detectMountBtn
                                Layout.fillWidth: true
                                iconName: "search"
                                text: rtkBackend.detectingMountPoints
                                    ? ApplicationWindow.window.t("rtk.detecting")
                                    : ApplicationWindow.window.t("rtk.detectMountPoints")
                                enabled: !rtkBackend.detectingMountPoints
                                onClicked: rtkBackend.detectMountPoints()
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        spacing: 8
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text {
                                text: ApplicationWindow.window.t("rtk.username")
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
                                text: ApplicationWindow.window.t("rtk.password")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkTextField {
                                Layout.fillWidth: true
                                text: page.textValue(rtkBackend.password)
                                onEditingFinished: rtkBackend.password = text
                            }
                        }
                    }

                    Item { width: 1; height: 4 }
                }
            }

            // ── CARD: RTCM Output Config ──
            Card {
                Layout.fillWidth: true
                height: implicitHeight
                title: ApplicationWindow.window.t("rtk.rtcmOutputConfig")

                ColumnLayout {
                    width: parent.width
                    spacing: 8

                    Item { width: 1; height: 4 }

                    Text {
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        text: ApplicationWindow.window.t("rtk.outputPort")
                        color: ApplicationWindow.window.muted
                        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                    }
                    RtkPortComboBox {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        spacing: 6
                        Item { Layout.fillWidth: true }
                        ToolbarButton {
                            iconName: "refresh-cw"
                            implicitWidth: 28
                            implicitHeight: 28
                            onClicked: {
                                deviceBackend.refreshPorts()
                                rtkBackend.refreshOutputPortOptions()
                            }
                        }
                        ToolbarButton {
                            iconName: "radio"
                            text: ApplicationWindow.window.t("rtk.autoDetectPort")
                            onClicked: deviceBackend.autoDetectPortsOrCancel()
                        }
                    }

                    Text {
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        text: ApplicationWindow.window.t("rtk.baudRate")
                        color: ApplicationWindow.window.muted
                        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                    }
                    RtkTextField {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        text: page.textValue(rtkBackend.outputBaud, "115200")
                        inputMethodHints: Qt.ImhDigitsOnly
                        onEditingFinished: rtkBackend.outputBaud = Math.max(1, Number(text))
                    }

                    // Lever Arm
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        spacing: 4

                        Text {
                            text: ApplicationWindow.window.t("rtk.leverArm")
                            color: ApplicationWindow.window.muted
                            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            Layout.alignment: Qt.AlignVCenter
                        }
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
                            Layout.preferredWidth: 56
                            text: "0"
                            validator: DoubleValidator { bottom: -10000; top: 10000; decimals: 4 }
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
                            Layout.preferredWidth: 56
                            text: "0"
                            validator: DoubleValidator { bottom: -10000; top: 10000; decimals: 4 }
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
                            Layout.preferredWidth: 56
                            text: "0"
                            validator: DoubleValidator { bottom: -10000; top: 10000; decimals: 4 }
                        }
                    }
                    ToolbarButton {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        text: ApplicationWindow.window.t("rtk.applyLeverArm")
                        iconName: "activity"
                        onClicked: rtkBackend.applyMainAntennaLeverArm(
                            Number(leverX.text), Number(leverY.text), Number(leverZ.text))
                    }

                    // Timeout + Reconnect
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        spacing: 8
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text {
                                text: ApplicationWindow.window.t("rtk.timeoutMs")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkTextField {
                                Layout.fillWidth: true
                                text: page.uiTimeoutMs
                                inputMethodHints: Qt.ImhDigitsOnly
                                onEditingFinished: page.uiTimeoutMs = text
                            }
                        }
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text {
                                text: ApplicationWindow.window.t("rtk.reconnectMs")
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            RtkTextField {
                                Layout.fillWidth: true
                                text: page.uiReconnectMs
                                inputMethodHints: Qt.ImhDigitsOnly
                                onEditingFinished: page.uiReconnectMs = text
                            }
                        }
                    }

                    Item { width: 1; height: 4 }
                }
            }

            // ── Spacer to push buttons to bottom ──
            Item { Layout.fillHeight: true }

            // ── Operation buttons ──
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "wifi"
                    text: ApplicationWindow.window.t("rtk.start")
                    enabled: !rtkBackend.running
                    variant: "primary"
                    onClicked: rtkBackend.start()
                }
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "square"
                    text: ApplicationWindow.window.t("rtk.stop")
                    enabled: rtkBackend.running
                    variant: "danger"
                    onClicked: rtkBackend.stop()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "scan"
                    text: ApplicationWindow.window.t("rtk.testConnection")
                    onClicked: rtkBackend.testConnection()
                }
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "trash-2"
                    text: ApplicationWindow.window.t("rtk.clearLog")
                    onClicked: rtkBackend.clearDiagnostics()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "save"
                    text: ApplicationWindow.window.t("rtk.saveConfig")
                    onClicked: rtkBackend.saveConfig()
                }
                ToolbarButton {
                    Layout.fillWidth: true
                    iconName: "folder-open"
                    text: ApplicationWindow.window.t("rtk.loadConfig")
                    onClicked: rtkBackend.loadConfig()
                }
            }
        }

        // ════════════════════════════════════════════════════════
        // RIGHT COLUMN  —  wide monitoring area  (~64%)
        // ════════════════════════════════════════════════════════
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // ── CARD: GGA Monitor ──
            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 240
                title: ApplicationWindow.window.t("rtk.ggaMonitor")

                ColumnLayout {
                    width: parent.width
                    spacing: 6

                    Item { width: 1; height: 4 }

                    // Source combo + Read button + Frequency
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        spacing: 8

                        Text {
                            text: ApplicationWindow.window.t("rtk.ggaSource") + ":"
                            color: ApplicationWindow.window.text
                            font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                            Layout.alignment: Qt.AlignVCenter
                        }

                        GgaSourceCombo {
                            id: ggaSourceCombo
                            Layout.preferredWidth: 200
                        }

                        ToolbarButton {
                            id: ggaToggleBtn
                            text: page.ggaReading
                                ? ApplicationWindow.window.t("rtk.stopReading")
                                : ApplicationWindow.window.t("rtk.readGga")
                            iconName: page.ggaReading ? "square" : "radio"
                            onClicked: {
                                page.ggaReading = !page.ggaReading
                                // TODO: Wire to backend GGA read/stop when API is available
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: ApplicationWindow.window.t("rtk.ggaFrequency") + ": 0.00 Hz"
                            color: ApplicationWindow.window.muted
                            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            font.family: "Consolas"
                        }
                    }

                    // Status
                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        text: page.ggaReading
                            ? (ApplicationWindow.window.t("rtk.waiting")
                               + " (" + page.ggaSourceModel[page.ggaSourceIndex].text + ")")
                            : ApplicationWindow.window.t("rtk.noData")
                        color: ApplicationWindow.window.muted
                        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                    }

                    // GGA content area
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Layout.bottomMargin: 4
                        clip: true
                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                        TextArea {
                            text: page.ggaReading
                                ? ApplicationWindow.window.t("rtk.waiting") + "..."
                                : ApplicationWindow.window.t("rtk.noData")
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.NoWrap
                            color: ApplicationWindow.window.text
                            font.family: "Consolas"
                            font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                            background: Rectangle { color: "transparent" }
                        }
                    }

                    Item { width: 1; height: 4 }
                }
            }

            // ── CARD: RTK Service Log ──
            Card {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: ApplicationWindow.window.t("rtk.serviceLog")

                ColumnLayout {
                    width: parent.width
                    spacing: 6

                    Item { width: 1; height: 4 }

                    // Row 1: diff status + message types
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        spacing: 16

                        Text {
                            text: ApplicationWindow.window.t("rtk.diffStatus") + ":"
                            color: ApplicationWindow.window.text
                            font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                            font.bold: true
                        }
                        Text {
                            text: rtkBackend.running
                                ? ApplicationWindow.window.t("rtk.connected")
                                : ApplicationWindow.window.t("rtk.stopped")
                            color: rtkBackend.running
                                ? ApplicationWindow.window.ok
                                : ApplicationWindow.window.muted
                            font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                            font.bold: true
                        }

                        Rectangle {
                            width: 1; height: 14
                            color: ApplicationWindow.window.border
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Text {
                            text: ApplicationWindow.window.t("rtk.messageTypes") + ":"
                            color: ApplicationWindow.window.text
                            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Text {
                            text: {
                                var mt = rtkBackend.stats ? rtkBackend.stats.messageTypes : ""
                                return mt ? String(mt) : "---"
                            }
                            color: ApplicationWindow.window.muted
                            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            font.family: "Consolas"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }

                        Text {
                            text: {
                                var m = rtkBackend.stats ? rtkBackend.stats.message : ""
                                return m ? String(m) : ""
                            }
                            color: ApplicationWindow.window.muted
                            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            font.family: "Consolas"
                            elide: Text.ElideRight
                        }
                    }

                    // Row 2: diagnostics summary
                    GridLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        columns: 4
                        columnSpacing: 8
                        rowSpacing: 2

                        Text {
                            text: ApplicationWindow.window.t("rtk.inputRate") + ": "
                                + page.statValue("inputBps") + " B/s"
                            color: ApplicationWindow.window.muted
                            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                        }
                        Text {
                            text: ApplicationWindow.window.t("rtk.outputRate") + ": "
                                + page.statValue("outputBps") + " B/s"
                            color: ApplicationWindow.window.muted
                            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                        }
                        Text {
                            text: ApplicationWindow.window.t("rtk.rtcmFrames") + ": "
                                + page.statValue("rtcm3FrameCount")
                            color: ApplicationWindow.window.muted
                            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                        }
                        Text {
                            text: ApplicationWindow.window.t("rtk.crcStatus") + ": "
                                + page.statValue("rtcm3CrcOkCount") + " / "
                                + page.statValue("rtcm3CrcFailCount")
                            color: ApplicationWindow.window.muted
                            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                        }
                    }

                    // Log area
                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.leftMargin: 12
                        Layout.rightMargin: 12
                        Layout.bottomMargin: 4
                        clip: true
                        ScrollBar.vertical: ScrollBar { id: logVBar }

                        TextArea {
                            text: page.diagnosticsText()
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

                    Item { width: 1; height: 4 }
                }

                Timer {
                    id: logScrollTimer
                    interval: 50
                    onTriggered: logVBar.position = 1.0 - logVBar.size
                }

                Connections {
                    target: rtkBackend
                    function onDiagnosticsChanged() { logScrollTimer.restart() }
                }
            }
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
        onEditingFinished: rtkBackend.setMountpoint(mpCombo.editText)

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
}
