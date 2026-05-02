import QtQuick

Item {
    id: chart

    property var samples: []
    property color lineColor: "#496083"
    property color plotBackground: "#f1f5f9"
    property color gridColor: "#cbd5e1"
    property color axisColor: "#64748b"
    property color emptyColor: "#64748b"
    property real uiScale: 1.0
    property bool scatter: false
    property bool fillUnderLine: false
    property bool hardLineCorners: false
    property string emptyText: "No data"
    property real yMin: -1.2
    property real yMax: 1.2
    property bool autoScaleY: true
    property bool showDemoWhenEmpty: true
    property bool tailWindow: false
    property int maxVisualSamples: 220
    property int sourcePointCount: 0
    property int xStartIndex: 0
    property int xEndIndex: -1
    property int xAxisLabelOffset: 1
    property real lineWidth: 1.5
    property real fillTopOpacity: 0.15
    property real fillBottomOpacity: 0.01
    property bool showCursor: false
    property int cursorSourceIndex: -1
    property string cursorXUnit: ""
    property string cursorYUnit: ""
    property color cursorColor: lineColor

    readonly property int marginLeft: 42
    readonly property int marginTop: 8
    readonly property int marginBottom: 22
    readonly property int marginRight: 10
    readonly property real chartWidth: Math.max(1, width - marginLeft - marginRight)
    readonly property real chartHeight: Math.max(1, height - marginTop - marginBottom)
    readonly property var drawSamples: buildDisplaySamples(samples)
    readonly property int pointCount: drawSamples.length
    readonly property var yRange: computeYRange(drawSamples)
    readonly property real effectiveYMin: yRange[0]
    readonly property real effectiveYMax: yRange[1]
    readonly property int effectiveSourceCount: Math.max(sourcePointCount, sampleCount(samples), pointCount)
    readonly property int effectiveXStart: Math.max(0, xStartIndex)
    readonly property int effectiveXEnd: xEndIndex >= effectiveXStart ? xEndIndex
                                                                      : Math.max(effectiveXStart, effectiveXStart + Math.max(0, effectiveSourceCount - 1))
    readonly property bool cursorVisible: showCursor && cursorSourceIndex >= effectiveXStart && cursorSourceIndex <= effectiveXEnd && cursorSampleValue(cursorSourceIndex) === cursorSampleValue(cursorSourceIndex)
    readonly property real cursorRatio: effectiveXEnd <= effectiveXStart ? 0 : (cursorSourceIndex - effectiveXStart) / (effectiveXEnd - effectiveXStart)
    readonly property real cursorX: marginLeft + cursorRatio * chartWidth
    readonly property real cursorY: py(cursorSampleValue(cursorSourceIndex))
    readonly property real cursorValue: cursorSampleValue(cursorSourceIndex)

    clip: true

    function sampleCount(list) {
        if (!list)
            return 0
        if (list.length !== undefined)
            return list.length
        if (list.count !== undefined)
            return list.count
        return 0
    }

    function sampleValue(list, index) {
        var value = Number(list[index])
        return value
    }

    function cursorSampleValue(sourceIndex) {
        var count = sampleCount(samples)
        if (count <= 0)
            return NaN
        var sourceCount = Math.max(1, effectiveXEnd - effectiveXStart + 1)
        var clampedSource = Math.max(effectiveXStart, Math.min(effectiveXEnd, sourceIndex))
        var sampleIndex = Math.round((clampedSource - effectiveXStart) * (count - 1) / Math.max(1, sourceCount - 1))
        return sampleValue(samples, Math.max(0, Math.min(count - 1, sampleIndex)))
    }

    function buildDisplaySamples(list) {
        var values = []
        var count = sampleCount(list)
        if (count >= 2) {
            var start = tailWindow && count > maxVisualSamples ? count - maxVisualSamples : 0
            var displayCount = count - start
            var stride = Math.max(1, Math.ceil(displayCount / maxVisualSamples))
            for (var i = start; i < count; i += stride)
                values.push(sampleValue(list, i))
            if ((count - 1 - start) % stride !== 0)
                values.push(sampleValue(list, count - 1))
        }
        if (values.length < 2 && showDemoWhenEmpty) {
            var mid = (yMin + yMax) / 2
            var amp = Math.max(0.001, (yMax - yMin) * 0.32)
            for (var di = 0; di < maxVisualSamples; ++di)
                values.push(mid + amp * Math.sin(di * 0.075) + amp * 0.24 * Math.sin(di * 0.27))
        }
        return values
    }

    function computeYRange(values) {
        if (!autoScaleY || values.length < 1)
            return [yMin, yMax]

        var minValue = Number.POSITIVE_INFINITY
        var maxValue = Number.NEGATIVE_INFINITY
        for (var i = 0; i < values.length; ++i) {
            var value = Number(values[i])
            if (isNaN(value))
                continue
            minValue = Math.min(minValue, value)
            maxValue = Math.max(maxValue, value)
        }

        if (!isFinite(minValue) || !isFinite(maxValue))
            return [yMin, yMax]

        var span = maxValue - minValue
        var pad = span > 0 ? span * 0.12 : Math.max(0.5, Math.abs(maxValue) * 0.08)
        return [minValue - pad, maxValue + pad]
    }

    function formatAxisValue(value) {
        var absValue = Math.abs(value)
        if (absValue >= 1000)
            return value.toFixed(0)
        if (absValue >= 10)
            return value.toFixed(1)
        return value.toFixed(2)
    }

    function formatIndexLabel(value) {
        if (value >= 10000)
            return Math.round(value / 1000) + "k"
        return Math.round(value).toString()
    }

    function formatCursorX(value) {
        return formatIndexLabel(value + xAxisLabelOffset) + cursorXUnit
    }

    function formatCursorY(value) {
        return formatAxisValue(value) + cursorYUnit
    }

    function xTickLabelValue(tickIndex, tickCount) {
        var displayStart = effectiveXStart + xAxisLabelOffset
        var displayEnd = effectiveXEnd + xAxisLabelOffset
        var lastTick = Math.max(1, tickCount - 1)
        var rawValue = displayStart + (displayEnd - displayStart) * tickIndex / lastTick
        if (tickIndex === 0 || tickIndex === lastTick)
            return Math.round(rawValue)

        var span = Math.abs(displayEnd - displayStart)
        if (span <= 0)
            return Math.round(rawValue)

        var roughStep = span / lastTick
        var roundTo = Math.pow(10, Math.max(0, Math.floor(Math.log(roughStep) / Math.LN10)))
        return Math.round(rawValue / roundTo) * roundTo
    }

    function px(index) {
        if (pointCount <= 1)
            return marginLeft
        return marginLeft + index * chartWidth / (pointCount - 1)
    }

    function py(value) {
        if (isNaN(Number(value)))
            return NaN
        var clamped = Math.max(effectiveYMin, Math.min(effectiveYMax, Number(value)))
        return marginTop + (effectiveYMax - clamped) * chartHeight / Math.max(0.000001, effectiveYMax - effectiveYMin)
    }

    function rgbaString(colorValue, opacity) {
        return "rgba(" + Math.round(colorValue.r * 255) + "," +
               Math.round(colorValue.g * 255) + "," +
               Math.round(colorValue.b * 255) + "," + opacity + ")"
    }

    Rectangle {
        anchors.fill: parent
        color: chart.plotBackground
    }

    Repeater {
        model: 11
        Rectangle {
            x: chart.marginLeft + chart.chartWidth * index / 10
            y: chart.marginTop
            width: 1
            height: chart.chartHeight
            color: chart.gridColor
            opacity: 1.0
        }
    }

    Repeater {
        model: 5
        Rectangle {
            x: chart.marginLeft
            y: chart.marginTop + chart.chartHeight * index / 4
            width: chart.chartWidth
            height: 1
            color: chart.gridColor
            opacity: 1.0
        }
    }

    Rectangle {
        x: chart.marginLeft
        y: chart.marginTop
        width: 1
        height: chart.chartHeight
        color: chart.axisColor
        opacity: 1.0
    }

    Rectangle {
        x: chart.marginLeft
        y: chart.marginTop + chart.chartHeight - 1
        width: chart.chartWidth
        height: 1
        color: chart.axisColor
        opacity: 1.0
    }

    Repeater {
        model: 5
        Text {
            z: 10
            x: 0
            y: chart.marginTop + chart.chartHeight * index / 4 - height / 2
            width: chart.marginLeft - 6
            text: chart.formatAxisValue(chart.effectiveYMax - (chart.effectiveYMax - chart.effectiveYMin) * index / 4)
            color: chart.axisColor
            opacity: 1.0
            font.pixelSize: Math.round(10 * chart.uiScale)
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    Repeater {
        model: 6
        Text {
            readonly property real labelCenter: chart.marginLeft + chart.chartWidth * index / 5
            z: 10
            x: index === 0 ? chart.marginLeft
                            : index === 5 ? chart.marginLeft + chart.chartWidth - width
                                           : labelCenter - width / 2
            y: chart.height - chart.marginBottom + 3
            width: 54
            text: chart.formatIndexLabel(chart.xTickLabelValue(index, 6))
            color: chart.axisColor
            opacity: 1.0
            font.pixelSize: Math.round(10 * chart.uiScale)
            font.weight: Font.DemiBold
            horizontalAlignment: index === 0 ? Text.AlignLeft : index === 5 ? Text.AlignRight : Text.AlignHCenter
        }
    }

    Canvas {
        id: lineLayer
        anchors.fill: parent
        antialiasing: true

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, lineLayer.width, lineLayer.height)
            if (chart.scatter || chart.pointCount < 2)
                return

            ctx.beginPath()
            var pathStarted = false
            for (var i = 0; i < chart.pointCount; ++i) {
                var y = chart.py(chart.drawSamples[i])
                if (isNaN(y)) {
                    pathStarted = false
                    continue
                }
                if (!pathStarted) {
                    ctx.moveTo(chart.px(i), y)
                    pathStarted = true
                } else {
                    ctx.lineTo(chart.px(i), y)
                }
            }

            if (chart.fillUnderLine && pathStarted) {
                var lastX = chart.px(chart.pointCount - 1)
                var baselineY = chart.marginTop + chart.chartHeight
                ctx.lineTo(lastX, baselineY)
                ctx.lineTo(chart.px(0), baselineY)
                ctx.closePath()

                var grad = ctx.createLinearGradient(0, chart.marginTop, 0, baselineY)
                grad.addColorStop(0, chart.rgbaString(chart.lineColor, chart.fillTopOpacity))
                grad.addColorStop(1, chart.rgbaString(chart.lineColor, chart.fillBottomOpacity))
                ctx.fillStyle = grad
                ctx.fill()
            }

            ctx.beginPath()
            pathStarted = false
            for (var j = 0; j < chart.pointCount; ++j) {
                var strokeY = chart.py(chart.drawSamples[j])
                if (isNaN(strokeY)) {
                    pathStarted = false
                    continue
                }
                if (!pathStarted) {
                    ctx.moveTo(chart.px(j), strokeY)
                    pathStarted = true
                } else {
                    ctx.lineTo(chart.px(j), strokeY)
                }
            }
            ctx.strokeStyle = chart.rgbaString(chart.lineColor, chart.lineColor.a)
            ctx.lineWidth = chart.lineWidth
            ctx.lineJoin = chart.hardLineCorners ? "miter" : "round"
            ctx.lineCap = chart.hardLineCorners ? "butt" : "round"
            ctx.stroke()
        }
    }

    Canvas {
        id: cursorLayer
        anchors.fill: parent
        z: 20

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            if (!chart.cursorVisible)
                return
            ctx.save()
            ctx.setLineDash([4, 3])
            ctx.strokeStyle = chart.rgbaString(chart.cursorColor, 0.92)
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.moveTo(chart.cursorX, chart.marginTop)
            ctx.lineTo(chart.cursorX, chart.marginTop + chart.chartHeight)
            ctx.moveTo(chart.marginLeft, chart.cursorY)
            ctx.lineTo(chart.marginLeft + chart.chartWidth, chart.cursorY)
            ctx.stroke()
            ctx.restore()
        }
    }

    Rectangle {
        visible: chart.cursorVisible
        z: 30
        width: Math.max(40, xLabel.implicitWidth + 10)
        height: 18
        radius: 3
        color: chart.cursorColor
        x: Math.max(chart.marginLeft, Math.min(chart.marginLeft + chart.chartWidth - width, chart.cursorX - width / 2))
        y: chart.marginTop + chart.chartHeight + 3
        Text {
            id: xLabel
            anchors.centerIn: parent
            text: chart.formatCursorX(chart.cursorSourceIndex)
            color: "#ffffff"
            font.pixelSize: Math.round(9 * chart.uiScale)
            font.weight: Font.Bold
            font.family: "Consolas"
        }
    }

    Rectangle {
        visible: chart.cursorVisible
        z: 30
        width: Math.max(chart.marginLeft - 5, yLabel.implicitWidth + 8)
        height: 18
        radius: 3
        color: chart.cursorColor
        x: 2
        y: Math.max(chart.marginTop, Math.min(chart.marginTop + chart.chartHeight - height, chart.cursorY - height / 2))
        Text {
            id: yLabel
            anchors.centerIn: parent
            text: chart.formatCursorY(chart.cursorValue)
            color: "#ffffff"
            font.pixelSize: Math.round(9 * chart.uiScale)
            font.weight: Font.Bold
            font.family: "Consolas"
        }
    }

    Connections {
        target: chart
        function onDrawSamplesChanged() { lineLayer.requestPaint(); cursorLayer.requestPaint() }
        function onLineColorChanged() { lineLayer.requestPaint() }
        function onPlotBackgroundChanged() { lineLayer.requestPaint() }
        function onScatterChanged() { lineLayer.requestPaint() }
        function onFillUnderLineChanged() { lineLayer.requestPaint() }
        function onHardLineCornersChanged() { lineLayer.requestPaint() }
        function onYMinChanged() { lineLayer.requestPaint() }
        function onYMaxChanged() { lineLayer.requestPaint() }
        function onAutoScaleYChanged() { lineLayer.requestPaint() }
        function onSourcePointCountChanged() { lineLayer.requestPaint() }
        function onXStartIndexChanged() { lineLayer.requestPaint() }
        function onXEndIndexChanged() { lineLayer.requestPaint() }
        function onLineWidthChanged() { lineLayer.requestPaint() }
        function onFillTopOpacityChanged() { lineLayer.requestPaint() }
        function onFillBottomOpacityChanged() { lineLayer.requestPaint() }
        function onCursorSourceIndexChanged() { cursorLayer.requestPaint() }
        function onShowCursorChanged() { cursorLayer.requestPaint() }
        function onCursorColorChanged() { cursorLayer.requestPaint() }
    }

    onWidthChanged: { lineLayer.requestPaint(); cursorLayer.requestPaint() }
    onHeightChanged: { lineLayer.requestPaint(); cursorLayer.requestPaint() }
    onCursorVisibleChanged: cursorLayer.requestPaint()
    onCursorXChanged: cursorLayer.requestPaint()
    onCursorYChanged: cursorLayer.requestPaint()

    Repeater {
        model: chart.scatter ? chart.pointCount : 0
        Rectangle {
            visible: !isNaN(Number(chart.drawSamples[index]))
            width: 5
            height: 5
            radius: 2.5
            x: chart.px(index) - width / 2
            y: chart.py(chart.drawSamples[index]) - height / 2
            color: chart.lineColor
            antialiasing: true
        }
    }

    Text {
        visible: chart.pointCount < 2 && !chart.showDemoWhenEmpty
        anchors.centerIn: parent
        text: chart.emptyText
        color: chart.emptyColor
        font.pixelSize: 11 * chart.uiScale
    }
}
