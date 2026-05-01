import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: topBar

    readonly property bool serialConnected: deviceBackend.connected
    readonly property bool tcpConnected: waveformBackend.connected
    readonly property bool anyDeviceConnected: serialConnected || tcpConnected

    function connectAllDevices() {
        if (appBackend.english)
            deviceBackend.appendLogLine("Global connection requested: 4 serial devices plus 1 TCP waveform source.", "info")
        deviceBackend.appendLogLine("全局连接已发起：4 个串口设备 + 1 个 TCP 波形源。", "info")
        if (!waveformBackend.connected)
            waveformBackend.connectToHost()
        if (!deviceBackend.connected && !deviceBackend.busy)
            deviceBackend.connectDevices()
    }

    function disconnectAllDevices() {
        if (waveformBackend.connected || waveformBackend.statusText === "Connecting...")
            waveformBackend.disconnectFromHost()
        if (deviceBackend.connected || deviceBackend.busy)
            deviceBackend.disconnectDevices()
    }

    color: ApplicationWindow.window.card
    border.color: ApplicationWindow.window.border

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        spacing: 10

        Image {
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            source: ApplicationWindow.window.dark
                    ? "../assets/logo/vaporview_logo_dark.png"
                    : "../assets/logo/vaporview_logo_light.png"
            fillMode: Image.PreserveAspectFit
            smooth: true
            mipmap: true
        }
        Text {
            text: ApplicationWindow.window.t("app.title")
            color: ApplicationWindow.window.text
            font.pixelSize: 13 * ApplicationWindow.window.scaleFactor
            font.bold: true
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 22; color: ApplicationWindow.window.border }
        Text {
            Layout.fillWidth: true
            text: ApplicationWindow.window.t("topbar.session") + ": " + (recordingBackend.sessionDirectory || "---")
            color: ApplicationWindow.window.muted
            font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
            elide: Text.ElideMiddle
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: ApplicationWindow.window.border }
        StatusPill {
            status: deviceBackend.connected ? "online" : "warning"
            label: deviceBackend.connected ? ApplicationWindow.window.t("topbar.systemOnline") : ApplicationWindow.window.t("topbar.partialOffline")
        }
        StatusPill {
            status: recordingBackend.recording ? "recording" : "warning"
            label: recordingBackend.recording ? ApplicationWindow.window.t("topbar.recording") : recordingBackend.paused ? ApplicationWindow.window.t("topbar.paused") : ApplicationWindow.window.t("topbar.stopped")
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: ApplicationWindow.window.border }
        RowLayout {
            spacing: 10
            Layout.maximumWidth: 360
            Text {
                text: ApplicationWindow.window.t("topbar.recordUsage") + ": " + recordingBackend.recordUsageText
                color: ApplicationWindow.window.muted
                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                elide: Text.ElideRight
            }
            Text {
                text: ApplicationWindow.window.t("topbar.diskRemaining") + ": " + recordingBackend.diskRemainingText
                color: ApplicationWindow.window.muted
                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                elide: Text.ElideRight
            }
            Text {
                text: ApplicationWindow.window.t("topbar.totalDisk") + ": " + recordingBackend.diskTotalText
                color: ApplicationWindow.window.muted
                font.pixelSize: 10 * ApplicationWindow.window.scaleFactor
                elide: Text.ElideRight
            }
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: ApplicationWindow.window.border }
        ToolbarButton {
            iconName: topBar.anyDeviceConnected ? "unlink" : "link"
            text: topBar.anyDeviceConnected ? ApplicationWindow.window.t("topbar.disconnect") : ApplicationWindow.window.t("topbar.connect")
            variant: topBar.anyDeviceConnected ? "danger" : "primary"
            enabled: true
            onClicked: topBar.anyDeviceConnected ? topBar.disconnectAllDevices() : topBar.connectAllDevices()
        }
        ToolbarButton {
            visible: deviceBackend.connectionInProgress
            iconName: "square"
            text: ApplicationWindow.window.t("topbar.cancel")
            variant: "danger"
            onClicked: deviceBackend.cancelConnect()
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: ApplicationWindow.window.border }
        ToolbarButton {
            iconName: recordingBackend.recording ? "square" : "play"
            text: recordingBackend.recording ? ApplicationWindow.window.t("topbar.stop") : ApplicationWindow.window.t("topbar.start")
            variant: recordingBackend.recording ? "danger" : "primary"
            onClicked: recordingBackend.recording ? recordingBackend.stopRecording() : recordingBackend.startRecording()
        }
        ToolbarButton {
            iconName: recordingBackend.paused ? "play" : "pause"
            text: recordingBackend.paused ? ApplicationWindow.window.t("topbar.resume") : ApplicationWindow.window.t("topbar.pause")
            enabled: recordingBackend.recording || recordingBackend.paused
            onClicked: recordingBackend.paused ? recordingBackend.startRecording() : recordingBackend.pauseRecording()
        }
        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: ApplicationWindow.window.border }
        ToolbarButton {
            text: appBackend.language === "zh" ? "EN" : "中"
            onClicked: appBackend.toggleLanguage()
        }
        ToolbarButton {
            text: appBackend.dark ? ApplicationWindow.window.t("settings.light") : ApplicationWindow.window.t("settings.dark")
            onClicked: appBackend.toggleTheme()
        }
    }
}
