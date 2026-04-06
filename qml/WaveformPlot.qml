import QtQuick

Item {
    id: root

    property var samples: []
    property color strokeColor: "#2563eb"
    property color gridColor: "#dce5f0"
    property color frameColor: "#d1d9e6"
    property color textColor: "#64748b"
    property string emptyText: "No waveform frame"

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()

            ctx.fillStyle = "transparent"
            ctx.fillRect(0, 0, width, height)

            var left = 48
            var top = 14
            var right = width - 12
            var bottom = height - 30
            var plotWidth = Math.max(1, right - left)
            var plotHeight = Math.max(1, bottom - top)

            ctx.strokeStyle = root.gridColor
            ctx.lineWidth = 1
            for (var gx = 0; gx <= 10; ++gx) {
                var x = left + plotWidth * gx / 10.0
                ctx.beginPath()
                ctx.moveTo(x, top)
                ctx.lineTo(x, bottom)
                ctx.stroke()
            }
            for (var gy = 0; gy <= 8; ++gy) {
                var y = top + plotHeight * gy / 8.0
                ctx.beginPath()
                ctx.moveTo(left, y)
                ctx.lineTo(right, y)
                ctx.stroke()
            }

            ctx.strokeStyle = root.frameColor
            ctx.strokeRect(left, top, plotWidth, plotHeight)

            if (!root.samples || root.samples.length === 0) {
                ctx.fillStyle = root.textColor
                ctx.font = "14px sans-serif"
                ctx.textAlign = "center"
                ctx.fillText(root.emptyText, width / 2, height / 2)
                return
            }

            var minValue = Number(root.samples[0])
            var maxValue = Number(root.samples[0])
            for (var i = 1; i < root.samples.length; ++i) {
                var value = Number(root.samples[i])
                if (value < minValue) minValue = value
                if (value > maxValue) maxValue = value
            }
            if (Math.abs(maxValue - minValue) < 1e-6) {
                var pad = Math.max(1e-6, Math.abs(maxValue) * 0.05 + 1e-6)
                minValue -= pad
                maxValue += pad
            }

            var step = Math.max(1, Math.ceil(root.samples.length / plotWidth))
            ctx.beginPath()
            ctx.strokeStyle = root.strokeColor
            ctx.lineWidth = 1.6
            var first = true
            for (var sampleIndex = 0, drawIndex = 0; sampleIndex < root.samples.length; sampleIndex += step, ++drawIndex) {
                var sampleValue = Number(root.samples[sampleIndex])
                var ratioX = root.samples.length <= 1 ? 0 : sampleIndex / (root.samples.length - 1)
                var ratioY = (sampleValue - minValue) / Math.max(1e-6, maxValue - minValue)
                var px = left + ratioX * plotWidth
                var py = bottom - ratioY * plotHeight
                if (first) {
                    ctx.moveTo(px, py)
                    first = false
                } else {
                    ctx.lineTo(px, py)
                }
            }
            ctx.stroke()

            ctx.fillStyle = root.textColor
            ctx.font = "12px sans-serif"
            ctx.textAlign = "right"
            ctx.fillText(maxValue.toFixed(4), left - 8, top + 6)
            ctx.fillText(((maxValue + minValue) * 0.5).toFixed(4), left - 8, top + plotHeight / 2 + 4)
            ctx.fillText(minValue.toFixed(4), left - 8, bottom)
            ctx.fillText(root.samples.length + " samples", right, height - 8)
        }
    }

    onSamplesChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
}
