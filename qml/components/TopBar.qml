import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    color: ApplicationWindow.window.card
    border.color: ApplicationWindow.window.border

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            radius: 4
            color: ApplicationWindow.window.primary
            Text { anchors.centerIn: parent; text: "V"; color: "white"; font.pixelSize: 11; font.bold: true }
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
        StatusPill {
            status: deviceBackend.connected ? "online" : "warning"
            label: deviceBackend.connected ? ApplicationWindow.window.t("topbar.systemOnline") : ApplicationWindow.window.t("topbar.partialOffline")
        }
        StatusPill {
            status: recordingBackend.recording ? "recording" : "warning"
            label: recordingBackend.recording ? ApplicationWindow.window.t("topbar.recording") : recordingBackend.paused ? ApplicationWindow.window.t("topbar.paused") : ApplicationWindow.window.t("topbar.stopped")
        }
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
        ToolbarButton {
            text: deviceBackend.connected ? ApplicationWindow.window.t("topbar.disconnect") : ApplicationWindow.window.t("topbar.connect")
            variant: deviceBackend.connected ? "danger" : "primary"
            enabled: !deviceBackend.busy
            onClicked: deviceBackend.connected ? deviceBackend.disconnectDevices() : deviceBackend.connectDevices()
        }
        ToolbarButton {
            visible: deviceBackend.connectionInProgress
            text: ApplicationWindow.window.t("topbar.cancel")
            variant: "danger"
            onClicked: deviceBackend.cancelConnect()
        }
        ToolbarButton {
            text: recordingBackend.recording ? ApplicationWindow.window.t("topbar.stop") : ApplicationWindow.window.t("topbar.start")
            variant: recordingBackend.recording ? "danger" : "primary"
            onClicked: recordingBackend.recording ? recordingBackend.stopRecording() : recordingBackend.startRecording()
        }
        ToolbarButton {
            text: recordingBackend.paused ? ApplicationWindow.window.t("topbar.resume") : ApplicationWindow.window.t("topbar.pause")
            enabled: recordingBackend.recording || recordingBackend.paused
            onClicked: recordingBackend.paused ? recordingBackend.startRecording() : recordingBackend.pauseRecording()
        }
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
