// CpSpark.qml — tiny area chart for a history list (cpu %, bytes/s…). Repaints only on new data.
import QtQuick

Canvas {
    id: root

    property var values: []
    property real max: 0                // 0 = scale to the largest value
    property color color: Theme.accent

    onValuesChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const v = values, n = v.length
        if (n < 2) return
        const top = max > 0 ? max : Math.max(1, ...v) * 1.15
        const step = width / (SysInfo.historyLength - 1)
        const x0 = width - (n - 1) * step
        const y = val => height - 2 - (height - 4) * Math.min(1, val / top)

        ctx.beginPath()
        ctx.moveTo(x0, y(v[0]))
        for (let i = 1; i < n; i++) ctx.lineTo(x0 + i * step, y(v[i]))
        ctx.lineWidth = 1.6
        ctx.strokeStyle = root.color
        ctx.lineJoin = "round"
        ctx.stroke()

        ctx.lineTo(x0 + (n - 1) * step, height)
        ctx.lineTo(x0, height)
        ctx.closePath()
        const g = ctx.createLinearGradient(0, 0, 0, height)
        g.addColorStop(0, Qt.rgba(root.color.r, root.color.g, root.color.b, 0.28))
        g.addColorStop(1, Qt.rgba(root.color.r, root.color.g, root.color.b, 0))
        ctx.fillStyle = g
        ctx.fill()
    }
}
