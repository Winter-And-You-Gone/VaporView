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
    property int maxVisualSamples: 220
    property int sourcePointCount: 0
    property int xStartIndex: 0
    property int xEndIndex: -1
    property int xAxisLabelOffset: 1
    property real lineWidth: 1.5
    property real fillTopOpacity: 0.15
    property real fillBottomOpacity: 0.01

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
        return isNaN(value) ? 0 : value
    }

    function buildDisplaySamples(list) {
        var values = []
        var count = sampleCount(list)
        if (count >= 2) {
            var stride = Math.max(1, Math.ceil(count / maxVisualSamples))
            for (var i = 0; i < count; i += stride)
                values.push(sampleValue(list, i))
            if ((count - 1) % stride !== 0)
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

    function px(index) {
        if (pointCount <= 1)
            return marginLeft
        return marginLeft + index * chartWidth / (pointCount - 1)
    }

    function py(value) {
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
            text: chart.formatIndexLabel(chart.effectiveXStart + (chart.effectiveXEnd - chart.effectiveXStart) * index / 5 + chart.xAxisLabelOffset)
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
            ctx.moveTo(chart.px(0), chart.py(chart.drawSamples[0]))
            for (var i = 1; i < chart.pointCount; ++i)
                ctx.lineTo(chart.px(i), chart.py(chart.drawSamples[i]))

            if (chart.fillUnderLine) {
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
            ctx.moveTo(chart.px(0), chart.py(chart.drawSamples[0]))
            for (var j = 1; j < chart.pointCount; ++j)
                ctx.lineTo(chart.px(j), chart.py(chart.drawSamples[j]))
            ctx.strokeStyle = chart.rgbaString(chart.lineColor, chart.lineColor.a)
            ctx.lineWidth = chart.lineWidth
            ctx.lineJoin = chart.hardLineCorners ? "miter" : "round"
            ctx.lineCap = chart.hardLineCorners ? "butt" : "round"
            ctx.stroke()
        }
    }

    Connections {
        target: chart
        function onDrawSamplesChanged() { lineLayer.requestPaint() }
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
    }

    onWidthChanged: lineLayer.requestPaint()
    onHeightChanged: lineLayer.requestPaint()

    Repeater {
        model: chart.scatter ? chart.pointCount : 0
        Rectangle {
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
