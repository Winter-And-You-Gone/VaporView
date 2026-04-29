import QtQuick

Canvas {
    id: canvas
    property var samples: []
    property color lineColor: ApplicationWindow.window.waveformRaw
    property bool scatter: false
    property string emptyText: "No data"

    onSamplesChanged: requestPaint()
    onLineColorChanged: requestPaint()
    onScatterChanged: requestPaint()
    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.fillStyle = ApplicationWindow.window.secondary
        ctx.fillRect(0, 0, width, height)
        ctx.strokeStyle = ApplicationWindow.window.border
        ctx.lineWidth = 1
        ctx.strokeRect(0.5, 0.5, width - 1, height - 1)

        if (!samples || samples.length < 2) {
            ctx.fillStyle = ApplicationWindow.window.muted
            ctx.font = Math.round(11 * ApplicationWindow.window.scaleFactor) + "px sans-serif"
            ctx.textAlign = "center"
            ctx.fillText(emptyText, width / 2, height / 2)
            return
        }

        var minV = samples[0]
        var maxV = samples[0]
        for (var i = 1; i < samples.length; ++i) {
            minV = Math.min(minV, samples[i])
            maxV = Math.max(maxV, samples[i])
        }
        if (Math.abs(maxV - minV) < 0.000001) {
            maxV += 1
            minV -= 1
        }
        function px(i) { return 6 + i * (width - 12) / (samples.length - 1) }
        function py(v) { return 6 + (maxV - v) * (height - 12) / (maxV - minV) }

        ctx.strokeStyle = lineColor
        ctx.fillStyle = lineColor
        ctx.lineWidth = 1.5
        ctx.beginPath()
        ctx.moveTo(px(0), py(samples[0]))
        for (var j = 1; j < samples.length; ++j) {
            if (scatter) {
                ctx.moveTo(px(j), py(samples[j]))
                ctx.arc(px(j), py(samples[j]), 2, 0, Math.PI * 2)
            } else {
                ctx.lineTo(px(j), py(samples[j]))
            }
        }
        scatter ? ctx.fill() : ctx.stroke()
    }
}
