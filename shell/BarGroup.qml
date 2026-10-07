// BarGroup.qml — an island of modules (or, for a bare module in a section, an invisible holder).
// Along the bar: gap | cap padding modules padding cap | gap. Across: inset [edge side, inner side].
import QtQuick

Item {
    id: g

    required property var spec             // normalised group
    required property var host
    required property var win
    property real cross: 28                 // bar thickness to sit in

    readonly property bool vertical: win.vertical
    readonly property alias box: box       // the control panel grows out of the clock's group

    readonly property real i0: spec.inset[0]
    readonly property real i1: spec.inset[1]
    readonly property real innerCross: cross - i0 - i1
    readonly property real modsAlong: vertical ? grid.implicitHeight : grid.implicitWidth
    readonly property bool any: modsAlong > 0
    readonly property real capS: BarSpec.capSize(spec.capStart, innerCross)
    readonly property real capE: BarSpec.capSize(spec.capEnd, innerCross)
    readonly property real along: any ? spec.gap[0] + capS + spec.padding[0] + modsAlong + spec.padding[1] + capE + spec.gap[1] : 0

    visible: any
    width: vertical ? cross : along
    height: vertical ? along : cross

    // the clock's island steps aside while the control panel (which grows out of it) is open
    readonly property bool morphSource: host.clockGroup === g
    opacity: morphSource && SysInfo.panelOpen ? 0 : spec.opacity
    Behavior on opacity { NumberAnimation { duration: 140 } }

    // clicks / scrolling on the island (between modules too) — modules without their own pass them here
    MouseArea {
        anchors.fill: box
        z: -1
        acceptedButtons: g.spec.click || g.spec.rightClick || g.spec.middleClick ? Qt.LeftButton | Qt.RightButton | Qt.MiddleButton : Qt.NoButton
        cursorShape: g.spec.click ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: e => {
            const a = g.spec[e.button === Qt.RightButton ? "rightClick" : e.button === Qt.MiddleButton ? "middleClick" : "click"]
            if (a && a !== "none") g.host.runAction(a, null, 0)
        }
        onWheel: e => {
            if (e.angleDelta.y === 0) return
            const up = e.angleDelta.y > 0, a = g.spec[up ? "scrollUp" : "scrollDown"]
            if (a && a !== "none") g.host.runAction(a, null, up ? 1 : -1)
        }
    }

    // the island's rectangle (the control panel morphs from it); its drawing only when it draws something
    Item {
        id: box
        x: g.vertical ? (g.win.edge === "right" ? g.i1 : g.i0) : g.spec.gap[0]
        y: g.vertical ? g.spec.gap[0] : (g.win.edge === "bottom" ? g.i1 : g.i0)
        width: g.vertical ? g.innerCross : g.along - g.spec.gap[0] - g.spec.gap[1]
        height: g.vertical ? g.along - g.spec.gap[0] - g.spec.gap[1] : g.innerCross

        Loader {
            anchors.fill: parent
            active: BarSpec.draws(g.spec)
            sourceComponent: BarBox {
        vertical: g.vertical
        cross: g.innerCross
        fill: Array.isArray(g.spec.bg) ? g.spec.bg.map(c => BarSpec.color(c)) : BarSpec.color(g.spec.bg)
        stroke: BarSpec.color(g.spec.border)
        strokeWidth: g.spec.borderWidth
        radius: g.spec.radius
        capStart: g.spec.capStart
        capEnd: g.spec.capEnd
        capStartBg: BarSpec.color(g.spec.capStartBg)
        capEndBg: BarSpec.color(g.spec.capEndBg)
        line: g.spec.line
            }
        }

        Grid {
            id: grid
            x: g.vertical ? 0 : g.capS + g.spec.padding[0]
            y: g.vertical ? g.capS + g.spec.padding[0] : 0
            // exactly one line: n × 1 or 1 × n (spare rows would still take spacing)
            rows: g.vertical ? Math.max(1, g.spec.modules.length) : 1
            columns: g.vertical ? 1 : Math.max(1, g.spec.modules.length)
            flow: g.vertical ? Grid.TopToBottom : Grid.LeftToRight
            spacing: g.spec.spacing
            Repeater {
                model: g.spec.modules.length
                BarModule {
                    required property int index
                    spec: g.spec.modules[index] ?? { type: "text", show: false }
                    host: g.host
                    win: g.win
                    group: g
                    cross: g.innerCross
                }
            }
        }
    }
}
