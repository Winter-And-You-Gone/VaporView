import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import "../components"

Item {
    id: page

    property int selectedIndex: 0
    property int sessionPage: 0
    readonly property int sessionsPerPage: 3
    readonly property int sessionPageCount: Math.max(1, Math.ceil(sessionBackend.sessions.length / sessionsPerPage))
    property int previewFrame: Math.min(maxFrame, Math.max(0, Math.floor(maxFrame * 0.49)))
    readonly property int maxFrame: Math.max(0, Number(sessionBackend.selectedSession.waveformFrames || sessionBackend.selectedSession.frames || sessionBackend.peakTrendPreview.length || 0))
    property bool trendScatter: false
    property bool filterVisible: false
    property int csvGen: sessionBackend.csvPreviewGeneration
    onCsvGenChanged: scrollCsvToHighlighted()

    readonly property color trendRed: "#ef4444"
    readonly property color trendBlue: ApplicationWindow.window.dark ? "#60a5fa" : "#3b82f6"
    readonly property color trendGreen: "#10b981"

    function sessionSizeText(sizeText) {
        var text = String(sizeText || "0 B")
        return text.replace(" MB", " M")
    }

    function selectedPath() {
        return String(sessionBackend.selectedSession.path || sessionBackend.recordingDirectory || "")
    }

    function openRawParserForSession() {
        if (selectedPath().length <= 0)
            return
        rawParserBackend.openSessionPath(selectedPath())
        ApplicationWindow.window.currentPage = "rawParser"
    }

    function metricValue(key, fallbackText) {
        var value = sessionBackend.selectedSession[key]
        if (value === undefined || value === null || String(value).length === 0)
            return fallbackText || "---"
        return String(value)
    }

    function pagedSession(row) {
        var actualIndex = sessionPage * sessionsPerPage + row
        if (actualIndex < 0 || actualIndex >= sessionBackend.sessions.length)
            return ({})
        return sessionBackend.sessions[actualIndex]
    }

    function selectVisibleSession(row) {
        var actualIndex = sessionPage * sessionsPerPage + row
        if (actualIndex < 0 || actualIndex >= sessionBackend.sessions.length)
            return
        selectedIndex = actualIndex
        sessionBackend.selectSession(actualIndex)
        previewFrame = Math.min(maxFrame, Math.max(0, Math.floor(maxFrame * 0.49)))
    }

    function scrollCsvToHighlighted() {
        Qt.callLater(function() {
            var rows = sessionBackend.csvPreviewRows
            var scrollIdx = -1
            for (var i = 0; i < rows.length; i++) {
                if (rows[i]) {
                    var rank = rows[i]._matchRank
                    if (rank === 0 || rank === 1) {
                        if (scrollIdx < 0 || i < scrollIdx)
                            scrollIdx = i
                    }
                }
            }
            if (scrollIdx >= 0)
                tableFlick.contentY = Math.max(0, 34 + scrollIdx * 30)
        })
    }

    FolderDialog {
        id: folderDialog
        title: ApplicationWindow.window.t("settings.recordDir")
        onAccepted: sessionBackend.setRecordingDirectory(selectedFolder.toString().replace("file:///", ""))
    }

    onSessionPageCountChanged: sessionPage = Math.min(sessionPage, sessionPageCount - 1)

    Connections {
        target: sessionBackend
        function onFrameSelectionChanged() {
            page.previewFrame = Math.max(0, sessionBackend.currentFrameIndex)
        }
        function onSelectedSessionChanged() {
            if (sessionBackend.currentFrameIndex >= 0)
                page.previewFrame = sessionBackend.currentFrameIndex
        }
    }

    component HeaderIconButton: Button {
        id: iconButton
        property string iconName: "refresh-cw"

        implicitWidth: 22
        implicitHeight: 22
        padding: 0
        background: Rectangle {
            radius: 6
            color: iconButton.hovered ? ApplicationWindow.window.secondary : "transparent"
            border.width: 0
        }
        contentItem: LucideIcon {
            anchors.centerIn: parent
            width: 12
            height: 12
            name: iconButton.iconName
            iconColor: iconButton.enabled ? ApplicationWindow.window.muted : Qt.rgba(ApplicationWindow.window.muted.r, ApplicationWindow.window.muted.g, ApplicationWindow.window.muted.b, 0.45)
        }
    }

    component InfoMetric: Item {
        property string label: ""
        property string value: "---"

        implicitHeight: 38
        Column {
            anchors.fill: parent
            spacing: 2
            Text {
                width: parent.width
                text: label
                color: ApplicationWindow.window.muted
                font.pixelSize: Math.round(9 * ApplicationWindow.window.scaleFactor)
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: value
                color: ApplicationWindow.window.text
                font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                font.weight: Font.DemiBold
                font.family: "Consolas"
                elide: Text.ElideRight
            }
        }
    }

    component TrendRow: RowLayout {
        property string label: ""
        property string unit: ""
        property var samples: []
        property color lineColor: ApplicationWindow.window.waveformRaw
        property bool fill: false
        property bool scatter: false
        property int sourcePointCount: samples && samples.length !== undefined ? samples.length : 0
        property int xStartIndex: 0
        property int xEndIndex: Math.max(0, sourcePointCount - 1)
        property bool showCursor: false
        property int cursorIndex: -1
        property int preferredHeight: 126
        property string emptyText: "暂无数据"

        spacing: 8
        Layout.fillWidth: true
        Layout.preferredHeight: preferredHeight

        Item {
            Layout.preferredWidth: 34
            Layout.fillHeight: true
            Column {
                anchors.centerIn: parent
                spacing: 1
                Repeater {
                    model: label.split("")
                    delegate: Text {
                        text: modelData
                        color: ApplicationWindow.window.muted
                        font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                        font.weight: Font.Bold
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
                Text {
                    visible: unit.length > 0
                    text: unit
                    color: ApplicationWindow.window.muted
                    font.pixelSize: Math.round(9 * ApplicationWindow.window.scaleFactor)
                    font.weight: Font.Bold
                    font.family: "Consolas"
                    horizontalAlignment: Text.AlignHCenter
                    width: parent.width
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 6
            color: Qt.rgba(ApplicationWindow.window.cardAlt.r, ApplicationWindow.window.cardAlt.g, ApplicationWindow.window.cardAlt.b, ApplicationWindow.window.dark ? 0.34 : 0.20)
            border.color: Qt.rgba(ApplicationWindow.window.border.r, ApplicationWindow.window.border.g, ApplicationWindow.window.border.b, 0.65)
            border.width: 1

            WaveformCanvas {
                anchors.fill: parent
                anchors.margins: 8
                samples: parent.parent.samples
                sourcePointCount: Math.max(2, parent.parent.sourcePointCount)
                xStartIndex: parent.parent.xStartIndex
                xEndIndex: Math.max(parent.parent.xStartIndex + 1, parent.parent.xEndIndex)
                autoScaleY: parent.parent.samples.length > 1
                showDemoWhenEmpty: false
                fillUnderLine: parent.parent.fill
                scatter: parent.parent.scatter
                hardLineCorners: true
                lineColor: parent.parent.lineColor
                lineWidth: 1.7
                maxVisualSamples: Math.max(240, Math.floor(width))
                showCursor: parent.parent.showCursor
                cursorSourceIndex: parent.parent.cursorIndex
                cursorYUnit: parent.parent.unit.length > 0 ? " " + parent.parent.unit : ""
                cursorColor: parent.parent.lineColor
                plotBackground: ApplicationWindow.window.chartPlot
                gridColor: ApplicationWindow.window.chartGrid
                axisColor: ApplicationWindow.window.chartAxis
                emptyColor: ApplicationWindow.window.muted
                emptyText: parent.parent.emptyText
                uiScale: ApplicationWindow.window.scaleFactor
            }
        }
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        contentWidth: Math.max(1240, width - 2)

        ColumnLayout {
            width: scroll.contentWidth
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 160
                spacing: 8

                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    title: "记录列表"
                    headerRight: Row {
                        spacing: 2
                        HeaderIconButton {
                            property int sortMode: 0
                            iconName: "arrow-down-up"
                            ToolTip.visible: hovered
                            ToolTip.text: ["按日期降序", "按日期升序", "按大小降序", "按名称排序"][sortMode]
                            ToolTip.delay: 400
                            onClicked: {
                                sortMode = (sortMode + 1) % 4
                                sessionBackend.sortSessions(sortMode)
                            }
                        }
                        HeaderIconButton {
                            iconName: "filter"
                            ToolTip.visible: hovered
                            ToolTip.text: "筛选"
                            ToolTip.delay: 400
                            onClicked: {
                                filterVisible = !filterVisible
                                if (!filterVisible) {
                                    sessionBackend.clearSessionFilter()
                                    filterInput.text = ""
                                }
                            }
                        }
                        HeaderIconButton {
                            iconName: "refresh-cw"
                            onClicked: sessionBackend.refreshSessions()
                            ToolTip.visible: hovered
                            ToolTip.text: "刷新"
                            ToolTip.delay: 400
                        }
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            id: filterBar
                            Layout.fillWidth: true
                            Layout.preferredHeight: filterVisible ? 32 : 0
                            visible: filterVisible
                            color: "transparent"
                            clip: true

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 4
                                TextInput {
                                    id: filterInput
                                    Layout.fillWidth: true
                                    color: ApplicationWindow.window.text
                                    font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                                    verticalAlignment: Text.AlignVCenter
                                    onTextChanged: {
                                        filterTimer.restart()
                                    }

                                    property bool placeHolder: true
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "搜索记录名称..."
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: parent.font.pixelSize
                                        visible: parent.text.length === 0
                                    }

                                    Timer {
                                        id: filterTimer
                                        interval: 300
                                        repeat: false
                                        onTriggered: {
                                            if (filterInput.text.length > 0) {
                                                sessionBackend.setSessionFilter(filterInput.text)
                                            } else {
                                                sessionBackend.clearSessionFilter()
                                            }
                                        }
                                    }
                                }
                                HeaderIconButton {
                                    iconName: "square"
                                    implicitWidth: 16
                                    implicitHeight: 16
                                    onClicked: {
                                        filterVisible = false
                                        sessionBackend.clearSessionFilter()
                                        filterInput.text = ""
                                    }
                                }
                            }
                        }

                        Column {
                            id: sessionList
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Repeater {
                                model: Math.min(page.sessionsPerPage, Math.max(0, sessionBackend.sessions.length - page.sessionPage * page.sessionsPerPage))
                                delegate: Rectangle {
                                property int actualIndex: page.sessionPage * page.sessionsPerPage + index
                                property var sessionData: page.pagedSession(index)

                                width: sessionList.width
                                height: 33
                                color: actualIndex === page.selectedIndex ? Qt.rgba(ApplicationWindow.window.primary.r, ApplicationWindow.window.primary.g, ApplicationWindow.window.primary.b, ApplicationWindow.window.dark ? 0.20 : 0.06)
                                                                         : "transparent"
                                border.width: 0

                                Rectangle {
                                    visible: actualIndex === page.selectedIndex
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 3
                                    color: ApplicationWindow.window.primary
                                }
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 1
                                    color: ApplicationWindow.window.border
                                    opacity: 0.55
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: page.selectVisibleSession(index)
                                }
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: true
                                        text: sessionData.name || "---"
                                        color: ApplicationWindow.window.text
                                        font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        text: page.sessionSizeText(sessionData.size)
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: Math.round(9 * ApplicationWindow.window.scaleFactor)
                                        font.family: "Consolas"
                                    }
                                }
                            }
                        }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 32
                            color: Qt.rgba(ApplicationWindow.window.secondary.r, ApplicationWindow.window.secondary.g, ApplicationWindow.window.secondary.b, ApplicationWindow.window.dark ? 0.38 : 0.26)
                            border.color: ApplicationWindow.window.border
                            border.width: 1
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                Text {
                                    Layout.fillWidth: true
                                    text: "共 " + sessionBackend.sessions.length + " 条"
                                    color: ApplicationWindow.window.muted
                                    font.pixelSize: Math.round(9 * ApplicationWindow.window.scaleFactor)
                                    font.weight: Font.Medium
                                }
                                HeaderIconButton {
                                    iconName: "chevron-down"
                                    enabled: page.sessionPage > 0
                                    rotation: 90
                                    onClicked: page.sessionPage = Math.max(0, page.sessionPage - 1)
                                }
                                Text {
                                    text: sessionBackend.sessions.length > 0 ? ((page.sessionPage + 1) + " / " + page.sessionPageCount) : "0 / 0"
                                    color: ApplicationWindow.window.text
                                    font.pixelSize: Math.round(9 * ApplicationWindow.window.scaleFactor)
                                    font.weight: Font.Bold
                                }
                                HeaderIconButton {
                                    iconName: "chevron-down"
                                    enabled: page.sessionPage < page.sessionPageCount - 1
                                    rotation: -90
                                    onClicked: page.sessionPage = Math.min(page.sessionPageCount - 1, page.sessionPage + 1)
                                }
                            }
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    title: "记录路径与操作"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 42
                            spacing: 6
                            TextArea {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                readOnly: true
                                wrapMode: TextEdit.NoWrap
                                text: page.selectedPath()
                                color: ApplicationWindow.window.muted
                                font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                                font.family: "Consolas"
                                background: Rectangle {
                                    radius: 6
                                    color: Qt.rgba(ApplicationWindow.window.secondary.r, ApplicationWindow.window.secondary.g, ApplicationWindow.window.secondary.b, ApplicationWindow.window.dark ? 0.26 : 0.20)
                                    border.color: ApplicationWindow.window.border
                                    border.width: 1
                                }
                            }
                            ToolbarButton {
                                Layout.preferredWidth: 34
                                Layout.fillHeight: true
                                iconName: "folder-open"
                                text: ""
                                onClicked: folderDialog.open()
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 3
                            columnSpacing: 6
                            rowSpacing: 6

                            ToolbarButton {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 28
                                iconName: "refresh-cw"
                                text: sessionBackend.loading ? "加载中" : "重新加载"
                                variant: "primary"
                                enabled: !sessionBackend.loading && page.selectedPath().length > 0
                                onClicked: {
                                    sessionBackend.reloadSelectedSession()
                                }
                            }
                            ToolbarButton { Layout.fillWidth: true; Layout.preferredHeight: 28; iconName: "activity"; text: "轨迹查看"; enabled: false }
                            ToolbarButton { Layout.fillWidth: true; Layout.preferredHeight: 28; iconName: "file-code"; text: "原始解析"; onClicked: page.openRawParserForSession() }
                            ToolbarButton { Layout.fillWidth: true; Layout.preferredHeight: 28; iconName: "download"; text: "导出数据"; enabled: false }
                            ToolbarButton { Layout.fillWidth: true; Layout.preferredHeight: 28; iconName: "trash-2"; text: "清空页面"; variant: "danger"; onClicked: sessionBackend.clear() }
                        }
                    }
                }

                Card {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    title: "记录数据概览"
                    headerRight: Text {
                        width: 230
                        text: page.metricValue("name", "---")
                        color: ApplicationWindow.window.primary
                        font.pixelSize: Math.round(9 * ApplicationWindow.window.scaleFactor)
                        font.weight: Font.Bold
                        font.family: "Consolas"
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideLeft
                    }

                    GridLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        columns: 2
                        columnSpacing: 16
                        rowSpacing: 6

                        InfoMetric { Layout.fillWidth: true; label: "开始时间"; value: page.metricValue("startTime", page.metricValue("date", "---")) }
                        InfoMetric { Layout.fillWidth: true; label: "结束时间"; value: page.metricValue("endTime", "---") }
                        InfoMetric { Layout.fillWidth: true; label: "时长"; value: page.metricValue("duration", "---") }
                        InfoMetric { Layout.fillWidth: true; label: "大小"; value: page.metricValue("size", "0 B") }
                        InfoMetric { Layout.fillWidth: true; label: "波形帧数 | 设备帧数"; value: page.metricValue("framesText", "0 | 0") }
                        InfoMetric { Layout.fillWidth: true; label: "波形/设备频率"; value: page.metricValue("ratesText", "--- | ---") }
                        InfoMetric { Layout.fillWidth: true; label: "设备行数"; value: Number(sessionBackend.selectedSession.sensorRows || 0).toLocaleString(Qt.locale(), "f", 0) }
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 880
                title: "波形与环境趋势"
                headerRight: Row {
                    spacing: 6
                    ToolbarButton {
                        width: 92
                        height: 26
                        iconName: "activity"
                        text: page.trendScatter ? "散点" : "折线"
                        onClicked: page.trendScatter = !page.trendScatter
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 46
                        radius: 6
                        color: Qt.rgba(ApplicationWindow.window.secondary.r, ApplicationWindow.window.secondary.g, ApplicationWindow.window.secondary.b, ApplicationWindow.window.dark ? 0.38 : 0.26)
                        border.color: Qt.rgba(ApplicationWindow.window.border.r, ApplicationWindow.window.border.g, ApplicationWindow.window.border.b, 0.55)
                        border.width: 1
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "0"; color: ApplicationWindow.window.muted; font.pixelSize: 9; font.family: "Consolas" }
                                Text {
                                    Layout.fillWidth: true
                                    text: "当前帧: " + page.previewFrame + " / " + page.maxFrame
                                    color: ApplicationWindow.window.text
                                    font.pixelSize: 9
                                    font.weight: Font.Bold
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                Text { text: String(page.maxFrame); color: ApplicationWindow.window.muted; font.pixelSize: 9; font.family: "Consolas" }
                            }
                            Slider {
                                Layout.fillWidth: true
                                from: 0
                                to: Math.max(1, page.maxFrame - 1)
                                value: page.previewFrame
                                enabled: sessionBackend.waveformIndexReady && page.maxFrame > 0 && !sessionBackend.loading
                                live: true
                                onValueChanged: {
                                    if (pressed && enabled) {
                                        page.previewFrame = Math.round(value)
                                        sessionBackend.setFrameCursor(page.previewFrame)
                                    }
                                }
                                onPressedChanged: {
                                    if (!pressed && enabled) {
                                        page.previewFrame = Math.round(value)
                                        sessionBackend.loadSessionFrame(page.previewFrame)
                                    }
                                }
                            }
                        }
                    }

                    TrendRow {
                        label: "原始波形"
                        samples: sessionBackend.waveformRawPreview
                        sourcePointCount: Math.max(sessionBackend.waveformRawPointCount, sessionBackend.waveformRawPreview.length)
                        xEndIndex: Math.max(1, sourcePointCount - 1)
                        lineColor: ApplicationWindow.window.waveformRaw
                        emptyText: "暂无原始波形"
                    }
                    TrendRow {
                        label: "二次谐波"
                        samples: sessionBackend.waveformHarmonicPreview
                        sourcePointCount: Math.max(sessionBackend.waveformHarmonicPointCount, sessionBackend.waveformHarmonicPreview.length)
                        xEndIndex: Math.max(1, sourcePointCount - 1)
                        lineColor: ApplicationWindow.window.waveformRaw
                        emptyText: "暂无二次谐波"
                    }
                    TrendRow {
                        label: "峰值趋势"
                        samples: sessionBackend.peakTrendPreview
                        sourcePointCount: Math.max(page.maxFrame, sessionBackend.peakTrendPreview.length)
                        xEndIndex: Math.max(1, sourcePointCount - 1)
                        cursorIndex: sessionBackend.currentFrameIndex
                        showCursor: sessionBackend.currentFrameIndex >= 0
                        scatter: page.trendScatter
                        lineColor: ApplicationWindow.window.text
                        fill: !page.trendScatter
                        emptyText: "暂无峰值趋势"
                    }
                    TrendRow {
                        label: "温度"; unit: "°C"
                        samples: sessionBackend.temperaturePreview
                        sourcePointCount: sessionBackend.temperaturePreview.length
                        xEndIndex: Math.max(1, sourcePointCount - 1)
                        cursorIndex: sessionBackend.currentCsvRow
                        showCursor: sessionBackend.currentCsvRow >= 0
                        scatter: page.trendScatter
                        lineColor: page.trendRed
                        emptyText: "暂无温度趋势"
                    }
                    TrendRow {
                        label: "湿度"; unit: "%"
                        samples: sessionBackend.humidityPreview
                        sourcePointCount: sessionBackend.humidityPreview.length
                        xEndIndex: Math.max(1, sourcePointCount - 1)
                        cursorIndex: sessionBackend.currentCsvRow
                        showCursor: sessionBackend.currentCsvRow >= 0
                        scatter: page.trendScatter
                        lineColor: page.trendBlue
                        emptyText: "暂无湿度趋势"
                    }
                    TrendRow {
                        label: "气压"; unit: "hPa"
                        samples: sessionBackend.pressurePreview
                        sourcePointCount: sessionBackend.pressurePreview.length
                        xEndIndex: Math.max(1, sourcePointCount - 1)
                        cursorIndex: sessionBackend.currentCsvRow
                        showCursor: sessionBackend.currentCsvRow >= 0
                        scatter: page.trendScatter
                        lineColor: page.trendGreen
                        emptyText: "暂无气压趋势"
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                title: "格式化设备详细数据"

                Flickable {
                    id: tableFlick
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    contentWidth: Math.max(width, sessionBackend.csvPreviewColumns.length * 170)
                    contentHeight: tableColumn.implicitHeight

                    Column {
                        id: tableColumn
                        width: tableFlick.contentWidth

                        Rectangle {
                            width: tableColumn.width
                            height: 34
                            color: ApplicationWindow.window.cardHeader
                            Row {
                                anchors.fill: parent
                                Repeater {
                                    model: sessionBackend.csvPreviewColumns
                                    delegate: Text {
                                        width: modelData === "#" ? 54 : modelData === "时间偏差" ? 100 : 170
                                        height: parent.height
                                        leftPadding: 12
                                        rightPadding: 8
                                        text: String(modelData)
                                        color: ApplicationWindow.window.muted
                                        font.pixelSize: Math.round(9 * ApplicationWindow.window.scaleFactor)
                                        font.weight: Font.Bold
                                        font.family: "Consolas"
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }

                        Repeater {
                            model: sessionBackend.csvPreviewRows
                            delegate: Rectangle {
                                property var rowData: modelData
                                property int matchRank: Number(rowData._matchRank === undefined ? -1 : rowData._matchRank)

                                width: tableColumn.width
                                height: 30
                                color: matchRank === 0 ? Qt.rgba(ApplicationWindow.window.ok.r, ApplicationWindow.window.ok.g, ApplicationWindow.window.ok.b, ApplicationWindow.window.dark ? 0.34 : 0.20)
                                      : matchRank === 1 ? Qt.rgba(ApplicationWindow.window.ok.r, ApplicationWindow.window.ok.g, ApplicationWindow.window.ok.b, ApplicationWindow.window.dark ? 0.20 : 0.11)
                                      : (index % 2 === 0 ? ApplicationWindow.window.card : Qt.rgba(ApplicationWindow.window.secondary.r, ApplicationWindow.window.secondary.g, ApplicationWindow.window.secondary.b, ApplicationWindow.window.dark ? 0.20 : 0.14))

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    height: 1
                                    color: ApplicationWindow.window.border
                                    opacity: 0.5
                                }

                                Row {
                                    anchors.fill: parent
                                    Repeater {
                                        model: sessionBackend.csvPreviewColumns
                                        delegate: Text {
                                            width: modelData === "#" ? 54 : modelData === "时间偏差" ? 100 : 170
                                            height: parent.height
                                            leftPadding: 12
                                            rightPadding: 8
                                            text: rowData[modelData] === undefined ? "" : String(rowData[modelData])
                                            color: modelData === "gnss_fix" ? ApplicationWindow.window.ok
                                                   : matchRank >= 0 ? ApplicationWindow.window.text
                                                   : ApplicationWindow.window.text
                                            font.pixelSize: Math.round(9 * ApplicationWindow.window.scaleFactor)
                                            font.weight: matchRank === 0 || modelData === "gnss_fix" ? Font.Bold : Font.Normal
                                            font.family: "Consolas"
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }

                        Text {
                            visible: sessionBackend.csvPreviewRows.length === 0
                            width: tableColumn.width
                            height: 80
                            text: "暂无 CSV 预览数据"
                            color: ApplicationWindow.window.muted
                            font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    ScrollBar.vertical: ScrollBar {}
                    ScrollBar.horizontal: ScrollBar {}
                }
            }
        }
    }

    Rectangle {
        visible: sessionBackend.loading
        z: 100
        anchors.fill: parent
        color: Qt.rgba(ApplicationWindow.window.bg.r, ApplicationWindow.window.bg.g, ApplicationWindow.window.bg.b, ApplicationWindow.window.dark ? 0.58 : 0.42)

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(460, parent.width - 48)
            height: 132
            radius: 8
            color: ApplicationWindow.window.card
            border.color: ApplicationWindow.window.border
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 18
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    BusyIndicator {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        running: sessionBackend.loading
                    }
                    Text {
                        Layout.fillWidth: true
                        text: sessionBackend.loadingText || "正在加载记录数据..."
                        color: ApplicationWindow.window.text
                        font.pixelSize: Math.round(12 * ApplicationWindow.window.scaleFactor)
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        text: sessionBackend.loadingProgress + "%"
                        color: ApplicationWindow.window.muted
                        font.pixelSize: Math.round(11 * ApplicationWindow.window.scaleFactor)
                        font.family: "Consolas"
                    }
                }

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: sessionBackend.loadingProgress
                }

                Text {
                    Layout.fillWidth: true
                    text: "大记录会在后台建立索引，界面可保持响应。"
                    color: ApplicationWindow.window.muted
                    font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
