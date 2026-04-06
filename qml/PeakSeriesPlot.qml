import QtQuick

Item {
    id: root

    property var values: []
    property int currentFrame: 0
    property bool scatterMode: true
    property color seriesColor: "#f7630c"
    property color markerColor: "#2563eb"
    property color gridColor: "#dce5f0"
    property color frameColor: "#d1d9e6"
    property color textColor: "#64748b"
    property string emptyText: "No peak overview"

    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()

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
            for (var gy = 0; gy <= 6; ++gy) {
                var y = top + plotHeight * gy / 6.0
                ctx.beginPath()
                ctx.moveTo(left, y)
                ctx.lineTo(right, y)
                ctx.stroke()
            }

            ctx.strokeStyle = root.frameColor
            ctx.strokeRect(left, top, plotWidth, plotHeight)

            if (!root.values || root.values.length === 0) {
                ctx.fillStyle = root.textColor
                ctx.font = "14px sans-serif"
                ctx.textAlign = "center"
                ctx.fillText(root.emptyText, width / 2, height / 2)
                return
            }

            var minValue = Number(root.values[0])
            var maxValue = Number(root.values[0])
            for (var i = 1; i < root.values.length; ++i) {
                var value = Number(root.values[i])
                if (value < minValue) minValue = value
                if (value > maxValue) maxValue = value
            }
            if (Math.abs(maxValue - minValue) < 1e-6) {
                var pad = Math.max(1e-6, Math.abs(maxValue) * 0.05 + 1e-6)
                minValue -= pad
                maxValue += pad
            }

            var points = []
            var step = Math.max(1, Math.ceil(root.values.length / plotWidth))
            for (var index = 0; index < root.values.length; index += step) {
                var currentValue = Number(root.values[index])
                var ratioX = root.values.length <= 1 ? 0.5 : index / (root.values.length - 1)
                var ratioY = (currentValue - minValue) / Math.max(1e-6, maxValue - minValue)
                points.push({
                    x: left + ratioX * plotWidth,
                    y: bottom - ratioY * plotHeight
                })
            }

            ctx.strokeStyle = root.seriesColor
            ctx.fillStyle = root.seriesColor
            if (!root.scatterMode && points.length >= 2) {
                ctx.lineWidth = 1.5
                ctx.beginPath()
                ctx.moveTo(points[0].x, points[0].y)
                for (var p = 1; p < points.length; ++p) {
                    ctx.lineTo(points[p].x, points[p].y)
                }
                ctx.stroke()
            } else {
                for (var dot = 0; dot < points.length; ++dot) {
                    ctx.beginPath()
                    ctx.arc(points[dot].x, points[dot].y, 2.4, 0, Math.PI * 2)
                    ctx.fill()
                }
            }

            if (root.currentFrame > 0 && root.currentFrame <= root.values.length) {
                var selectedIndex = root.currentFrame - 1
                var selectedRatioX = root.values.length <= 1 ? 0.5 : selectedIndex / (root.values.length - 1)
                var selectedValue = Number(root.values[selectedIndex])
                var selectedRatioY = (selectedValue - minValue) / Math.max(1e-6, maxValue - minValue)
                var selectedX = left + selectedRatioX * plotWidth
                var selectedY = bottom - selectedRatioY * plotHeight

                ctx.strokeStyle = root.markerColor
                ctx.setLineDash([4, 4])
                ctx.beginPath()
                ctx.moveTo(selectedX, top)
                ctx.lineTo(selectedX, bottom)
                ctx.stroke()
                ctx.setLineDash([])
                ctx.fillStyle = root.markerColor
                ctx.beginPath()
                ctx.arc(selectedX, selectedY, 4, 0, Math.PI * 2)
                ctx.fill()
            }

            ctx.fillStyle = root.textColor
            ctx.font = "12px sans-serif"
            ctx.textAlign = "right"
            ctx.fillText(maxValue.toFixed(4), left - 8, top + 6)
            ctx.fillText(((maxValue + minValue) * 0.5).toFixed(4), left - 8, top + plotHeight / 2 + 4)
            ctx.fillText(minValue.toFixed(4), left - 8, bottom)
            ctx.fillText(root.values.length + " frames", right, height - 8)
        }
    }

    onValuesChanged: canvas.requestPaint()
    onCurrentFrameChanged: canvas.requestPaint()
    onScatterModeChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()
    onHeightChanged: canvas.requestPaint()
}
