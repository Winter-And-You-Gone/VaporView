import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import FluentUI 1.0

FluScrollablePage {
    title: appController.english ? "RTK Service" : "RTK 服务"
    padding: 12
    spacing: 12

    FileDialog {
        id: saveConfigDialog
        title: appController.english ? "Save RTK Profile" : "保存 RTK 配置"
        fileMode: FileDialog.SaveFile
        nameFilters: [appController.english ? "INI files (*.ini)" : "INI 文件 (*.ini)"]
        defaultSuffix: "ini"
        onAccepted: rtkController.saveProfileToUrl(selectedFile)
    }

    FileDialog {
        id: loadConfigDialog
        title: appController.english ? "Load RTK Profile" : "加载 RTK 配置"
        fileMode: FileDialog.OpenFile
        nameFilters: [appController.english ? "INI files (*.ini)" : "INI 文件 (*.ini)"]
        onAccepted: rtkController.loadProfileFromUrl(selectedFile)
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: headerLayout.implicitHeight + 40

        ColumnLayout {
            id: headerLayout
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10

            FluText {
                text: appController.english ? "Embedded NTRIP Console" : "内嵌 NTRIP 控制台"
                font: FluTextStyle.Title
            }

            FluText {
                text: rtkController.statusText
                font: FluTextStyle.BodyStrong
            }
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: width > 1180 ? 2 : 1
        columnSpacing: 12
        rowSpacing: 12

        FluFrame {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            Layout.preferredHeight: ntripLayout.implicitHeight + 36

            ColumnLayout {
                id: ntripLayout
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

                FluText {
                    text: appController.english ? "NTRIP Server" : "NTRIP 服务器"
                    font: FluTextStyle.BodyStrong
                }

                FluTextBox {
                    Layout.fillWidth: true
                    text: rtkController.server
                    placeholderText: appController.english ? "Server host" : "服务器地址"
                    onTextChanged: rtkController.server = text
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: width > 720 ? 2 : 1
                    columnSpacing: 10
                    rowSpacing: 10

                    FluTextBox {
                        Layout.fillWidth: true
                        text: rtkController.port
                        placeholderText: appController.english ? "Port" : "端口"
                        onTextChanged: rtkController.port = text
                    }

                    FluTextBox {
                        Layout.fillWidth: true
                        text: rtkController.username
                        placeholderText: appController.english ? "Username" : "用户名"
                        onTextChanged: rtkController.username = text
                    }
                }

                FluPasswordBox {
                    Layout.fillWidth: true
                    text: rtkController.password
                    placeholderText: appController.english ? "Password" : "密码"
                    onTextChanged: rtkController.password = text
                }

                FluComboBox {
                    Layout.fillWidth: true
                    editable: true
                    model: rtkController.mountpointOptions
                    currentIndex: -1
                    editText: rtkController.mountpoint
                    onEditTextChanged: rtkController.mountpoint = editText
                    onActivated: rtkController.mountpoint = currentText
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 10

                    FluButton {
                        text: appController.english ? "Detect Mountpoints" : "检测挂载点"
                        disabled: rtkController.busy
                        onClicked: rtkController.fetchMountpoints()
                    }

                    FluButton {
                        text: appController.english ? "Load Profile" : "加载配置"
                        disabled: rtkController.busy
                        onClicked: loadConfigDialog.open()
                    }

                    FluButton {
                        text: appController.english ? "Save Profile" : "保存配置"
                        disabled: rtkController.busy
                        onClicked: saveConfigDialog.open()
                    }
                }
            }
        }

        FluFrame {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            Layout.preferredHeight: serialLayout.implicitHeight + 36

            ColumnLayout {
                id: serialLayout
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

                FluText {
                    text: appController.english ? "Serial Output" : "串口输出"
                    font: FluTextStyle.BodyStrong
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: width > 640 ? 2 : 1
                    columnSpacing: 10
                    rowSpacing: 10

                    FluComboBox {
                        Layout.fillWidth: true
                        editable: true
                        model: rtkController.portOptions
                        currentIndex: -1
                        editText: rtkController.outputPort
                        onEditTextChanged: rtkController.outputPort = editText
                        onActivated: rtkController.outputPort = currentText
                    }

                    FluButton {
                        text: appController.english ? "Refresh Ports" : "刷新串口"
                        disabled: rtkController.busy
                        onClicked: rtkController.refreshPorts()
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: width > 920 ? 3 : 1
                    columnSpacing: 10
                    rowSpacing: 10

                    FluComboBox {
                        Layout.fillWidth: true
                        editable: true
                        model: rtkController.baudOptions
                        currentIndex: -1
                        editText: rtkController.baudrate
                        onEditTextChanged: rtkController.baudrate = editText
                        onActivated: rtkController.baudrate = currentText
                    }

                    FluComboBox {
                        Layout.fillWidth: true
                        editable: true
                        model: rtkController.timingOptions
                        currentIndex: -1
                        editText: rtkController.timeoutMs
                        onEditTextChanged: rtkController.timeoutMs = editText
                        onActivated: rtkController.timeoutMs = currentText
                    }

                    FluComboBox {
                        Layout.fillWidth: true
                        editable: true
                        model: rtkController.timingOptions
                        currentIndex: -1
                        editText: rtkController.reconnectMs
                        onEditTextChanged: rtkController.reconnectMs = editText
                        onActivated: rtkController.reconnectMs = currentText
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 10

                    FluFilledButton {
                        text: appController.english ? "Start" : "启动"
                        disabled: rtkController.running || rtkController.busy
                        onClicked: rtkController.startService()
                    }

                    FluButton {
                        text: appController.english ? "Stop" : "停止"
                        disabled: !rtkController.running || rtkController.busy
                        onClicked: rtkController.stopService()
                    }

                    FluButton {
                        text: appController.english ? "No-Signal Test" : "无信号测试"
                        disabled: rtkController.running || rtkController.busy
                        onClicked: rtkController.runNoSignalTest()
                    }
                }
            }
        }
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: ggaLayout.implicitHeight + 36

        ColumnLayout {
            id: ggaLayout
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                FluText {
                    text: appController.english ? "GGA Monitor" : "GGA 监视"
                    font: FluTextStyle.BodyStrong
                }

                Item { Layout.fillWidth: true }

                FluButton {
                    text: rtkController.ggaMonitorEnabled
                          ? (appController.english ? "Stop Reading" : "停止读取")
                          : (appController.english ? "Read GGA" : "读取 GGA")
                    disabled: rtkController.busy
                    onClicked: rtkController.toggleGgaMonitor()
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: width > 760 ? 3 : 1
                columnSpacing: 10
                rowSpacing: 10

                FluComboBox {
                    Layout.preferredWidth: 220
                    editable: true
                    model: rtkController.portOptions
                    currentIndex: -1
                    editText: rtkController.ggaPort
                    onEditTextChanged: rtkController.ggaPort = editText
                    onActivated: rtkController.ggaPort = currentText
                }

                FluText {
                    text: rtkController.ggaStatusText
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }

                FluText {
                    text: rtkController.ggaFrequencyText
                    color: "#2563eb"
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                radius: 10
                color: "#0f172a"

                ListView {
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    spacing: 4
                    model: rtkController.ggaLogModel

                    delegate: FluText {
                        width: ListView.view.width
                        text: typeof modelData === "undefined" ? "" : String(modelData)
                        color: "#dbeafe"
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }

    FluFrame {
        Layout.fillWidth: true
        Layout.preferredHeight: 320

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            FluText {
                text: appController.english ? "RTK Event Log" : "RTK 事件日志"
                font: FluTextStyle.BodyStrong
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: "#0b1220"

                ListView {
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    spacing: 4
                    model: rtkController.logModel

                    delegate: FluText {
                        width: ListView.view.width
                        text: typeof modelData === "undefined" ? "" : String(modelData)
                        color: "#e2e8f0"
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }
    }
}
