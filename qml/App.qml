import QtQuick
import QtQuick.Controls
import FluentUI 1.0

FluWindow {
    id: window

    title: "VaporView"
    width: 1440
    height: 940
    minimumWidth: 1180
    minimumHeight: 760
    launchMode: FluWindowType.SingleTask
    fitsAppBarWindows: true

    appBar: FluAppBar {
        height: 36
        showDark: true
    }

    Timer {
        id: startupPageTimer
        interval: 100
        repeat: false
        onTriggered: navigationView.setCurrentIndex(0)
    }

    FluNavigationView {
        id: navigationView
        anchors.fill: parent
        pageMode: FluNavigationViewType.NoStack
        displayMode: FluNavigationViewType.Open
        title: "VaporView"
        cellWidth: 280
        topPadding: 0

        actionItem: Component {
            Item {
                id: actionContainer
                width: actionRow.width + titleBarReserve
                height: actionRow.height

                readonly property int titleBarReserve: {
                    if (window.appBar && window.appBar.layoutStandardbuttons) {
                        return window.appBar.layoutStandardbuttons.width + 12
                    }
                    return 212
                }

                Component.onCompleted: {
                    window.setHitTestVisible(actionContainer)
                }

                Row {
                    id: actionRow
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: parent.titleBarReserve
                    spacing: 8

                    FluButton {
                        text: appController.english ? "中文" : "EN"
                        onClicked: appController.toggleLanguage()
                    }

                    FluFilledButton {
                        text: appController.english ? "RTK Service" : "RTK 服务"
                        onClicked: navigationView.push("qrc:/qt/qml/VaporViewApp/qml/page/RtkPage.qml")
                    }
                }
            }
        }

        items: FluObject {
            FluPaneItem {
                title: appController.english ? "Overview" : "总览"
                icon: FluentIcons.Home
                url: "qrc:/qt/qml/VaporViewApp/qml/page/DashboardPage.qml"
                onTap: navigationView.push(url)
            }

            FluPaneItem {
                title: appController.english ? "Devices" : "设备"
                icon: FluentIcons.TVMonitor
                url: "qrc:/qt/qml/VaporViewApp/qml/page/DevicesPage.qml"
                onTap: navigationView.push(url)
            }

            FluPaneItem {
                title: appController.english ? "RTK" : "RTK"
                icon: FluentIcons.MapDrive
                url: "qrc:/qt/qml/VaporViewApp/qml/page/RtkPage.qml"
                onTap: navigationView.push(url)
            }

            FluPaneItem {
                title: appController.english ? "Sessions" : "会话"
                icon: FluentIcons.FolderOpen
                url: "qrc:/qt/qml/VaporViewApp/qml/page/SessionPage.qml"
                onTap: navigationView.push(url)
            }
        }

        footerItems: FluObject {
            FluPaneItemSeparator {
            }

            FluPaneItem {
                title: appController.english ? "Tools" : "工具"
                icon: FluentIcons.Settings
                url: "qrc:/qt/qml/VaporViewApp/qml/page/ToolsPage.qml"
                onTap: navigationView.push(url)
            }
        }

        Component.onCompleted: {
            startupPageTimer.start()
        }
    }
}
