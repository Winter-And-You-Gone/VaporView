import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../components"

Item {
    id: page
    function t(key) { appBackend.language; return appBackend.t(key) }

    Flickable {
        anchors.fill: parent; anchors.margins: 12; clip: true
        contentWidth: width; contentHeight: Math.max(col.implicitHeight, height)
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: col; width: parent.width - 24; spacing: 12

            Card { Layout.fillWidth: true; title: t("components.title")
                Column { x: 12; y: 10; width: parent.width - 24; height: implicitHeight; spacing: 6
                    Text { width: parent.width; text: t("components.description"); color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; wrapMode: Text.WordWrap }
                }
            }

            Card { Layout.fillWidth: true; title: t("components.globalStyle")
                Column { x: 12; y: 10; width: parent.width - 24; height: implicitHeight; spacing: 6
                    StyleSlider { label: t("components.radius"); bp: "uiRadius"; fr: 0; to: 16 }
                    StyleSlider { label: t("components.controlHeight"); bp: "uiControlHeight"; fr: 28; to: 44 }
                    StyleSlider { label: t("components.buttonHeight"); bp: "uiButtonHeight"; fr: 28; to: 44 }
                    StyleSlider { label: t("components.cardHeaderHeight"); bp: "uiCardHeaderHeight"; fr: 28; to: 44 }
                    StyleSlider { label: t("components.cardPadding"); bp: "uiCardPadding"; fr: 6; to: 20 }
                    StyleSlider { label: t("components.controlPadding"); bp: "uiControlPaddingX"; fr: 6; to: 18 }
                    StyleSlider { label: t("components.spacing"); bp: "uiSpacing"; fr: 4; to: 16 }
                    StyleSlider { label: t("components.borderWidth"); bp: "uiBorderWidth"; fr: 0; to: 2 }
                    StyleSlider { label: t("components.fontSmall"); bp: "uiSmallFontSize"; fr: 8; to: 14 }
                    StyleSlider { label: t("components.fontBody"); bp: "uiBodyFontSize"; fr: 10; to: 16 }
                    StyleSlider { label: t("components.fontValue"); bp: "uiValueFontSize"; fr: 11; to: 18 }
                    RowLayout { spacing: 8
                        Text { text: t("components.compactMode") + ":"; color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; Layout.preferredWidth: 120 }
                        Switch { checked: appBackend.uiCompactMode; onCheckedChanged: appBackend.uiCompactMode = checked }
                    }
                    RowLayout { spacing: 8
                        Text { text: t("components.debugOutlines") + ":"; color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; Layout.preferredWidth: 120 }
                        Switch { checked: appBackend.uiShowDebugOutlines; onCheckedChanged: appBackend.uiShowDebugOutlines = checked }
                    }
                    Row { spacing: 8
                        ToolbarButton { text: t("components.saveStyle"); iconName: "save"; variant: "primary"; onClicked: appBackend.saveUiStyle() }
                        ToolbarButton { text: t("components.resetStyle"); iconName: "rotate-ccw"; onClicked: appBackend.resetUiStyle() }
                    }
                }
            }

            Card { Layout.fillWidth: true; title: t("components.buttons")
                Column { x: 12; y: 10; width: parent.width - 24; height: implicitHeight; spacing: 8
                    Flow { width: parent.width; spacing: 6
                        ToolbarButton { text: t("components.defaultBtn"); iconName: "scan" }
                        ToolbarButton { text: t("components.primaryBtn"); iconName: "wifi"; variant: "primary" }
                        ToolbarButton { text: t("components.dangerBtn"); iconName: "trash-2"; variant: "danger" }
                        ToolbarButton { text: t("components.disabled"); iconName: "square"; enabled: false }
                        ToolbarButton { iconName: "zap"; variant: "primary" }
                        ToolbarButton { text: "Long Button Text Here"; iconName: "settings" }
                    }
                }
            }

            Card { Layout.fillWidth: true; title: t("components.inputs")
                Column { x: 12; y: 10; width: parent.width - 24; height: implicitHeight; spacing: 6
                    RowLayout { spacing: 8
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text { text: t("components.textField"); color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
                            TextField { Layout.fillWidth: true; placeholderText: "Placeholder text"; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; color: ApplicationWindow.window.text; leftPadding: ApplicationWindow.window.uiControlPaddingX; rightPadding: ApplicationWindow.window.uiControlPaddingX; background: Rectangle { implicitHeight: ApplicationWindow.window.uiControlHeight; radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.card; border.color: ApplicationWindow.window.border; border.width: ApplicationWindow.window.uiBorderWidth } }
                        }
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text { text: t("components.numberField"); color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
                            TextField { Layout.fillWidth: true; placeholderText: "12345"; inputMethodHints: Qt.ImhDigitsOnly; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; color: ApplicationWindow.window.text; leftPadding: ApplicationWindow.window.uiControlPaddingX; rightPadding: ApplicationWindow.window.uiControlPaddingX; background: Rectangle { implicitHeight: ApplicationWindow.window.uiControlHeight; radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.card; border.color: ApplicationWindow.window.border; border.width: ApplicationWindow.window.uiBorderWidth } }
                        }
                    }
                }
            }

            Card { Layout.fillWidth: true; title: t("components.comboboxes")
                Column { x: 12; y: 10; width: parent.width - 24; height: implicitHeight; spacing: 6
                    RowLayout { spacing: 8
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text { text: t("components.combo"); color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
                            ComboBox { Layout.fillWidth: true; model: ["COM3", "COM4"]; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; implicitHeight: ApplicationWindow.window.uiControlHeight; background: Rectangle { radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.card; border.color: ApplicationWindow.window.border; border.width: ApplicationWindow.window.uiBorderWidth } }
                        }
                        ColumnLayout { Layout.fillWidth: true; spacing: 2
                            Text { text: t("components.editableCombo"); color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
                            ComboBox { Layout.fillWidth: true; editable: true; model: ["AUTO", "RTCM32"]; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; implicitHeight: ApplicationWindow.window.uiControlHeight; background: Rectangle { radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.card; border.color: ApplicationWindow.window.border; border.width: ApplicationWindow.window.uiBorderWidth } }
                        }
                    }
                }
            }

            Card { Layout.fillWidth: true; title: t("components.cards")
                Column { x: 12; y: 10; width: parent.width - 24; height: implicitHeight; spacing: 8
                    Card { width: parent.width; height: implicitHeight; title: t("components.staticCard")
                        Column { x: 12; y: 10; width: parent.width - 24; height: implicitHeight; spacing: 4
                            Text { text: "This is body text inside a card."; color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor }
                        }
                    }
                    Card { width: parent.width; height: implicitHeight; title: t("components.cardWithAction")
                        headerRight: ToolbarButton { iconName: "zap"; text: "Action" }
                        Column { x: 12; y: 10; width: parent.width - 24; height: implicitHeight; spacing: 4
                            Text { text: "Card with header action button."; color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor }
                        }
                    }
                }
            }

            Card { Layout.fillWidth: true; title: t("components.statusMetrics")
                Column { x: 12; y: 10; width: parent.width - 24; height: implicitHeight; spacing: 8
                    Flow { width: parent.width; spacing: 6
                        Rectangle { implicitWidth: l1.implicitWidth + 12; implicitHeight: 22; radius: 4; color: Qt.rgba(0.29,0.86,0.50,0.12); border.color: Qt.rgba(0.29,0.86,0.50,0.25)
                            Text { id: l1; text: "Connected"; color: ApplicationWindow.window.ok; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor; font.bold: true; anchors.centerIn: parent } }
                        Rectangle { implicitWidth: l2.implicitWidth + 12; implicitHeight: 22; radius: 4; color: Qt.rgba(0.98,0.75,0.14,0.12); border.color: Qt.rgba(0.98,0.75,0.14,0.25)
                            Text { id: l2; text: "Warning"; color: ApplicationWindow.window.warning; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor; font.bold: true; anchors.centerIn: parent } }
                        Rectangle { implicitWidth: l3.implicitWidth + 12; implicitHeight: 22; radius: 4; color: Qt.rgba(0.97,0.44,0.44,0.12); border.color: Qt.rgba(0.97,0.44,0.44,0.25)
                            Text { id: l3; text: "Error"; color: ApplicationWindow.window.danger; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor; font.bold: true; anchors.centerIn: parent } }
                        Rectangle { implicitWidth: l4.implicitWidth + 12; implicitHeight: 22; radius: 4; color: Qt.rgba(0.58,0.64,0.72,0.12); border.color: Qt.rgba(0.58,0.64,0.72,0.25)
                            Text { id: l4; text: "Offline"; color: ApplicationWindow.window.offline; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor; font.bold: true; anchors.centerIn: parent } }
                    }
                    RowLayout { spacing: 8
                        Rectangle { Layout.fillWidth: true; implicitHeight: 52; radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.cardAlt; border.color: ApplicationWindow.window.border
                            Column { anchors.centerIn: parent; spacing: 2
                                Text { text: "Throughput"; color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
                                Text { text: "1250 B/s"; color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiValueFontSize * ApplicationWindow.window.scaleFactor; font.bold: true; font.family: "Consolas" } } }
                        Rectangle { Layout.fillWidth: true; implicitHeight: 52; radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.cardAlt; border.color: ApplicationWindow.window.border
                            Column { anchors.centerIn: parent; spacing: 2
                                Text { text: "Latency"; color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor }
                                Text { text: "--- ms"; color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiValueFontSize * ApplicationWindow.window.scaleFactor; font.bold: true; font.family: "Consolas" } } }
                    }
                }
            }

            Card { Layout.fillWidth: true; title: t("components.logs")
                Column { x: 12; y: 10; width: parent.width - 24; height: implicitHeight; spacing: 6
                    Text { text: "Diagnostic Log"; color: ApplicationWindow.window.text; font.bold: true; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor }
                    Rectangle { width: parent.width; height: 120; radius: ApplicationWindow.window.uiRadius; color: ApplicationWindow.window.secondary; border.color: ApplicationWindow.window.border
                        ScrollView { anchors.fill: parent; anchors.margins: 8; clip: true
                            TextArea { text: "[INFO]  NTRIP connected\n[INFO]  Mount: AUTO\n[INFO]  RTCM stream active\n[DEBUG] Bps: 1250"
                                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; color: ApplicationWindow.window.text; font.family: "Consolas"
                                font.pixelSize: ApplicationWindow.window.uiSmallFontSize * ApplicationWindow.window.scaleFactor; background: Rectangle { color: "transparent" } }
                        }
                    }
                }
            }
        }
    }

    component StyleSlider: RowLayout {
        property string label: ""; property string bp: ""; property int fr: 0; property int to: 10
        spacing: 6
        Text { text: parent.label + ":"; color: ApplicationWindow.window.muted; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; Layout.preferredWidth: 120 }
        Slider { Layout.fillWidth: true; from: parent.fr; to: parent.to; stepSize: 1; value: appBackend[parent.bp]; onMoved: appBackend[parent.bp] = value }
        Text { text: appBackend[parent.bp] + " px"; color: ApplicationWindow.window.text; font.pixelSize: ApplicationWindow.window.uiBodyFontSize * ApplicationWindow.window.scaleFactor; font.family: "Consolas"; Layout.preferredWidth: 44; horizontalAlignment: Text.AlignRight }
    }
}
