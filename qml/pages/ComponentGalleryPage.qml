import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    id: page
    function t(key) { appBackend.language; return appBackend.t(key) }

    Flickable {
        anchors.fill: parent; anchors.margins: 12; clip: true
        contentWidth: width; contentHeight: Math.max(mainContent.height, height)
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Item {
            id: mainContent; width: parent.width - 16
            readonly property bool narrow: width < 1050
            readonly property int gap: ApplicationWindow.window.uiSpacing
            readonly property int leftW: narrow ? width : 340
            readonly property int rightW: narrow ? width : width - leftW - gap
            height: narrow ? leftPanel.height + gap + rightPanel.height : Math.max(leftPanel.height, rightPanel.height)

            Column { id: leftPanel; width: mainContent.leftW; spacing: ApplicationWindow.window.uiSpacing
                Card { width: parent.width; height: implicitHeight; title: t("components.title")
                    CardBody { Text { width: parent.width; text: t("components.description"); color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; wrapMode: Text.WordWrap } }
                }
                Card { width: parent.width; height: implicitHeight; title: t("components.globalStyle")
                    CardBody {
                        StyleSlider { lb: t("components.radius"); p: "uiRadius"; fr: 0; to: 16 }
                        StyleSlider { lb: t("components.controlHeight"); p: "uiControlHeight"; fr: 28; to: 44 }
                        StyleSlider { lb: t("components.buttonHeight"); p: "uiButtonHeight"; fr: 28; to: 44 }
                        StyleSlider { lb: t("components.cardHeaderHeight"); p: "uiCardHeaderHeight"; fr: 28; to: 44 }
                        StyleSlider { lb: t("components.cardPadding"); p: "uiCardPadding"; fr: 6; to: 20 }
                        StyleSlider { lb: t("components.controlPadding"); p: "uiControlPaddingX"; fr: 6; to: 18 }
                        StyleSlider { lb: t("components.spacing"); p: "uiSpacing"; fr: 4; to: 16 }
                        StyleSlider { lb: t("components.borderWidth"); p: "uiBorderWidth"; fr: 0; to: 2 }
                        StyleSlider { lb: t("components.fontSmall"); p: "uiSmallFontSize"; fr: 8; to: 14 }
                        StyleSlider { lb: t("components.fontBody"); p: "uiBodyFontSize"; fr: 10; to: 16 }
                        StyleSlider { lb: t("components.fontValue"); p: "uiValueFontSize"; fr: 11; to: 18 }
                        RowLayout { spacing: 8
                            Text { text: t("components.compactMode") + ":"; color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; Layout.preferredWidth: 130 }
                            Switch { checked: appBackend.uiCompactMode; onCheckedChanged: appBackend.uiCompactMode = checked }
                        }
                        RowLayout { spacing: 8
                            Text { text: t("components.debugOutlines") + ":"; color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; Layout.preferredWidth: 130 }
                            Switch { checked: appBackend.uiShowDebugOutlines; onCheckedChanged: appBackend.uiShowDebugOutlines = checked }
                        }
                        Row { spacing: 8
                            ToolbarButton { text: t("components.saveStyle"); iconName: "save"; variant: "primary"; onClicked: appBackend.saveUiStyle() }
                            ToolbarButton { text: t("components.resetStyle"); iconName: "rotate-ccw"; onClicked: appBackend.resetUiStyle() }
                        }
                    }
                }
            }

            Column { id: rightPanel; width: mainContent.rightW; spacing: ApplicationWindow.window.uiSpacing
                x: mainContent.narrow ? 0 : mainContent.leftW + mainContent.gap
                y: mainContent.narrow ? leftPanel.height + mainContent.gap : 0

                Card { width: parent.width; height: implicitHeight; title: t("components.buttons")
                    CardBody {
                        Flow { id: btnFlow; width: parent.width; spacing: 6
                            ToolbarButton { text: t("components.defaultBtn"); iconName: "scan" }
                            ToolbarButton { text: t("components.primaryBtn"); iconName: "wifi"; variant: "primary" }
                            ToolbarButton { text: t("components.dangerBtn"); iconName: "trash-2"; variant: "danger" }
                            ToolbarButton { text: t("components.disabled"); iconName: "square"; enabled: false }
                            ToolbarButton { iconName: "zap"; variant: "primary" }
                            ToolbarButton { text: t("components.longButton"); iconName: "settings" }
                        }
                        Item { width: 1; height: btnFlow.childrenRect.height }
                    }
                }

                Card { width: parent.width; height: implicitHeight; title: t("components.inputs")
                    CardBody {
                        Flow { id: inputFlow; width: parent.width; spacing: ApplicationWindow.window.uiSpacing
                            AppTextField { width: 200; placeholderText: t("components.placeholderText") }
                            AppTextField { width: 160; placeholderText: t("components.numberPlaceholder"); inputMethodHints: Qt.ImhDigitsOnly }
                            AppTextField { width: 200; text: t("components.passwordPlain"); echoMode: TextInput.Normal }
                            AppTextField { width: 160; placeholderText: t("components.disabled"); enabled: false }
                        }
                        Item { width: 1; height: inputFlow.childrenRect.height }
                    }
                }

                Card { width: parent.width; height: implicitHeight; title: t("components.comboboxes")
                    CardBody {
                        Flow { id: comboFlow; width: parent.width; spacing: ApplicationWindow.window.uiSpacing
                            Column { width: 220; spacing: 2
                                Text { text: t("components.combo"); color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
                                AppComboBox { width: parent.width; model: ["A","B","C"]; currentIndex: 0 }
                            }
                            Column { width: 240; spacing: 2
                                Text { text: t("components.portCombo"); color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
                                AppComboBox { width: parent.width; model: ["COM3","COM5","COM7"]; currentIndex: 0 }
                            }
                            Column { width: 240; spacing: 2
                                Text { text: t("components.mountPointCombo"); color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
                                AppEditableComboBox { width: parent.width; text: "AUTO"; model: ["AUTO", "RTCM33_GR", "RTCM32_GR"] }
                            }
                            Column { width: 260; spacing: 2
                                Text { text: t("components.editableCombo"); color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
                                AppEditableComboBox { width: parent.width; text: "CUSTOM_MOUNT"; placeholderText: t("components.editableCombo"); model: ["AUTO", "RTCM33_GR", "RTCM32_GR"] }
                            }
                        }
                        Item { width: 1; height: comboFlow.childrenRect.height }
                    }
                }

                Card { width: parent.width; height: implicitHeight; title: t("components.cards")
                    CardBody {
                        Flow { id: cardFlow; width: parent.width; spacing: 8
                            Card { width: 240; height: implicitHeight; title: t("components.staticCard")
                                CardBody { Text { text: t("components.cardBodyText"); color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor } }
                            }
                            Card { width: 260; height: implicitHeight; title: t("components.cardWithAction")
                                headerRight: ToolbarButton { iconName: "zap"; text: t("components.action") }
                                CardBody { Text { text: t("components.cardActionText"); color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor } }
                            }
                        }
                        Item { width: 1; height: cardFlow.childrenRect.height }
                    }
                }

                Card { width: parent.width; height: implicitHeight; title: t("components.statusMetrics")
                    CardBody {
                        Flow { id: pillFlow; width: parent.width; spacing: 6
                            StatusPill { label: t("components.connected"); status: "success" }
                            StatusPill { label: t("components.warning"); status: "warning" }
                            StatusPill { label: t("components.error"); status: "error" }
                            StatusPill { label: t("components.offline"); status: "offline" }
                        }
                        Item { width: 1; height: pillFlow.childrenRect.height }
                        RowLayout { Layout.fillWidth: true; spacing: 8
                            MetricCell { Layout.fillWidth: true; label: t("components.throughput"); value: "1250"; unit: "B/s" }
                            MetricCell { Layout.fillWidth: true; label: t("components.latency"); value: "---"; unit: "ms" }
                        }
                    }
                }

                Card { width: parent.width; height: implicitHeight; title: t("components.logs")
                    CardBody {
                        Text { text: t("components.diagLog"); color: ApplicationWindow.window.text; font.bold: true; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor }
                        LogBox { width: parent.width; height: 100; text: t("components.ntripConnected") + "\n" + t("components.mountAuto") + "\n" + t("components.rtcmActive") + "\n" + t("components.rateExample") }
                    }
                }
            }
        }
    }

    component CardBody: Column {
        x: ApplicationWindow.window.uiCardPadding; y: ApplicationWindow.window.uiCardPadding
        width: parent ? parent.width - ApplicationWindow.window.uiCardPadding * 2 : 0
        spacing: ApplicationWindow.window.uiSpacing; height: implicitHeight
    }

    component StyleSlider: RowLayout {
        property string lb: ""; property string p: ""; property int fr: 0; property int to: 10
        spacing: 6
        Text { text: parent.lb + ":"; color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; Layout.preferredWidth: 130 }
        Slider { Layout.fillWidth: true; from: parent.fr; to: parent.to; stepSize: 1; value: appBackend[parent.p]; onMoved: appBackend[parent.p] = Math.round(value) }
        Text { text: appBackend[parent.p] + " px"; color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; font.family: "Consolas"; Layout.preferredWidth: 44; horizontalAlignment: Text.AlignRight }
    }
}
