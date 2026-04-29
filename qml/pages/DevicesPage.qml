import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ToolbarButton { text: ApplicationWindow.window.t("devices.autoDetect"); variant: "primary"; onClicked: deviceBackend.autoDetectPortsOrCancel() }
            ToolbarButton { text: ApplicationWindow.window.t("devices.refreshPorts"); onClicked: deviceBackend.refreshPorts() }
            Item { Layout.fillWidth: true }
            ToolbarButton { text: ApplicationWindow.window.t("devices.connectAll"); variant: "primary"; enabled: !deviceBackend.busy; onClicked: deviceBackend.connectDevices() }
            ToolbarButton { text: ApplicationWindow.window.t("devices.disconnectAll"); variant: "danger"; enabled: !deviceBackend.busy || deviceBackend.connected; onClicked: deviceBackend.disconnectDevices() }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            cellWidth: Math.max(300, width / 3)
            cellHeight: 220
            model: deviceBackend.devices
            delegate: Card {
                width: grid.cellWidth - 10
                height: grid.cellHeight - 10
                title: displayName

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        StatusPill {
                            status: connected ? "online" : "warning"
                            label: connected ? ApplicationWindow.window.t("devices.connected") : ApplicationWindow.window.t("devices.notConnected")
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            text: actualRate > 0 ? Number(actualRate).toFixed(1) + " Hz" : "-- Hz"
                            color: ApplicationWindow.window.muted
                            font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                        }
                    }

                    TextField {
                        Layout.fillWidth: true
                        text: port
                        placeholderText: kind === "tcp" ? ApplicationWindow.window.t("devices.ipAddress") : ApplicationWindow.window.t("devices.port")
                        enabled: !connected && !deviceBackend.busy
                        onEditingFinished: {
                            if (kind === "tcp") {
                                var parts = text.split(":")
                                waveformBackend.host = parts[0]
                                if (parts.length > 1) waveformBackend.port = Number(parts[1])
                            } else {
                                deviceBackend.updateDevicePort(id, text)
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            Layout.fillWidth: true
                            model: deviceBackend.supportedBaudRates()
                            currentIndex: Math.max(0, model.indexOf(String(baudRate)))
                            enabled: kind !== "tcp" && !connected && !deviceBackend.busy
                            onActivated: deviceBackend.updateDeviceBaud(id, Number(currentText))
                        }
                        ComboBox {
                            Layout.fillWidth: true
                            model: deviceBackend.supportedRates(id)
                            currentIndex: Math.max(0, model.indexOf(String(sampleRate)))
                            enabled: kind !== "tcp" && !connected && !deviceBackend.busy
                            onActivated: deviceBackend.updateDeviceSampleRate(id, Number(currentText))
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: statusText
                        color: ApplicationWindow.window.muted
                        font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                        elide: Text.ElideRight
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        ToolbarButton {
                            Layout.fillWidth: true
                            text: connected ? ApplicationWindow.window.t("topbar.disconnect") : ApplicationWindow.window.t("devices.connect")
                            variant: connected ? "danger" : "primary"
                            enabled: !deviceBackend.busy
                            onClicked: {
                                if (kind === "tcp") waveformBackend.toggleConnection()
                                else connected ? deviceBackend.disconnectDevices() : deviceBackend.connectDevices()
                            }
                        }
                        ToolbarButton {
                            visible: id === "epsilon"
                            text: "EPS Rates"
                            onClicked: epsilonRatesPopup.open()
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
        background: Rectangle { color: ApplicationWindow.window.card; border.color: ApplicationWindow.window.border; radius: 6 }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            Text { text: "EPSILON Packet Rates"; color: ApplicationWindow.window.text; font.bold: true }
            Text { Layout.fillWidth: true; text: "QML keeps the saved packet-rate profile and can apply grouped/custom values through the backend."; wrapMode: Text.Wrap; color: ApplicationWindow.window.muted; font.pixelSize: 11 }
            RowLayout {
                Layout.fillWidth: true
                ToolbarButton { text: "Save Grouped"; variant: "primary"; onClicked: { deviceBackend.saveEpsilonPacketRates({}, false); epsilonRatesPopup.close() } }
                ToolbarButton { text: "Reconfigure"; onClicked: { deviceBackend.reconfigureEpsilonOutput(); epsilonRatesPopup.close() } }
            }
            RowLayout {
                Layout.fillWidth: true
                TextField { id: rtcmPort; Layout.fillWidth: true; placeholderText: "RTCM forward port" }
                TextField { id: rtcmBaud; Layout.preferredWidth: 100; text: "115200" }
            }
            ToolbarButton { text: "Configure RTCM Port"; onClicked: { deviceBackend.configureEpsilonRtcmPort(rtcmPort.text, Number(rtcmBaud.text)); epsilonRatesPopup.close() } }
            Item { Layout.fillHeight: true }
            ToolbarButton { text: "Close"; onClicked: epsilonRatesPopup.close() }
        }
    }
}
