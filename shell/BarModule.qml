// BarModule.qml — one module of the bar. The look and the behaviour are generic (spec + style
// cascade); the data comes from a provider file loaded for its type only, Bm<Type>.qml:
//   vars {…} for the format · level 0–100 (icons, warning/critical) · status ("muted", "paused"…)
//   format / icons / actions / tooltip defaults · visual: true when it draws itself (workspaces, tray)
// "when": { state: {…} } re-styles in a state; thresholds come from "states": { warning: 70, critical: 90 }.
import Quickshell
import QtQuick

Item {
    id: m

    required property var spec             // normalised module (BarSpec.normModule)
    required property var host             // Bar.qml
    required property var win              // BarWindow
    property var group: null               // BarGroup
    property real cross: 22                 // thickness given by the group

    readonly property bool vertical: win.vertical
    readonly property string type: spec.type || "text"

    // ---- provider ----
    readonly property var p: prov.status === Loader.Ready ? prov.item : null
    readonly property real innerCross: cross - spec.inset[0] - spec.inset[1]

    // ---- state + effective spec ----
    property bool altOn: false
    readonly property real level: p && p.level !== undefined ? p.level : -1
    readonly property string threshold: {
        const st = spec.states ?? (p ? p.thresholds : null)
        if (!st || level < 0) return ""
        const low = p && p.lowIsBad === true
        let best = "", bestV = low ? Infinity : -Infinity
        for (const k in st) {
            const v = +st[k]
            if (low ? (level <= v && v < bestV) : (level >= v && v > bestV)) { best = k; bestV = v }
        }
        return best
    }
    readonly property var states: {
        const s = []
        if (threshold) s.push(threshold)
        if (p && p.status) for (const x of String(p.status).split(" ")) if (x) s.push(x)
        if (altOn) s.push("alt")
        if (hovered) s.push("hover")
        return s
    }
    readonly property var eff: {
        const w = spec.when
        if (!w || !states.length) return spec
        let o = spec
        for (const s of states) if (w[s]) o = Object.assign({}, o, w[s])
        return o
    }

    // ---- text ----
    readonly property var icons: eff.icons ?? (p ? p.icons : null)
    readonly property string icon: {
        const ic = icons
        if (!ic) return p && p.icon !== undefined ? p.icon : ""
        if (Array.isArray(ic)) return ic.length ? ic[Math.max(0, Math.min(ic.length - 1, Math.floor(Math.max(0, level) / 100 * ic.length)))] : ""
        for (const s of states.slice().reverse()) if (ic[s] !== undefined) return ic[s]
        return ic["default"] ?? (p && p.icon !== undefined ? p.icon : "")
    }
    readonly property string format: eff.format ?? (p ? p.format : "")
    readonly property string text: {
        if (!p) return ""
        const v = Object.assign({ icon: icon }, p.vars)
        return BarSpec.render(format, v)
    }
    readonly property bool shown: spec.show !== false && !!p && p.shown !== false && (p.visual === true || text !== "")
    visible: shown

    readonly property real baseFont: BarSpec.fontSize(eff.fontSize, win.spec.fontSize)
    readonly property color fg: BarSpec.color(hovered && eff.hoverFg ? eff.hoverFg : eff.fg || (p && p.fg) || win.spec.fg, "text")

    // ---- geometry: gap | cap pad content pad cap | gap ----
    readonly property real boxCross: cross - spec.inset[0] - spec.inset[1]
    readonly property real capS: BarSpec.capSize(eff.capStart, boxCross)
    readonly property real capE: BarSpec.capSize(eff.capEnd, boxCross)
    // side bars: turn the text when asked, or (unless rotate: false) when it is wider than the bar
    readonly property bool turned: vertical && (eff.rotate === true || (eff.rotate !== false && label.implicitWidth > innerCross - 2))
    readonly property real labelAlong: text === "" ? 0 : vertical && !turned ? label.implicitHeight : label.implicitWidth
    readonly property real provAlong: p && p.visual ? (vertical ? prov.height : prov.width) : 0
    readonly property real contentAlong: labelAlong + provAlong + (labelAlong > 0 && provAlong > 0 ? 6 : 0)
    readonly property real along: shown ? spec.gap[0] + capS + eff.padding[0] + contentAlong + eff.padding[1] + capE + spec.gap[1] : 0
    width: vertical ? cross : along
    height: vertical ? along : cross

    Item {
        id: inner
        // module box across the bar: inset [edge, inner] from the group's thickness
        readonly property real i0: m.spec.inset[0]
        readonly property real i1: m.spec.inset[1]
        x: m.vertical ? (m.win.edge === "right" ? i1 : i0) : m.spec.gap[0]
        y: m.vertical ? m.spec.gap[0] : (m.win.edge === "bottom" ? i1 : i0)
        width: m.vertical ? m.cross - i0 - i1 : m.along - m.spec.gap[0] - m.spec.gap[1]
        height: m.vertical ? m.along - m.spec.gap[0] - m.spec.gap[1] : m.cross - i0 - i1
        opacity: m.eff.opacity
        Behavior on opacity { NumberAnimation { duration: 200 } }

        // only modules that draw something get a background at all
        Loader {
            anchors.fill: parent
            active: BarSpec.draws(m.spec) || BarSpec.draws(m.eff)
            sourceComponent: BarBox {
            vertical: m.vertical
            cross: m.boxCross
            fill: {
                const f = m.hovered && m.eff.hoverBg ? m.eff.hoverBg : m.eff.bg
                return Array.isArray(f) ? f.map(c => BarSpec.color(c)) : BarSpec.color(f)
            }
            stroke: BarSpec.color(m.eff.border)
            strokeWidth: m.eff.borderWidth
            radius: m.eff.radius
            capStart: m.eff.capStart
            capEnd: m.eff.capEnd
            capStartBg: BarSpec.color(m.eff.capStartBg)
            capEndBg: BarSpec.color(m.eff.capEndBg)
            line: m.eff.line
            }
        }

        Text {
            id: label
            visible: m.text !== ""
            text: m.text
            textFormat: m.text.indexOf("<") >= 0 || m.text.indexOf("&") >= 0 ? Text.StyledText : Text.PlainText
            horizontalAlignment: Text.AlignHCenter      // multi-line formats ("{icon}\n{volume}") centre each line
            color: m.fg
            font.family: m.eff.font || m.win.spec.font
            font.pixelSize: m.baseFont + (m.hovered ? m.eff.hoverGrow : 0)
            font.bold: m.eff.bold
            font.italic: m.eff.italic
            font.hintingPreference: Font.PreferFullHinting
            renderType: Text.NativeRendering
            rotation: m.turned ? (m.win.edge === "left" ? -90 : 90) : 0
            // rotated text keeps its unrotated box; centre it on the slot either way
            readonly property real cx: m.vertical ? parent.width / 2 : m.capS + m.eff.padding[0] + implicitWidth / 2
            readonly property real cy: m.vertical ? m.capS + m.eff.padding[0] + m.labelAlong / 2 : parent.height / 2
                + (m.baseFont > m.win.spec.fontSize ? 1 : 0)     // GTK sits the bigger glyphs 1px lower
            x: Math.round(cx - implicitWidth / 2)
            y: Math.round(cy - implicitHeight / 2)
            Behavior on font.pixelSize { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: 160 } }
        }

        Loader {
            id: prov
            source: "Bm" + m.type.charAt(0).toUpperCase() + m.type.slice(1) + ".qml"
            onLoaded: item.m = m
            visible: m.p ? m.p.visual === true : false
            readonly property real at: m.capS + m.eff.padding[0] + m.labelAlong + (m.labelAlong > 0 ? 6 : 0)
            x: m.vertical ? Math.round((parent.width - width) / 2) : at
            y: m.vertical ? at : Math.round((parent.height - height) / 2)
        }
    }

    // ---- input ----
    readonly property alias hovered: mouse.containsMouse
    readonly property string tooltip: {
        if (!hovered || !p) return ""
        const t = eff.tooltip
        if (t === false) return ""
        if (typeof t === "string") return BarSpec.render(t, Object.assign({ icon: icon }, p.vars))
        return p.tooltip ?? ""
    }
    readonly property bool tooltipRich: p ? (p.tooltipRich === true || tooltip.indexOf("<") >= 0) : false

    function actionFor(key) {
        const own = eff[key]
        if (own !== undefined) return own
        if (p && p.actions && p.actions[key] !== undefined) return p.actions[key]
        if ((key === "scrollUp" || key === "scrollDown") && group) return group.spec[key]
        return undefined
    }
    function fire(key, steps) {
        const a = actionFor(key)
        if (a === undefined || a === "none" || a === "") return false
        host.runAction(a, m, steps || 0)
        if (p && p.acted) p.acted(key)
        return true
    }

    MouseArea {
        id: mouse
        anchors.fill: inner
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        cursorShape: m.actionFor("click") !== undefined ? Qt.PointingHandCursor : Qt.ArrowCursor
        function keyOf(b) { return b === Qt.RightButton ? "rightClick" : b === Qt.MiddleButton ? "middleClick" : "click" }
        // no action of its own for this button: let the press through to the group
        onPressed: e => e.accepted = m.actionFor(keyOf(e.button)) !== undefined
        onClicked: e => m.fire(keyOf(e.button))
        onWheel: e => {
            if (e.angleDelta.y === 0) return
            const up = e.angleDelta.y > 0
            e.accepted = m.fire(up ? "scrollUp" : "scrollDown", up ? 1 : -1)
        }
        onContainsMouseChanged: containsMouse ? m.host.showTip(m) : m.host.hideTip(m)
    }
}
