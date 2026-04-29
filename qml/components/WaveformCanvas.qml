import QtQuick

Item {
    id: chart

    property var samples: []
    property color lineColor: ApplicationWindow.window.waveformRaw
    property bool scatter: false
    property string emptyText: "No data"
    property real yMin: -1.2
    property real yMax: 1.2
    property real xSamplePeriod: 0.05
    property bool showDemoWhenEmpty: true
    property int maxVisualSamples: 220

    readonly property int marginLeft: 32
    readonly property int marginTop: 8
    readonly property int marginBottom: 18
    readonly property int marginRight: 8
    readonly property real chartWidth: Math.max(1, width - marginLeft - marginRight)
    readonly property real chartHeight: Math.max(1, height - marginTop - marginBottom)
    readonly property var drawSamples: buildDisplaySamples(samples)
    readonly property int pointCount: drawSamples.length

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

    function px(index) {
        if (pointCount <= 1)
            return marginLeft
        return marginLeft + index * chartWidth / (pointCount - 1)
    }

    function py(value) {
        var clamped = Math.max(yMin, Math.min(yMax, Number(value)))
        return marginTop + (yMax - clamped) * chartHeight / Math.max(0.000001, yMax - yMin)
    }

    Rectangle {
        anchors.fill: parent
        color: ApplicationWindow.window.secondary
    }

    Repeater {
        model: 11
        Rectangle {
            x: chart.marginLeft + chart.chartWidth * index / 10
            y: chart.marginTop
            width: 1
            height: chart.chartHeight
            color: ApplicationWindow.window.border
            opacity: 0.55
        }
    }

    Repeater {
        model: 5
        Rectangle {
            x: chart.marginLeft
            y: chart.marginTop + chart.chartHeight * index / 4
            width: chart.chartWidth
            height: 1
            color: ApplicationWindow.window.border
            opacity: 0.55
        }
    }

    Repeater {
        model: 5
        Text {
            x: 0
            y: chart.marginTop + chart.chartHeight * index / 4 - height / 2
            width: chart.marginLeft - 6
            text: (chart.yMax - (chart.yMax - chart.yMin) * index / 4).toFixed(1)
            color: ApplicationWindow.window.text
            opacity: 0.78
            font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
        }
    }

    Repeater {
        model: 6
        Text {
            x: chart.marginLeft + chart.chartWidth * index / 5 - width / 2
            y: chart.height - chart.marginBottom + 2
            width: 48
            text: ((Math.max(0, chart.pointCount - 1) * chart.xSamplePeriod * index / 5)).toFixed(1) + "s"
            color: ApplicationWindow.window.text
            opacity: 0.78
            font.pixelSize: Math.round(10 * ApplicationWindow.window.scaleFactor)
            font.weight: Font.DemiBold
            horizontalAlignment: Text.AlignHCenter
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
        color: ApplicationWindow.window.muted
        font.pixelSize: 11 * ApplicationWindow.window.scaleFactor
    }
}
