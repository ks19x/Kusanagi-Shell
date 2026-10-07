// BarBox.qml — the background of a bar, group or module: fill (colour or [from, to] gradient),
// border, per-corner radius, powerline caps on either end and an indicator line.
//   caps: none | round | arrow | arrow-in | slant | slant-back   (shapes only exist where used;
//         capStartBg / capEndBg fill behind them, so touching segments show the neighbour's colour)
//   line: { pos: bottom|top|left|right, width: 2, color: "accent", length: 1 (fraction) }
import QtQuick
import QtQuick.Shapes

Item {
    id: box

    property bool vertical: false
    property var fill: "transparent"           // colour, or an array of two colours
    property color stroke: "transparent"
    property real strokeWidth: 0
    property var radius: 0                     // n | [tl, tr, br, bl]
    property string capStart: "none"
    property string capEnd: "none"
    property var line: null
    property color capStartBg: "transparent"   // behind a cap: the neighbour's colour, for touching powerline segments
    property color capEndBg: "transparent"

    // thickness across the bar; parents that size the box from its caps pass it in (no binding loop)
    property real cross: vertical ? width : height
    function capSize(k) { return k === "arrow" || k === "arrow-in" || k === "slant" || k === "slant-back" ? Math.round(cross / 2) : 0 }
    readonly property real startW: capSize(capStart)
    readonly property real endW: capSize(capEnd)

    readonly property bool grad: Array.isArray(fill) && fill.length > 1
    readonly property color c0: Array.isArray(fill) ? (fill[0] ?? "transparent") : fill
    readonly property color c1: Array.isArray(fill) ? (fill[fill.length - 1] ?? "transparent") : fill

    // corner i (0 tl, 1 tr, 2 br, 3 bl) sits on the start or the end side
    function corner(i) {
        const atStart = vertical ? (i === 0 || i === 1) : (i === 0 || i === 3)
        const cap = atStart ? capStart : capEnd
        if (cap === "round") return Math.floor(cross / 2)
        if (cap !== "none") return 0
        return Array.isArray(radius) ? (radius[i] ?? 0) : radius
    }

    Rectangle {
        id: body
        x: box.vertical ? 0 : box.startW
        y: box.vertical ? box.startW : 0
        width: box.vertical ? box.width : Math.max(0, box.width - box.startW - box.endW)
        height: box.vertical ? Math.max(0, box.height - box.startW - box.endW) : box.height
        color: box.grad ? "transparent" : box.c0
        gradient: box.grad ? gr : null
        topLeftRadius: box.corner(0)
        topRightRadius: box.corner(1)
        bottomRightRadius: box.corner(2)
        bottomLeftRadius: box.corner(3)
        border.width: box.strokeWidth
        border.color: box.stroke
        Behavior on color { ColorAnimation { duration: 160 } }
    }
    Gradient {
        id: gr
        orientation: box.vertical ? Gradient.Vertical : Gradient.Horizontal
        GradientStop { position: 0; color: box.c0 }
        GradientStop { position: 1; color: box.c1 }
    }

    // ---- powerline caps ----
    component Cap: Shape {
        id: cap
        property string kind: "none"
        property bool atEnd: false
        property color paint: "transparent"
        property color back: "transparent"
        readonly property real c: box.capSize(kind)
        readonly property real h: box.cross
        width: box.vertical ? h : c
        height: box.vertical ? c : h
        preferredRendererType: Shape.CurveRenderer
        Rectangle { anchors.fill: parent; color: cap.back; visible: cap.back.a > 0 }
        // points as (along, across); the body touches along = c at the start, along = 0 at the end
        readonly property var pts: {
            const c = cap.c, h = cap.h
            let p = kind === "arrow" ? [[c, 0], [0, h / 2], [c, h]]
                  : kind === "arrow-in" ? [[0, 0], [c, 0], [c, h], [0, h], [c, h / 2]]
                  : kind === "slant" ? [[c, 0], [0, h], [c, h]]
                  : kind === "slant-back" ? [[0, 0], [c, 0], [c, h]] : []
            if (atEnd) p = p.map(q => [c - q[0], q[1]])
            return p.map(q => box.vertical ? Qt.point(q[1], q[0]) : Qt.point(q[0], q[1]))
        }
        ShapePath {
            fillColor: cap.paint
            strokeWidth: -1
            strokeColor: "transparent"
            PathPolyline { path: cap.pts.length ? cap.pts.concat([cap.pts[0]]) : [] }
        }
    }
    Loader {
        active: box.startW > 0
        sourceComponent: Cap { kind: box.capStart; paint: box.c0; back: box.capStartBg }
    }
    Loader {
        active: box.endW > 0
        x: box.vertical ? 0 : box.width - box.endW
        y: box.vertical ? box.height - box.endW : 0
        sourceComponent: Cap { kind: box.capEnd; atEnd: true; paint: box.c1; back: box.capEndBg }
    }

    // ---- indicator line (underline / overline / side bar) ----
    Loader {
        active: !!box.line && (box.line.width ?? 2) > 0
        anchors.fill: parent
        sourceComponent: Item {
            readonly property string pos: box.line.pos ?? "bottom"
            readonly property real w: box.line.width ?? 2
            readonly property real frac: box.line.length ?? 1
            readonly property bool horiz: pos === "top" || pos === "bottom"
            Rectangle {
                width: parent.horiz ? (box.width - box.startW - box.endW) * parent.frac : parent.w
                height: parent.horiz ? parent.w : (box.height - box.startW - box.endW) * parent.frac
                x: parent.pos === "right" ? box.width - width : parent.horiz ? box.startW + (box.width - box.startW - box.endW - width) / 2 : 0
                y: parent.pos === "bottom" ? box.height - height : parent.horiz ? 0 : box.startW + (box.height - box.startW - box.endW - height) / 2
                radius: box.line.round ? parent.w / 2 : 0
                color: BarSpec.color(box.line.color, "accent")
                Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
            }
        }
    }
}
