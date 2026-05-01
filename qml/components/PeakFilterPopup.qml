import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: popup

    modal: true
    focus: true
    width: 420
    padding: 16
    anchors.centerIn: Overlay.overlay

    readonly property color panelColor: ApplicationWindow.window.card
    readonly property color fieldColor: ApplicationWindow.window.dark ? ApplicationWindow.window.cardAlt : "#ffffff"
    readonly property color fieldHoverColor: ApplicationWindow.window.dark ? Qt.rgba(0.15, 0.23, 0.36, 1.0) : "#f8fafc"
    readonly property color fieldTextColor: ApplicationWindow.window.text
    readonly property color fieldBorderColor: ApplicationWindow.window.border
    readonly property color focusBorderColor: ApplicationWindow.window.dark ? "#60a5fa" : "#1d4ed8"
    readonly property color mutedTextColor: ApplicationWindow.window.muted

    function modeText(mode) {
        var english = appBackend.language === "en"
        if (mode === 1) return english ? "IQR Outlier Filter" : "IQR 异常值过滤"
        if (mode === 2) return english ? "Keep Range" : "保留区间"
        if (mode === 3) return english ? "Exclude Range" : "排除区间"
        return english ? "Off" : "关闭"
    }

    function parseEndIndex(text) {
        var value = String(text || "").trim().toLowerCase()
        if (value.length === 0 || value === "整帧" || value === "full frame" || value === "end")
            return 0
        return Math.max(0, Number(value))
    }

    function parseStartIndex(text) {
        var n = Number(String(text || "").trim())
        return isFinite(n) ? Math.max(0, Math.floor(n)) : NaN
    }

    function parseRangeNumber(text) {
        var n = Number(String(text || "").trim())
        return isFinite(n) ? n : NaN
    }

    function showValidation(message) {
        validationText.text = message
        validationText.visible = true
        if (deviceBackend && deviceBackend.appendLogLine)
            deviceBackend.appendLogLine(message, "warning")
    }

    function resetFields() {
        validationText.visible = false
        validationText.text = ""
        startField.text = String(waveformBackend.peakSearchStartIndex)
        endField.text = waveformBackend.peakSearchEndIndex <= 0
                ? (appBackend.language === "en" ? "Full Frame" : "整帧")
                : String(waveformBackend.peakSearchEndIndex)
        modeCombo.currentIndex = Math.max(0, modeCombo.indexOfValue(waveformBackend.peakFilterMode))
        minField.text = Number(waveformBackend.filterMin).toFixed(6)
        maxField.text = Number(waveformBackend.filterMax).toFixed(6)
    }

    function applySettings() {
        var english = appBackend.language === "en"
        var startIndex = parseStartIndex(startField.text)
        var endIndex = parseEndIndex(endField.text)
        var mode = Number(modeCombo.currentValue)
        var minValue = parseRangeNumber(minField.text)
        var maxValue = parseRangeNumber(maxField.text)
        if (!isFinite(startIndex)) {
            showValidation(english ? "Search Start must be a valid non-negative integer." : "搜索起点必须是有效的非负整数。")
            return
        }
        if (!isFinite(endIndex)) {
            showValidation(english ? "Search End must be a valid non-negative integer, or Full Frame." : "搜索终点必须是有效的非负整数，或者设置为整帧。")
            return
        }
        if (endIndex > 0 && endIndex <= startIndex) {
            showValidation(english ? "Search End must be greater than Search Start, or set to Full Frame." : "搜索终点必须大于搜索起点，或者设置为整帧。")
            return
        }
        if ((mode === 2 || mode === 3) && (!isFinite(minValue) || !isFinite(maxValue))) {
            showValidation(english ? "Please enter valid numeric range values." : "请输入有效的数值区间。")
            return
        }
        waveformBackend.configurePeakSettings(
                    startIndex,
                    endIndex,
                    mode,
                    minValue,
                    maxValue)
        close()
    }

    background: Rectangle {
        radius: 10
        color: popup.panelColor
        border.color: popup.fieldBorderColor
    }

    ColumnLayout {
        width: parent.width
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: appBackend.language === "en" ? "Peak Settings" : "峰值设置"
            color: ApplicationWindow.window.text
            font.pixelSize: Math.round(14 * ApplicationWindow.window.scaleFactor)
            font.bold: true
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 10

            Label { text: appBackend.language === "en" ? "Search Start" : "搜索起点"; color: ApplicationWindow.window.text }
            ThemedTextField { id: startField; Layout.fillWidth: true; text: "0"; inputMethodHints: Qt.ImhDigitsOnly }

            Label { text: appBackend.language === "en" ? "Search End" : "搜索终点"; color: ApplicationWindow.window.text }
            ThemedTextField { id: endField; Layout.fillWidth: true; text: appBackend.language === "en" ? "Full Frame" : "整帧" }

            Label { text: appBackend.language === "en" ? "Method" : "方式"; color: ApplicationWindow.window.text }
            ComboBox {
                id: modeCombo
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "value"
                model: [
                    { label: popup.modeText(0), value: 0 },
                    { label: popup.modeText(1), value: 1 },
                    { label: popup.modeText(2), value: 2 },
                    { label: popup.modeText(3), value: 3 }
                ]
                font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
                contentItem: Text {
                    leftPadding: 12
                    rightPadding: 28
                    text: modeCombo.displayText
                    color: popup.fieldTextColor
                    font: modeCombo.font
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                background: Rectangle {
                    implicitHeight: 34
                    radius: 8
                    color: modeCombo.hovered ? popup.fieldHoverColor : popup.fieldColor
                    border.color: modeCombo.activeFocus ? popup.focusBorderColor : popup.fieldBorderColor
                }
                popup: Popup {
                    y: modeCombo.height + 2
                    width: modeCombo.width
                    implicitHeight: contentItem.implicitHeight
                    padding: 2
                    background: Rectangle {
                        radius: 8
                        color: popup.fieldColor
                        border.color: popup.fieldBorderColor
                    }
                    contentItem: ListView {
                        clip: true
                        implicitHeight: contentHeight
                        model: modeCombo.popup.visible ? modeCombo.delegateModel : null
                        currentIndex: modeCombo.highlightedIndex
                    }
                }
                delegate: ItemDelegate {
                    width: modeCombo.width
                    contentItem: Text {
                        text: modelData && modelData.label !== undefined ? modelData.label : model.label
                        color: popup.fieldTextColor
                        font: modeCombo.font
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        color: highlighted ? popup.fieldHoverColor : "transparent"
                        radius: 6
                    }
                }
            }

            Label { text: appBackend.language === "en" ? "Range Min" : "区间最小值"; color: ApplicationWindow.window.text }
            ThemedTextField { id: minField; Layout.fillWidth: true; text: "0.000000"; inputMethodHints: Qt.ImhFormattedNumbersOnly }

            Label { text: appBackend.language === "en" ? "Range Max" : "区间最大值"; color: ApplicationWindow.window.text }
            ThemedTextField { id: maxField; Layout.fillWidth: true; text: "0.000000"; inputMethodHints: Qt.ImhFormattedNumbersOnly }
        }

        Text {
            id: validationText
            Layout.fillWidth: true
            visible: false
            wrapMode: Text.WordWrap
            color: ApplicationWindow.window.danger
            font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
        }

        Text {
            Layout.fillWidth: true
            text: appBackend.language === "en"
                  ? "Peak search uses sample indexes [start, end). Search End = Full Frame scans to the end of each frame. IQR removes statistical outliers. Keep Range keeps values inside [min, max]. Exclude Range removes values inside [min, max]. Changing the search window clears the live trend; new frames use the updated range."
                  : "峰值搜索使用采样点下标 [起点, 终点)。搜索终点为“整帧”时表示一直搜索到本帧末尾。IQR 会过滤统计异常值。保留区间只保留 [最小值, 最大值] 内的峰值。排除区间会过滤 [最小值, 最大值] 内的峰值。修改搜索窗口后，已有实时趋势会清空，后续新帧按新区间计算。"
            wrapMode: Text.WordWrap
            color: ApplicationWindow.window.text
            font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
            lineHeight: 1.2
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            ToolbarButton {
                text: appBackend.language === "en" ? "Cancel" : "取消"
                onClicked: popup.close()
            }
            ToolbarButton {
                text: appBackend.language === "en" ? "OK" : "确定"
                variant: "primary"
                onClicked: popup.applySettings()
            }
        }
    }

    onOpened: resetFields()

    component ThemedTextField: TextField {
        id: textFieldControl
        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
        color: popup.fieldTextColor
        selectedTextColor: ApplicationWindow.window.primaryForeground
        selectionColor: ApplicationWindow.window.primary
        placeholderTextColor: popup.mutedTextColor
        leftPadding: 12
        rightPadding: 12
        background: Rectangle {
            implicitHeight: 34
            radius: 8
            color: textFieldControl.hovered ? popup.fieldHoverColor : popup.fieldColor
            border.color: textFieldControl.activeFocus ? popup.focusBorderColor : popup.fieldBorderColor
        }
    }
}
