import QtQuick

Canvas {
    id: icon
    property string name: ""
    property string library: ApplicationWindow.window ? ApplicationWindow.window.iconLibrary : "lucide"
    property color iconColor: ApplicationWindow.window ? ApplicationWindow.window.text : "#020817"
    property real stroke: 1.8

    implicitWidth: 18
    implicitHeight: 18

    onNameChanged: requestPaint()
    onLibraryChanged: requestPaint()
    onIconColorChanged: requestPaint()
    onStrokeChanged: requestPaint()
    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, width, height)
        var s = Math.min(width, height) / 24
        var ox = (width - 24 * s) / 2
        var oy = (height - 24 * s) / 2

        function x(v) { return ox + v * s }
        function y(v) { return oy + v * s }
        function line(x1, y1, x2, y2) {
            ctx.beginPath()
            ctx.moveTo(x(x1), y(y1))
            ctx.lineTo(x(x2), y(y2))
            ctx.stroke()
        }
        function rect(rx, ry, rw, rh) {
            ctx.strokeRect(x(rx), y(ry), rw * s, rh * s)
        }
        function circle(cx, cy, r) {
            ctx.beginPath()
            ctx.arc(x(cx), y(cy), r * s, 0, Math.PI * 2)
            ctx.stroke()
        }
        function poly(points, closePath) {
            ctx.beginPath()
            ctx.moveTo(x(points[0][0]), y(points[0][1]))
            for (var i = 1; i < points.length; ++i) {
                ctx.lineTo(x(points[i][0]), y(points[i][1]))
            }
            if (closePath) ctx.closePath()
            ctx.stroke()
        }
        function path(points, closePath) {
            ctx.beginPath()
            ctx.moveTo(x(points[0][0]), y(points[0][1]))
            for (var i = 1; i < points.length; ++i) {
                ctx.lineTo(x(points[i][0]), y(points[i][1]))
            }
            if (closePath) ctx.closePath()
            ctx.stroke()
        }

        var lib = library === "tabler" || library === "phosphor" ? library : "lucide"
        var strokeWidth = stroke
        if (lib === "tabler") strokeWidth = 2.0
        if (lib === "phosphor") strokeWidth = 1.65
        ctx.strokeStyle = iconColor
        ctx.lineWidth = strokeWidth * s
        ctx.lineCap = "round"
        ctx.lineJoin = "round"

        if (lib === "tabler") {
            switch (name) {
            case "link":
                ctx.beginPath(); ctx.arc(x(9), y(12), 4 * s, Math.PI * 0.55, Math.PI * 1.45); ctx.stroke()
                ctx.beginPath(); ctx.arc(x(15), y(12), 4 * s, -Math.PI * 0.45, Math.PI * 0.45); ctx.stroke()
                line(9, 12, 15, 12)
                return
            case "unlink":
                line(4, 4, 20, 20)
                ctx.beginPath(); ctx.arc(x(9), y(12), 4 * s, Math.PI * 0.6, Math.PI * 1.4); ctx.stroke()
                ctx.beginPath(); ctx.arc(x(15), y(12), 4 * s, -Math.PI * 0.4, Math.PI * 0.4); ctx.stroke()
                return
            case "scan":
                rect(5, 5, 14, 14); line(8, 12, 16, 12)
                return
            case "settings":
                circle(12, 12, 3); circle(12, 12, 7.5); line(12, 3, 12, 5); line(12, 19, 12, 21); line(3, 12, 5, 12); line(19, 12, 21, 12)
                return
            }
        } else if (lib === "phosphor") {
            switch (name) {
            case "link":
                line(9, 15, 15, 9)
                ctx.beginPath(); ctx.moveTo(x(8), y(9)); ctx.bezierCurveTo(x(10), y(7), x(12), y(7), x(14), y(9)); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(x(10), y(15)); ctx.bezierCurveTo(x(12), y(17), x(14), y(17), x(16), y(15)); ctx.stroke()
                circle(8, 16, 3)
                circle(16, 8, 3)
                return
            case "unlink":
                line(4, 4, 20, 20)
                circle(8, 16, 3)
                circle(16, 8, 3)
                return
            case "play":
                ctx.beginPath(); ctx.moveTo(x(8), y(5)); ctx.lineTo(x(19), y(12)); ctx.lineTo(x(8), y(19)); ctx.closePath(); ctx.fillStyle = iconColor; ctx.fill()
                return
            case "square":
                ctx.fillStyle = iconColor; ctx.fillRect(x(7), y(7), 10 * s, 10 * s)
                return
            case "pause":
                ctx.fillStyle = iconColor; ctx.fillRect(x(8), y(5), 3.5 * s, 14 * s); ctx.fillRect(x(13), y(5), 3.5 * s, 14 * s)
                return
            case "zap":
                path([[13,2],[5,13],[11,13],[9,22],[19,10],[13,10]], true)
                return
            }
        }

        switch (name) {
        case "link":
            line(10, 13, 14, 9)
            ctx.beginPath(); ctx.moveTo(x(9), y(7)); ctx.bezierCurveTo(x(11), y(5), x(14), y(5), x(16), y(7)); ctx.bezierCurveTo(x(18), y(9), x(18), y(12), x(16), y(14)); ctx.lineTo(x(14), y(16)); ctx.stroke()
            ctx.beginPath(); ctx.moveTo(x(10), y(8)); ctx.lineTo(x(8), y(10)); ctx.bezierCurveTo(x(6), y(12), x(6), y(15), x(8), y(17)); ctx.bezierCurveTo(x(10), y(19), x(13), y(19), x(15), y(17)); ctx.stroke()
            break
        case "unlink":
            line(4, 4, 20, 20)
            line(10, 13, 14, 9)
            line(15, 7, 16, 7)
            line(8, 17, 9, 17)
            break
        case "zap":
            path([[13,2],[4,14],[11,14],[9,22],[20,10],[13,10]], true)
            break
        case "scan":
            line(4, 8, 4, 5); line(4, 5, 7, 5); line(17, 5, 20, 5); line(20, 5, 20, 8)
            line(4, 16, 4, 19); line(4, 19, 7, 19); line(17, 19, 20, 19); line(20, 19, 20, 16)
            line(7, 12, 17, 12)
            break
        case "refresh-cw":
            ctx.beginPath(); ctx.arc(x(12), y(12), 7 * s, -0.2, Math.PI * 1.35); ctx.stroke()
            poly([[19,5],[19,10],[14,10]], false)
            break
        case "play":
            path([[8,5],[19,12],[8,19]], true)
            break
        case "pause":
            line(9, 5, 9, 19); line(15, 5, 15, 19)
            break
        case "square":
            rect(6, 6, 12, 12)
            break
        case "cpu":
            rect(7, 7, 10, 10); rect(10, 10, 4, 4)
            line(9, 2, 9, 5); line(15, 2, 15, 5); line(9, 19, 9, 22); line(15, 19, 15, 22)
            line(2, 9, 5, 9); line(2, 15, 5, 15); line(19, 9, 22, 9); line(19, 15, 22, 15)
            break
        case "layout-dashboard":
            rect(4, 4, 6, 7); rect(14, 4, 6, 5); rect(4, 15, 6, 5); rect(14, 13, 6, 7)
            break
        case "table-properties":
            rect(4, 5, 16, 14); line(4, 10, 20, 10); line(9, 10, 9, 19); line(15, 10, 15, 19)
            break
        case "activity":
            poly([[3,12],[7,12],[10,4],[14,20],[17,12],[21,12]], false)
            break
        case "folder-open":
            ctx.beginPath(); ctx.moveTo(x(3), y(7)); ctx.lineTo(x(9), y(7)); ctx.lineTo(x(11), y(9)); ctx.lineTo(x(21), y(9)); ctx.lineTo(x(19), y(19)); ctx.lineTo(x(3), y(19)); ctx.closePath(); ctx.stroke()
            break
        case "satellite":
            rect(9, 9, 6, 6); line(12, 15, 12, 22); line(8, 22, 16, 22); line(5, 5, 9, 9); line(15, 15, 19, 19)
            break
        case "file-code":
            poly([[14,3],[6,3],[6,21],[18,21],[18,7],[14,3]], false); line(14, 3, 14, 7); line(14, 7, 18, 7); poly([[10,12],[8,14],[10,16]], false); poly([[14,12],[16,14],[14,16]], false)
            break
        case "settings":
            circle(12, 12, 3); circle(12, 12, 8)
            line(12, 2, 12, 4); line(12, 20, 12, 22); line(2, 12, 4, 12); line(20, 12, 22, 12); line(5, 5, 6.5, 6.5); line(17.5, 17.5, 19, 19); line(19, 5, 17.5, 6.5); line(6.5, 17.5, 5, 19)
            break
        case "upload":
            line(12, 15, 12, 3); poly([[7,8],[12,3],[17,8]], false); line(5, 19, 19, 19)
            break
        case "download":
            line(12, 3, 12, 15); poly([[7,10],[12,15],[17,10]], false); line(5, 19, 19, 19)
            break
        case "trash-2":
            line(4, 7, 20, 7); rect(7, 7, 10, 14); line(10, 11, 10, 17); line(14, 11, 14, 17); line(9, 4, 15, 4)
            break
        case "save":
            rect(5, 4, 14, 16); line(8, 4, 8, 10); line(16, 4, 16, 10); rect(8, 14, 8, 6)
            break
        case "rotate-ccw":
            ctx.beginPath(); ctx.arc(x(12), y(12), 7 * s, Math.PI * 0.25, Math.PI * 1.75); ctx.stroke()
            poly([[4,5],[4,10],[9,10]], false)
            break
        case "wifi":
            ctx.beginPath(); ctx.arc(x(12), y(18), 2 * s, Math.PI, 0); ctx.stroke()
            ctx.beginPath(); ctx.arc(x(12), y(18), 6 * s, Math.PI, 0); ctx.stroke()
            ctx.beginPath(); ctx.arc(x(12), y(18), 10 * s, Math.PI, 0); ctx.stroke()
            break
        default:
            circle(12, 12, 8)
            break
        }
    }
}
