import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    id: page

    function portModel(currentPort) {
        var ports = []
        var current = String(currentPort || "")
        for (var i = 0; i < deviceBackend.ports.length; ++i)
            ports.push(deviceBackend.ports[i])
        if (current.length > 0 && ports.indexOf(current) < 0)
            ports.unshift(current)
        return ports
    }

    function selectIndex(values, value) {
        var index = values.indexOf(String(value))
        return index >= 0 ? index : 0
    }

    function tcpHost(endpoint) {
        var text = String(endpoint || "")
        var split = text.lastIndexOf(":")
        return split > 0 ? text.slice(0, split) : (text.length > 0 ? text : waveformBackend.host)
    }

    function tcpPort(endpoint) {
        var text = String(endpoint || "")
        var split = text.lastIndexOf(":")
        return split > 0 ? text.slice(split + 1) : String(waveformBackend.port)
    }

    function updateTcpEndpoint(host, portText) {
        var hostText = String(host || "").trim()
        var portNumber = Math.max(1, Math.min(65535, Number(portText)))
        if (hostText.length === 0)
            hostText = "127.0.0.1"
        waveformBackend.host = hostText
        waveformBackend.port = portNumber
        deviceBackend.updateDevicePort("waveform", hostText + ":" + portNumber)
    }

    function disconnectText() {
        return ApplicationWindow.window.t("devices.disconnectAll")
            .replace("全部断开", "断开")
            .replace("Disconnect All", "Disconnect")
    }

    function disconnectAllDevices() {
        if (waveformBackend.connected)
            waveformBackend.disconnectFromHost()
        deviceBackend.disconnectDevices()
    }

    function rateText(value) {
        var hz = Number(value)
        if (!isFinite(hz) || hz <= 0.05)
            return "--"
        return hz >= 10 ? hz.toFixed(1) : hz.toFixed(2)
    }

    function runDeviceTest(deviceId) {
        if (deviceId === "epsilon") {
            epsilonRatesPopup.open()
        } else {
            deviceBackend.appendLogLine(ApplicationWindow.window.t("devices.testNotAvailable"), "warning")
        }
    }

    component FieldLabel: Text {
        color: ApplicationWindow.window.muted
        font.pixelSize: 12 * ApplicationWindow.window.scaleFactor
        font.weight: Font.Medium
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    component DeviceTextField: TextField {
        id: field
        Layout.preferredWidth: 128
        Layout.preferredHeight: 30
        color: enabled ? ApplicationWindow.window.text : ApplicationWindow.window.muted
        selectedTextColor: ApplicationWindow.window.primaryForeground
        selectionColor: ApplicationWindow.window.primary
        font.family: "Consolas"
        font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
        leftPadding: 10
        rightPadding: 10
        verticalAlignment: TextInput.AlignVCenter
        background: Rectangle {
            radius: 5
            color: field.enabled ? ApplicationWindow.window.bg : ApplicationWindow.window.cardAlt
            border.color: ApplicationWindow.window.border
            border.width: 1
        }
    }

    component DeviceComboBox: ComboBox {
        id: combo
        Layout.preferredWidth: 128
        Layout.preferredHeight: 30
        font.family: "Consolas"
        font.pixelSize: 11 * ApplicationWindow.window.scaleFactor

        contentItem: Text {
            leftPadding: 10
            rightPadding: 28
            text: combo.displayText
            color: combo.enabled ? ApplicationWindow.window.text : ApplicationWindow.window.muted
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        indicator: LucideIcon {
            width: 14
            height: 14
            x: combo.width - width - 10
            y: Math.round((combo.height - height) / 2)
            name: "chevron-down"
            iconColor: combo.enabled ? ApplicationWindow.window.text : ApplicationWindow.window.muted
            stroke: 2
        }

        background: Rectangle {
            radius: 5
            color: combo.enabled ? ApplicationWindow.window.bg : ApplicationWindow.window.cardAlt
            border.color: ApplicationWindow.window.border
            border.width: 1
        }

        popup: Popup {
            y: combo.height + 2
            width: combo.width
            implicitHeight: Math.min(contentItem.implicitHeight, 220)
            padding: 1
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            }
            background: Rectangle {
                color: ApplicationWindow.window.card
                border.color: ApplicationWindow.window.border
                radius: 5
                }  // ColumnLayout
            }  // Card
            }  // Repeater
        }  // Flow

    component UnitFieldLabel: RowLayout {
        property string label: ""
        property string unit: ""

        Layout.fillWidth: true
        spacing: 3

        FieldLabel {
            text: parent.label
        }

        Text {
            visible: parent.unit.length > 0
            text: "(" + parent.unit + ")"
            color: ApplicationWindow.window.muted
            opacity: 0.75
            font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
            verticalAlignment: Text.AlignVCenter
        }

        Item { Layout.fillWidth: true }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ToolbarButton { iconName: deviceBackend.autoDetectInProgress ? "square" : "scan"; text: deviceBackend.autoDetectInProgress ? ApplicationWindow.window.t("topbar.cancel") : ApplicationWindow.window.t("devices.autoDetect"); variant: "primary"; onClicked: deviceBackend.autoDetectPortsOrCancel() }
            ToolbarButton { iconName: "refresh-cw"; text: ApplicationWindow.window.t("devices.refreshPorts"); onClicked: deviceBackend.refreshPorts() }
            Item { Layout.fillWidth: true }
            ToolbarButton { iconName: "link"; text: ApplicationWindow.window.t("devices.connectAll"); variant: "primary"; enabled: !deviceBackend.busy; onClicked: deviceBackend.connectDevices() }
            ToolbarButton { iconName: "unlink"; text: ApplicationWindow.window.t("devices.disconnectAll"); variant: "danger"; enabled: !deviceBackend.busy || deviceBackend.connected || waveformBackend.connected; onClicked: page.disconnectAllDevices() }
        }

        Flow {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Repeater {
                model: deviceBackend.devices

                Card {
                    readonly property real calcWidth: Math.min(240, (grid.width - grid.spacing * Math.max(0, Math.floor((grid.width + grid.spacing) / 220) - 1)) / Math.max(1, Math.floor((grid.width + grid.spacing) / 220)))
                    width: calcWidth
                    height: implicitHeight
                title: ApplicationWindow.window.t(nameKey)
                headerRight: StatusPill {
                    status: connected ? "online" : "offline"
                    label: connected ? ApplicationWindow.window.t("devices.connected") : ApplicationWindow.window.t("devices.notConnected")
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    RowLayout {
                        visible: kind === "tcp"
                        Layout.fillWidth: true
                        spacing: 12
                        FieldLabel { Layout.fillWidth: true; text: ApplicationWindow.window.t("devices.ipAddress") }
                        DeviceTextField {
                            id: hostField
                            text: page.tcpHost(port)
                            enabled: !connected && !deviceBackend.busy
                            onEditingFinished: page.updateTcpEndpoint(text, tcpPortField.text)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        FieldLabel { Layout.fillWidth: true; text: ApplicationWindow.window.t("devices.port") }
                        DeviceTextField {
                            id: tcpPortField
                            visible: kind === "tcp"
                            text: page.tcpPort(port)
                            enabled: !connected && !deviceBackend.busy
                            validator: IntValidator { bottom: 1; top: 65535 }
                            onEditingFinished: page.updateTcpEndpoint(hostField.text, text)
                        }
                        DeviceComboBox {
                            visible: kind !== "tcp"
                            model: page.portModel(port)
                            currentIndex: page.selectIndex(model, port)
                            enabled: !connected && !deviceBackend.busy
                            onActivated: deviceBackend.updateDevicePort(id, currentText)
                        }
                    }

                    RowLayout {
                        visible: kind !== "tcp"
                        Layout.fillWidth: true
                        spacing: 12
                        FieldLabel { Layout.fillWidth: true; text: ApplicationWindow.window.t("devices.baudRate") }
                        DeviceComboBox {
                            model: deviceBackend.supportedBaudRates()
                            currentIndex: page.selectIndex(model, baudRate)
                            enabled: !connected && !deviceBackend.busy
                            onActivated: deviceBackend.updateDeviceBaud(id, Number(currentText))
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        UnitFieldLabel {
                            label: kind === "tcp"
                                ? ApplicationWindow.window.t("devices.receiveRate")
                                : ApplicationWindow.window.t("devices.sampleRate")
                            unit: ApplicationWindow.window.t("unit.hz")
                        }
                        DeviceComboBox {
                            visible: kind !== "tcp"
                            model: deviceBackend.supportedRates(id)
                            currentIndex: page.selectIndex(model, sampleRate)
                            enabled: kind !== "tcp" && !connected && !deviceBackend.busy
                            onActivated: deviceBackend.updateDeviceSampleRate(id, Number(currentText))
                        }
                        DeviceTextField {
                            visible: kind === "tcp"
                            text: page.rateText(actualRate)
                            enabled: false
                        }
                    }

                    Item { Layout.fillHeight: true }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: ApplicationWindow.window.border
                        opacity: 0.65
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ToolbarButton {
                            Layout.fillWidth: true
                            iconName: "link"
                            text: connected ? page.disconnectText() : ApplicationWindow.window.t("devices.connect")
                            variant: connected ? "secondary" : "primary"
                            enabled: !deviceBackend.busy
                            onClicked: {
                                if (kind === "tcp") {
                                    page.updateTcpEndpoint(hostField.text, tcpPortField.text)
                                    waveformBackend.toggleConnection()
                                } else {
                                    connected ? deviceBackend.disconnectDevice(id) : deviceBackend.connectDevice(id)
                                }
                            }
                        }

                        ToolbarButton {
                            iconName: "flask-conical"
                            text: ""
                            enabled: !deviceBackend.busy
                            ToolTip.text: id === "epsilon" ? "EPS Rates" : ApplicationWindow.window.t("devices.test")
                            ToolTip.visible: hovered
                            ToolTip.delay: 400
                            onClicked: page.runDeviceTest(id)
                        }

                        ToolbarButton {
                            iconName: "info"
                            text: ""
                            ToolTip.text: ApplicationWindow.window.t("devices.details")
                            ToolTip.visible: hovered
                            ToolTip.delay: 400
                            onClicked: ApplicationWindow.window.currentPage = "detailedData"
                    }
                }
            }
            }
        }
    }

    Popup {
        id: epsilonRatesPopup
        modal: true
        anchors.centerIn: parent
        width: 420
        height: 360
        background: Rectangle { color: ApplicationWindow.window.card; border.color: ApplicationWindow.window.border; radius: 8 }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            Text { text: "EPSILON Packet Rates"; color: ApplicationWindow.window.text; font.bold: true }
            Text { Layout.fillWidth: true; text: "QML keeps the saved packet-rate profile and can apply grouped/custom values through the backend."; wrapMode: Text.Wrap; color: ApplicationWindow.window.muted; font.pixelSize: 11 * ApplicationWindow.window.scaleFactor }
            RowLayout {
                Layout.fillWidth: true
                ToolbarButton { iconName: "save"; text: "Save Grouped"; variant: "primary"; onClicked: { deviceBackend.saveEpsilonPacketRates({}, false); epsilonRatesPopup.close() } }
                ToolbarButton { iconName: "refresh-cw"; text: "Reconfigure"; onClicked: { deviceBackend.reconfigureEpsilonOutput(); epsilonRatesPopup.close() } }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: rtcmPort; Layout.fillWidth: true; placeholderText: "RTCM forward port" }
                TextField { id: rtcmBaud; Layout.preferredWidth: 100; text: "115200" }
            }
            ToolbarButton { iconName: "link"; text: "Configure RTCM Port"; onClicked: { deviceBackend.configureEpsilonRtcmPort(rtcmPort.text, Number(rtcmBaud.text)); epsilonRatesPopup.close() } }
            Item { Layout.fillHeight: true }
            ToolbarButton { iconName: "square"; text: "Close"; onClicked: epsilonRatesPopup.close() }
        }
    }
}
