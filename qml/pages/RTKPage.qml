import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    id: page

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

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: ApplicationWindow.window.t("rtk.ntripConfig")

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RtkTextField {
                    Layout.fillWidth: true
                    placeholderText: ApplicationWindow.window.t("rtk.casterAddress")
                    text: page.textValue(rtkBackend.server)
                    onEditingFinished: rtkBackend.server = text
                }
                RtkTextField {
                    Layout.fillWidth: true
                    placeholderText: ApplicationWindow.window.t("rtk.port")
                    text: page.textValue(rtkBackend.port, "2101")
                    onEditingFinished: rtkBackend.port = text
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
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    RtkTextField {
                        Layout.fillWidth: true
                        placeholderText: "Output Port"
                        text: page.textValue(rtkBackend.outputPort)
                        onEditingFinished: rtkBackend.outputPort = text
                    }
                    RtkTextField {
                        Layout.preferredWidth: 120
                        placeholderText: "Baud"
                        text: page.textValue(rtkBackend.outputBaud, "115200")
                        inputMethodHints: Qt.ImhDigitsOnly
                        onEditingFinished: rtkBackend.outputBaud = Math.max(1, Number(text))
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    RtkTextField { id: leverX; Layout.fillWidth: true; placeholderText: "Lever X m"; text: "0" }
                    RtkTextField { id: leverY; Layout.fillWidth: true; placeholderText: "Y"; text: "0" }
                    RtkTextField { id: leverZ; Layout.fillWidth: true; placeholderText: "Z"; text: "0" }
                }
                RowLayout {
                    Layout.fillWidth: true
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
                        iconName: "save"
                        text: ApplicationWindow.window.t("rtk.saveConfig")
                        onClicked: rtkBackend.saveConfig()
                    }
                }
                ToolbarButton {
                    iconName: "activity"
                    text: "Apply Lever Arm"
                    onClicked: rtkBackend.applyMainAntennaLeverArm(Number(leverX.text), Number(leverY.text), Number(leverZ.text))
                }
                Item { Layout.fillHeight: true }
            }
        }

        Card {
            Layout.fillWidth: true
            Layout.fillHeight: true
            title: ApplicationWindow.window.t("rtk.diagnostics")
            headerRight: ToolbarButton {
                iconName: "trash-2"
                iconSize: 13
                text: ApplicationWindow.window.t("home.clearLog")
                onClicked: rtkBackend.clearDiagnostics()
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 8
                    rowSpacing: 8
                    MetricTile { Layout.fillWidth: true; Layout.preferredHeight: 66; label: ApplicationWindow.window.t("rtk.rtcmThroughput"); value: String(page.statValue("inputBps")); unit: "B/s" }
                    MetricTile { Layout.fillWidth: true; Layout.preferredHeight: 66; label: "Output"; value: String(page.statValue("outputBps")); unit: "B/s" }
                    MetricTile { Layout.fillWidth: true; Layout.preferredHeight: 66; label: "RTCM3"; value: String(page.statValue("rtcm3FrameCount")) }
                    MetricTile { Layout.fillWidth: true; Layout.preferredHeight: 66; label: ApplicationWindow.window.t("rtk.diffStatus"); value: rtkBackend.running ? ApplicationWindow.window.t("rtk.connected") : "---" }
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true

                    TextArea {
                        text: page.diagnosticsText()
                        readOnly: true
                        selectByMouse: true
                        selectByKeyboard: true
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
            border.color: field.activeFocus ? (ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8")
                                            : ApplicationWindow.window.border
        }
    }
}
