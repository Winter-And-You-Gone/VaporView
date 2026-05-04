import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    id: page

    readonly property int rightPad: 16

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

    // Local state for fields without Q_PROPERTY yet
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
                width: mainFlick.width - 12 - page.rightPad
                spacing: 12

                // ── Top row: two card columns ──
                RowLayout {
                    width: parent.width
                    spacing: 12

                    // ── LEFT COLUMN ──
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        // ── CARD: NTRIP Server Config ──
                        Card {
                            Layout.fillWidth: true
                            height: implicitHeight
                            title: ApplicationWindow.window.t("rtk.ntripConfig")

                            ColumnLayout {
                                width: parent.width
                                anchors.margins: 12
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
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
                                    placeholderText: ApplicationWindow.window.t("rtk.mountPoint")
                                    text: page.textValue(rtkBackend.mountpoint)
                                    onEditingFinished: rtkBackend.mountpoint = text
                                }
                                RowLayout {
                                    Layout.fillWidth: true
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
                            }
                        }

                        // ── CARD: RTCM Output Config ──
                        Card {
                            Layout.fillWidth: true
                            height: implicitHeight
                            title: "RTCM 输出配置"

                            ColumnLayout {
                                width: parent.width
                                anchors.margins: 12
                                spacing: 8

                                // Port + Baud
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        text: "Output Port"
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
                                        text: "Baud"
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
                                    spacing: 6

                                    Text {
                                        text: "Lever Arm"
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
                                        text: "Apply"
                                        iconName: "activity"
                                        onClicked: rtkBackend.applyMainAntennaLeverArm(
                                            Number(leverX.text), Number(leverY.text), Number(leverZ.text)
                                        )
                                    }
                                }

                                // Timeout + Reconnect (local state only until backend exposes them)
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        text: "Timeout (ms)"
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
                                        text: "Reconnect (ms)"
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
                            }
                        }

                        // ── CARD: GGA Monitor (simplified — auto-sent by backend) ──
                        // TODO: Add GGA sentence / frequency as Q_PROPERTY on RtkBackend
                        Card {
                            Layout.fillWidth: true
                            height: implicitHeight
                            title: "GGA 监视器"

                            ColumnLayout {
                                width: parent.width
                                anchors.margins: 12
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Text {
                                        text: ApplicationWindow.window.t("rtk.ggaSource") + ":"
                                        color: ApplicationWindow.window.text
                                        font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                    }
                                    Text {
                                        text: "EPSILON (auto-sent)"
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                        font.family: "Consolas"
                                    }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: "In: " + page.statValue("inputBps") + " B/s"
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                        font.family: "Consolas"
                                    }
                                    Text {
                                        text: "Out: " + page.statValue("outputBps") + " B/s"
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                        font.family: "Consolas"
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "GGA 由系统自动根据 EPSILON 位置数据生成，以 1 Hz 频率发送至 NTRIP 播发器。"
                                    color: ApplicationWindow.window.muted
                                    font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }
                    }

                    // ── RIGHT COLUMN ──
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        // ── CARD: Diagnostics ──
                        Card {
                            Layout.fillWidth: true
                            height: implicitHeight
                            title: ApplicationWindow.window.t("rtk.diagnostics")
                            headerRight: ToolbarButton {
                                iconName: "trash-2"
                                iconSize: 13
                                text: ApplicationWindow.window.t("home.clearLog")
                                onClicked: rtkBackend.clearDiagnostics()
                            }

                            ColumnLayout {
                                width: parent.width
                                anchors.margins: 12
                                spacing: 8

                                // 2×2 metrics
                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: 8
                                    rowSpacing: 8

                                    MetricTile {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 66
                                        label: "Input"
                                        value: page.statValue("inputBps") + " B/s"
                                    }
                                    MetricTile {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 66
                                        label: "Output"
                                        value: page.statValue("outputBps") + " B/s"
                                    }
                                    MetricTile {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 66
                                        label: "RTCM Frames"
                                        value: String(page.statValue("rtcm3FrameCount"))
                                    }
                                    MetricTile {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 66
                                        label: "CRC"
                                        value: page.statValue("rtcm3CrcOkCount") + " / " + page.statValue("rtcm3CrcFailCount")
                                    }
                                }

                                // Scrollable diagnostics log
                                ScrollView {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 200
                                    clip: true
                                    ScrollBar.vertical: ScrollBar { id: diagVBar }

                                    TextArea {
                                        id: diagText
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

                                Timer {
                                    id: diagScrollTimer
                                    interval: 50
                                    onTriggered: diagVBar.position = 1.0 - diagVBar.size
                                }
                            }
                        }

                        // ── CARD: RTK Service Log ──
                        Card {
                            Layout.fillWidth: true
                            height: implicitHeight
                            title: "RTK 服务日志"

                            ColumnLayout {
                                width: parent.width
                                anchors.margins: 12
                                spacing: 6

                                RowLayout {
                                    Layout.fillWidth: true
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
                                            : "---"
                                        color: rtkBackend.running
                                            ? ApplicationWindow.window.ok
                                            : ApplicationWindow.window.muted
                                        font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                                        font.bold: true
                                    }
                                    Item { Layout.fillWidth: true }
                                    Text {
                                        text: {
                                            var m = rtkBackend.stats.message
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
                                    spacing: 12

                                    Text {
                                        text: "消息类型:"
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                    }
                                    Text {
                                        text: {
                                            var mt = rtkBackend.stats.messageTypes
                                            return mt ? String(mt) : "---"
                                        }
                                        color: ApplicationWindow.window.text
                                        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                                        font.family: "Consolas"
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }

                                ScrollView {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 100
                                    clip: true

                                    TextArea {
                                        text: page.diagnosticsText()
                                        readOnly: true
                                        selectByMouse: true
                                        wrapMode: TextEdit.Wrap
                                        color: ApplicationWindow.window.text
                                        font.family: "Consolas"
                                        font.pixelSize: Math.round(9 * ApplicationWindow.window.scaleFactor)
                                        background: Rectangle { color: "transparent" }
                                    }
                                }
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
                        text: rtkBackend.running ? "Stop" : "Start"
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
                        text: "Clear Log"
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
                        text: "Load Config"
                        onClicked: rtkBackend.loadConfig()
                    }
                }

                Item { width: 1; height: 24 }
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
                        ? ("Running: "
                           + page.statValue("inputBps") + " B/s in  /  "
                           + page.statValue("outputBps") + " B/s out")
                        : "Stopped"
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

    // ── Inline: auto-scroll when diagnostics change ──
    Connections {
        target: rtkBackend
        function onDiagnosticsChanged() { diagScrollTimer.restart() }
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
