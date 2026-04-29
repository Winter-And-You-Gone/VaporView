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
    property string emptyText: "No data"
    property real yMin: -1.2
    property real yMax: 1.2
    property bool autoScaleY: true
    property bool showDemoWhenEmpty: true
    property int maxVisualSamples: 220
    property int sourcePointCount: 0
    property int xStartIndex: 0
    property int xEndIndex: -1

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
            text: chart.formatIndexLabel(chart.effectiveXStart + (chart.effectiveXEnd - chart.effectiveXStart) * index / 5)
            color: chart.axisColor
            opacity: 1.0
            font.pixelSize: Math.round(10 * chart.uiScale)
            font.weight: Font.DemiBold
            horizontalAlignment: index === 0 ? Text.AlignLeft : index === 5 ? Text.AlignRight : Text.AlignHCenter
        }
    }

    Repeater {
        model: chart.scatter ? 0 : Math.max(0, chart.pointCount - 1)
        Rectangle {
            readonly property real x1: chart.px(index)
            readonly property real y1: chart.py(chart.drawSamples[index])
            readonly property real x2: chart.px(index + 1)
            readonly property real y2: chart.py(chart.drawSamples[index + 1])
            readonly property real dx: x2 - x1
            readonly property real dy: y2 - y1

            x: x1
            y: y1 - height / 2
            width: Math.max(1, Math.sqrt(dx * dx + dy * dy))
            height: 2
            radius: 1
            color: chart.lineColor
            antialiasing: true
            transformOrigin: Item.Left
            rotation: Math.atan2(dy, dx) * 180 / Math.PI
        }
    }

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
