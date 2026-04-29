import QtQuick

Canvas {
    id: canvas
    property var samples: []
    property color lineColor: ApplicationWindow.window.waveformRaw
    property bool scatter: false
    property string emptyText: "No data"
    property real yMin: -1.2
    property real yMax: 1.2
    property real xSamplePeriod: 0.05
    property bool showDemoWhenEmpty: true

    Component.onCompleted: requestPaint()
    onSamplesChanged: requestPaint()
    onLineColorChanged: requestPaint()
    onScatterChanged: requestPaint()
    onYMinChanged: requestPaint()
    onYMaxChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onVisibleChanged: if (visible) requestPaint()

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

    onPaint: {
        if (width <= 0 || height <= 0)
            return

        var ctx = getContext("2d")
        if (ctx.reset) {
            ctx.reset()
        } else {
            ctx.setTransform(1, 0, 0, 1, 0, 0)
            ctx.globalAlpha = 1
            ctx.lineWidth = 1
            ctx.setLineDash([])
        }
        ctx.fillStyle = ApplicationWindow.window.secondary
        ctx.fillRect(0, 0, width, height)

        var marginLeft = 32
        var marginTop = 8
        var marginBottom = 18
        var marginRight = 8
        var chartW = Math.max(1, width - marginLeft - marginRight)
        var chartH = Math.max(1, height - marginTop - marginBottom)
        var incomingCount = sampleCount(samples)
        var drawSamples = []
        if (incomingCount >= 2) {
            for (var si = 0; si < incomingCount; ++si) {
                drawSamples.push(sampleValue(samples, si))
            }
        }
        if (drawSamples.length < 2 && showDemoWhenEmpty) {
            drawSamples = []
            var mid = (yMin + yMax) / 2
            var amp = Math.max(0.001, (yMax - yMin) * 0.32)
            for (var di = 0; di < 400; ++di) {
                drawSamples.push(mid + amp * Math.sin(di * 0.075) + amp * 0.24 * Math.sin(di * 0.27))
            }
        }

        ctx.strokeStyle = ApplicationWindow.window.border
        ctx.lineWidth = 0.5
        ctx.setLineDash([2, 2])
        for (var gx = 0; gx <= 10; ++gx) {
            var x = marginLeft + chartW * gx / 10
            ctx.beginPath()
            ctx.moveTo(x, marginTop)
            ctx.lineTo(x, marginTop + chartH)
            ctx.stroke()
        }
        for (var gy = 0; gy <= 4; ++gy) {
            var y = marginTop + chartH * gy / 4
            ctx.beginPath()
            ctx.moveTo(marginLeft, y)
            ctx.lineTo(marginLeft + chartW, y)
            ctx.stroke()
        }
        ctx.setLineDash([])

        ctx.fillStyle = ApplicationWindow.window.text
        ctx.font = "600 " + Math.round(10 * ApplicationWindow.window.scaleFactor) + "px sans-serif"
        ctx.textAlign = "right"
        ctx.textBaseline = "middle"
        for (var yl = 0; yl <= 4; ++yl) {
            var value = yMax - (yMax - yMin) * yl / 4
            ctx.fillText(value.toFixed(1), marginLeft - 6, marginTop + chartH * yl / 4)
        }
        ctx.textAlign = "center"
        ctx.textBaseline = "alphabetic"
        for (var xl = 0; xl <= 5; ++xl) {
            var tx = marginLeft + chartW * xl / 5
            var seconds = ((drawSamples.length > 1 ? drawSamples.length - 1 : 0) * xSamplePeriod * xl / 5)
            ctx.fillText(seconds.toFixed(1) + "s", tx, height - 4)
        }

        if (drawSamples.length < 2) {
            return
        }

        function px(i) { return marginLeft + i * chartW / (drawSamples.length - 1) }
        function py(v) {
            var clamped = Math.max(yMin, Math.min(yMax, v))
            return marginTop + (yMax - clamped) * chartH / Math.max(0.000001, yMax - yMin)
        }

        ctx.strokeStyle = lineColor
        ctx.fillStyle = lineColor
        ctx.lineWidth = 1.5
        ctx.beginPath()
        ctx.moveTo(px(0), py(drawSamples[0]))
        for (var j = 1; j < drawSamples.length; ++j) {
            if (scatter) {
                ctx.moveTo(px(j), py(drawSamples[j]))
                ctx.arc(px(j), py(drawSamples[j]), 2, 0, Math.PI * 2)
            } else {
                ctx.lineTo(px(j), py(drawSamples[j]))
            }
        }
        scatter ? ctx.fill() : ctx.stroke()
    }
}
