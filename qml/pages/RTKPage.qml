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

    // GGA source options (local state until backend exposes this)
    // TODO: Add ggaSourceOptions / ggaSource as Q_PROPERTY on RtkBackend
    property var ggaSourceModel: [
        { text: ApplicationWindow.window.t("rtk.ggaSourceEpsilonMain"), value: "epsilon_main" },
        { text: ApplicationWindow.window.t("rtk.ggaSourceEpsilonAux"), value: "epsilon_aux" },
        { text: ApplicationWindow.window.t("rtk.ggaSourceAuto"), value: "auto" },
    ]
    property int ggaSourceIndex: 0
    property bool ggaReading: false

    // Local state for timeout/reconnect (not yet Q_PROPERTY)
    // TODO: Add timeoutMs/reconnectMs as Q_PROPERTY on RtkBackend
    property string uiTimeoutMs: "5000"
    property string uiReconnectMs: "1000"

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Main scrollable area ──
        Flickable {
            id: mainFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: mainFlick.width
            contentHeight: mainColumn.height + 24
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            Column {
                id: mainColumn
                x: 12
                y: 12
                width: mainFlick.width - 12 - 16
                spacing: 12

                // ── Top row: two card columns ──
                RowLayout {
                    width: parent.width
                    spacing: 12

                    // ── LEFT COLUMN (wider) ──
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        Layout.minimumWidth: 320
                        spacing: 12

                        // ── CARD 1: NTRIP Server Config ──
                        Card {
                            Layout.fillWidth: true
                            height: implicitHeight
                            title: ApplicationWindow.window.t("rtk.ntripConfig")

                            ColumnLayout {
                                width: parent.width
                                spacing: 8

                                Item { width: 1; height: 4 }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 12
                                    Layout.rightMargin: 12
                                    spacing: 8
                                    RtkTextField {
                                        Layout.fillWidth: true
                                        placeholderText: ApplicationWindow.window.t("rtk.casterAddress")
                                        text: page.textValue(rtkBackend.server)
                                        onEditingFinished: rtkBackend.server = text
                                    }
                                    RtkTextField {
                                        Layout.preferredWidth: 100
                                        placeholderText: ApplicationWindow.window.t("rtk.port")
                                        text: page.textValue(rtkBackend.port, "2101")
                                        onEditingFinished: rtkBackend.port = text
                                    }
                                }
                                RtkTextField {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 12
                                    Layout.rightMargin: 12
                                    placeholderText: ApplicationWindow.window.t("rtk.mountPoint")
                                    text: page.textValue(rtkBackend.mountpoint)
                                    onEditingFinished: rtkBackend.mountpoint = text
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 12
                                    Layout.rightMargin: 12
                                    spacing: 8
                                    RtkTextField {
                                        Layout.fillWidth: true
                                        placeholderText: ApplicationWindow.window.t("rtk.username")
                                        text: page.textValue(rtkBackend.username)
                                        onEditingFinished: rtkBackend.username = text
                                    }
                                    RtkTextField {
                                        Layout.fillWidth: true
                                        placeholderText: ApplicationWindow.window.t("rtk.password")
                                        echoMode: TextInput.Password
                                        text: page.textValue(rtkBackend.password)
                                        onEditingFinished: rtkBackend.password = text
                                    }
                                }

                                Item { width: 1; height: 4 }
                            }
                        }

                        // ── CARD 2: RTCM Output Config ──
                        Card {
                            Layout.fillWidth: true
                            height: implicitHeight
                            title: ApplicationWindow.window.t("rtk.rtcmOutputConfig")

                            ColumnLayout {
                                width: parent.width
                                spacing: 8

                                Item { width: 1; height: 4 }

                                // Port + Baud
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 12
                                    Layout.rightMargin: 12
                                    spacing: 8

                                    Text {
                                        text: ApplicationWindow.window.t("rtk.outputPort")
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                        Layout.alignment: Qt.AlignVCenter
                                    }
                                    RtkTextField {
                                        Layout.fillWidth: true
                                        placeholderText: "e.g. COM3"
                                        text: page.textValue(rtkBackend.outputPort)
                                        onEditingFinished: rtkBackend.outputPort = text
                                    }
                                    Text {
                                        text: ApplicationWindow.window.t("rtk.baudRate")
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                        Layout.alignment: Qt.AlignVCenter
                                    }
                                    RtkTextField {
                                        Layout.preferredWidth: 100
                                        text: page.textValue(rtkBackend.outputBaud, "115200")
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        onEditingFinished: rtkBackend.outputBaud = Math.max(1, Number(text))
                                    }
                                }

                                // Lever Arm
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 12
                                    Layout.rightMargin: 12
                                    spacing: 6

                                    Text {
                                        text: ApplicationWindow.window.t("rtk.leverArm")
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                        Layout.alignment: Qt.AlignVCenter
                                    }
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
                                        Layout.preferredWidth: 64
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
                                        Layout.preferredWidth: 64
                                        text: "0"
                                        validator: DoubleValidator { bottom: -10000; top: 10000; decimals: 4 }
                                    }
                                    ToolbarButton {
                                        text: ApplicationWindow.window.t("rtk.applyLeverArm")
                                        iconName: "activity"
                                        onClicked: rtkBackend.applyMainAntennaLeverArm(
                                            Number(leverX.text), Number(leverY.text), Number(leverZ.text)
                                        )
                                    }
                                }

                                // Timeout + Reconnect
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 12
                                    Layout.rightMargin: 12
                                    spacing: 8

                                    Text {
                                        text: ApplicationWindow.window.t("rtk.timeoutMs")
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                        Layout.alignment: Qt.AlignVCenter
                                    }
                                    RtkTextField {
                                        Layout.fillWidth: true
                                        text: page.uiTimeoutMs
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        onEditingFinished: page.uiTimeoutMs = text
                                    }
                                    Text {
                                        text: ApplicationWindow.window.t("rtk.reconnectMs")
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                        Layout.alignment: Qt.AlignVCenter
                                    }
                                    RtkTextField {
                                        Layout.fillWidth: true
                                        text: page.uiReconnectMs
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        onEditingFinished: page.uiReconnectMs = text
                                    }
                                }

                                Item { width: 1; height: 4 }
                            }
                        }

                        // ── CARD 3: GGA Monitor ──
                        Card {
                            Layout.fillWidth: true
                            height: implicitHeight
                            title: ApplicationWindow.window.t("rtk.ggaMonitor")

                            ColumnLayout {
                                width: parent.width
                                spacing: 6

                                Item { width: 1; height: 4 }

                                // Row 1: Source combo + Read button + Frequency
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

                                    ComboBox {
                                        id: ggaSourceCombo
                                        Layout.preferredWidth: 180
                                        model: page.ggaSourceModel
                                        currentIndex: page.ggaSourceIndex
                                        textRole: "text"
                                        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
                                        implicitHeight: 34
                                        onActivated: page.ggaSourceIndex = currentIndex

                                        delegate: ItemDelegate {
                                            width: ggaSourceCombo.width
                                            text: ggaSourceCombo.textRole
                                                ? model[ggaSourceCombo.textRole] : modelData
                                            font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
                                            highlighted: ggaSourceCombo.highlightedIndex === index
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
                                            text: ggaSourceCombo.displayText
                                            color: ApplicationWindow.window.text
                                            font: ggaSourceCombo.font
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }

                                        background: Rectangle {
                                            implicitHeight: 34
                                            radius: 7
                                            color: ggaSourceCombo.hovered
                                                ? ApplicationWindow.window.secondary
                                                : ApplicationWindow.window.card
                                            border.color: ggaSourceCombo.activeFocus
                                                ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8")
                                                : ApplicationWindow.window.border
                                        }

                                        popup: Popup {
                                            y: ggaSourceCombo.height + 2
                                            width: ggaSourceCombo.width
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
                                                model: ggaSourceCombo.delegateModel
                                                currentIndex: ggaSourceCombo.highlightedIndex
                                            }
                                        }
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
                                        text: ApplicationWindow.window.t("rtk.ggaFrequency") + ": "
                                            + "0.00 Hz"
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                        font.family: "Consolas"
                                    }
                                }

                                // Row 2: Status text
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
                                    wrapMode: Text.WordWrap
                                }

                                // Row 3: GGA content area
                                ScrollView {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 80
                                    Layout.leftMargin: 12
                                    Layout.rightMargin: 12
                                    clip: true

                                    TextArea {
                                        text: page.ggaReading
                                            ? ("$GPGGA," + new Date().toLocaleTimeString() + ",..."
                                               + "\n" + ApplicationWindow.window.t("rtk.waiting"))
                                            : ApplicationWindow.window.t("rtk.noData")
                                        readOnly: true
                                        selectByMouse: true
                                        wrapMode: TextEdit.Wrap
                                        color: ApplicationWindow.window.text
                                        font.family: "Consolas"
                                        font.pixelSize: Math.round(9 * ApplicationWindow.window.scaleFactor)
                                        background: Rectangle { color: "transparent" }
                                    }
                                }

                                Item { width: 1; height: 4 }
                            }
                        }
                    }

                    // ── RIGHT COLUMN (narrower, compact diagnostics) ──
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        Layout.maximumWidth: 400

                        // ── CARD 4: Diagnostics (compact) ──
                        Card {
                            Layout.fillWidth: true
                            height: implicitHeight
                            title: ApplicationWindow.window.t("rtk.diagnostics")
                            headerRight: ToolbarButton {
                                iconName: "trash-2"
                                iconSize: 13
                                text: ApplicationWindow.window.t("rtk.clearLog")
                                onClicked: rtkBackend.clearDiagnostics()
                            }

                            ColumnLayout {
                                width: parent.width
                                spacing: 6

                                Item { width: 1; height: 4 }

                                // 2×2 compact metrics
                                GridLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 12
                                    Layout.rightMargin: 12
                                    columns: 2
                                    columnSpacing: 6
                                    rowSpacing: 6

                                    MetricTile {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 54
                                        label: ApplicationWindow.window.t("rtk.inputRate")
                                        value: page.statValue("inputBps") + " B/s"
                                    }
                                    MetricTile {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 54
                                        label: ApplicationWindow.window.t("rtk.outputRate")
                                        value: page.statValue("outputBps") + " B/s"
                                    }
                                    MetricTile {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 54
                                        label: ApplicationWindow.window.t("rtk.rtcmFrames")
                                        value: String(page.statValue("rtcm3FrameCount"))
                                    }
                                    MetricTile {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 54
                                        label: ApplicationWindow.window.t("rtk.crcStatus")
                                        value: page.statValue("rtcm3CrcOkCount") + " / " + page.statValue("rtcm3CrcFailCount")
                                    }
                                }

                                // Compact log area
                                ScrollView {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 110
                                    Layout.leftMargin: 12
                                    Layout.rightMargin: 12
                                    Layout.bottomMargin: 4
                                    clip: true
                                    ScrollBar.vertical: ScrollBar { id: diagVBar }

                                    TextArea {
                                        text: page.diagnosticsText()
                                        readOnly: true
                                        selectByMouse: true
                                        wrapMode: TextEdit.Wrap
                                        color: ApplicationWindow.window.text
                                        selectedTextColor: ApplicationWindow.window.primaryForeground
                                        selectionColor: ApplicationWindow.window.primary
                                        font.family: "Consolas"
                                        font.pixelSize: Math.round(9 * ApplicationWindow.window.scaleFactor)
                                        background: Rectangle { color: "transparent" }
                                    }
                                }

                                Item { width: 1; height: 4 }
                            }

                            Timer {
                                id: diagScrollTimer
                                interval: 50
                                onTriggered: diagVBar.position = 1.0 - diagVBar.size
                            }

                            Connections {
                                target: rtkBackend
                                function onDiagnosticsChanged() { diagScrollTimer.restart() }
                            }
                        }
                    }
                }

                // ── Operation buttons (full width) ──
                RowLayout {
                    width: parent.width
                    spacing: 8

                    ToolbarButton {
                        iconName: rtkBackend.running ? "square" : "wifi"
                        text: rtkBackend.running
                            ? ApplicationWindow.window.t("rtk.stop")
                            : ApplicationWindow.window.t("rtk.start")
                        variant: rtkBackend.running ? "danger" : "primary"
                        onClicked: rtkBackend.running ? rtkBackend.stop() : rtkBackend.start()
                    }
                    ToolbarButton {
                        iconName: "scan"
                        text: ApplicationWindow.window.t("rtk.testConnection")
                        onClicked: rtkBackend.testConnection()
                    }
                    ToolbarButton {
                        iconName: "trash-2"
                        text: ApplicationWindow.window.t("rtk.clearLog")
                        onClicked: rtkBackend.clearDiagnostics()
                    }
                    Item { Layout.fillWidth: true }
                    ToolbarButton {
                        iconName: "save"
                        text: ApplicationWindow.window.t("rtk.saveConfig")
                        onClicked: rtkBackend.saveConfig()
                    }
                    ToolbarButton {
                        iconName: "folder-open"
                        text: ApplicationWindow.window.t("rtk.loadConfig")
                        onClicked: rtkBackend.loadConfig()
                    }
                }

                // ── RTK Service Log (full width, more prominent) ──
                Card {
                    width: parent.width
                    height: implicitHeight
                    title: ApplicationWindow.window.t("rtk.serviceLog")

                    ColumnLayout {
                        width: parent.width
                        spacing: 4

                        Item { width: 1; height: 4 }

                        // Status summary row
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            spacing: 8

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
                            Item { Layout.fillWidth: true }
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

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            spacing: 12

                            Text {
                                text: ApplicationWindow.window.t("rtk.messageTypes") + ":"
                                color: ApplicationWindow.window.muted
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                            }
                            Text {
                                text: {
                                    var mt = rtkBackend.stats ? rtkBackend.stats.messageTypes : ""
                                    return mt ? String(mt) : "---"
                                }
                                color: ApplicationWindow.window.text
                                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                font.family: "Consolas"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }

                        // Scrollable log area (larger)
                        ScrollView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 180
                            Layout.leftMargin: 12
                            Layout.rightMargin: 12
                            Layout.bottomMargin: 4
                            clip: true

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
                }

                Item { width: 1; height: 12 }
            }
        }

        // ── Status bar (pinned bottom) ──
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: ApplicationWindow.window.card

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: ApplicationWindow.window.border
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: rtkBackend.running
                        ? ApplicationWindow.window.ok
                        : ApplicationWindow.window.offline
                }

                Text {
                    text: rtkBackend.running
                        ? (ApplicationWindow.window.t("rtk.statusRunning")
                            .replace("%1", page.statValue("inputBps"))
                            .replace("%2", page.statValue("outputBps")))
                        : ApplicationWindow.window.t("rtk.stopped")
                    color: ApplicationWindow.window.text
                    font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

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
}
