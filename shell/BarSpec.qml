pragma Singleton
// BarSpec.qml — what the bar(s) look like, as data. Bar.qml draws whatever this says.
//
// Config.bars (settings.json "bars": [...]) is a list of bars; empty = one bar built from the
// classic Settings → Bar options (legacy(), so presets and the Customize tab keep working).
// docs/bar.md has the whole format; in short:
//
//   { "position": "top", "size": 28, "margin": [edge, inner, sides], "bg": "bg/0.5", "radius": 12,
//     "group":  { style every group starts from },   "module": { style every module starts from },
//     "start": [ entries ], "center": [ entries ], "end": [ entries ] }
//
//   entry  = "clock" | { "type": "cpu", "format": " {usage}%", "fg": "accent", ... }
//          | { "type": "group", "bg": "card", "radius": 8, "modules": [ entries ] }
//   colour = theme token (accent accent2 text dim faint bg card danger ok warn) | "#rrggbb", "/0.5" = alpha
//
// Style cascades bar → group → module; "when": { "<state>": { …overrides } } re-styles a module
// in a state (muted, paused, warning, critical, charging, alt …).
import Quickshell
import QtQuick

Singleton {
    id: root

    property var host: null        // Bar.qml registers itself (previews borrow its media / audio state)

    // ---------------------------------------------------------------- the bars
    readonly property var bars: {
        // settings.json hands back Qt list/map types: make them plain JS (Array.isArray etc.) once
        let raw = []
        try { raw = Config.bars && Config.bars.length ? JSON.parse(JSON.stringify(Config.bars)) : [] } catch (e) { raw = [] }
        const list = raw.length ? raw : [legacy()]
        return list.map((b, i) => normBar(b, i))
    }
    // which module types are on screen anywhere: data sources run only for these (SysInfo etc.)
    readonly property var uses: {
        const u = {}
        for (const b of bars) for (const s of ["start", "center", "end"]) for (const g of b[s]) for (const m of g.modules) u[m.type] = true
        return u
    }
    // the first bar decides where popups, toasts and the control panel open from
    readonly property var primary: bars[0]
    readonly property string edge: primary ? primary.position : "top"
    readonly property bool vertical: edge === "left" || edge === "right"
    readonly property int thickness: primary ? primary.size + primary.margin[0] + primary.margin[1] : 0

    // Variants keys: one bar window per (bar, screen); strings so unrelated edits keep the windows
    function instanceKeys(screens) {
        const out = []
        bars.forEach((b, i) => {
            for (const s of screens) if (screenMatches(b.screen, s.name)) out.push(i + "|" + s.name)
        })
        return out
    }
    function screenMatches(want, name) {
        if (!want || want === "all" || (Array.isArray(want) && !want.length)) return true
        return Array.isArray(want) ? want.includes(name) : want === name
    }

    // ---------------------------------------------------------------- normalising
    readonly property var barDefaults: ({
        position: "top", screen: "", size: 28, length: 0, align: "center",
        margin: [0, 0, 0], padding: 0, spacing: 0,
        bg: "transparent", border: "", borderWidth: 0, radius: 0, line: null, shadow: false,
        exclusive: true, layer: "top", autohide: false,
        font: "", fontSize: 0, fg: "text"
    })
    readonly property var groupDefaults: ({
        bg: "transparent", border: "", borderWidth: 0, radius: 0, padding: [0, 0], gap: [0, 0], inset: [0, 0],
        spacing: 0, capStart: "none", capEnd: "none", opacity: 1, line: null, hover: true
    })
    readonly property var moduleDefaults: ({
        bg: "transparent", fg: "", border: "", borderWidth: 0, radius: 0, padding: [10, 10], gap: [2, 2], inset: [0, 0],
        font: "", fontSize: 0, bold: false, italic: false, opacity: 1, hoverGrow: 0, hoverBg: "", hoverFg: "",
        capStart: "none", capEnd: "none", line: null, when: {}, states: null, icons: null
    })

    function pair(v, d) {
        if (v === undefined || v === null) return d
        if (Array.isArray(v)) return v.length === 1 ? [v[0], v[0]] : [v[0], v[1]]
        return [v, v]
    }
    // margin: n | [edge, inner, sides] | [edge, sides] | { edge, inner, sides }
    function triple(v) {
        if (v === undefined || v === null) return [0, 0, 0]
        if (typeof v === "number") return [v, v, v]
        if (Array.isArray(v)) return v.length >= 3 ? [v[0], v[1], v[2]] : v.length === 2 ? [v[0], v[0], v[1]] : [v[0], v[0], v[0]]
        return [v.edge ?? 0, v.inner ?? 0, v.sides ?? 0]
    }

    function normBar(b, i) {
        const o = Object.assign({}, barDefaults, b)
        o.index = i
        o.margin = triple(b.margin)
        o.fontSize = b.fontSize || Config.bar.fontSize
        o.font = b.font || Theme.fontFamily
        o.group = Object.assign({}, groupDefaults, b.group || {})
        o.module = Object.assign({}, moduleDefaults, b.module || {})
        for (const s of ["start", "center", "end"]) o[s] = (b[s] || []).map(e => normEntry(e, o))
        return o
    }
    // a bare module in a section becomes a group of one with no look of its own
    function normEntry(e, bar) {
        if (typeof e === "string") e = { type: e }
        if (e.type === "group") {
            const g = Object.assign({}, bar.group, e)
            g.padding = pair(g.padding, [0, 0]); g.gap = pair(g.gap, [0, 0]); g.inset = pair(g.inset, [0, 0])
            const md = Object.assign({}, bar.module, e.module || {})
            g.modules = (e.modules || []).map(m => normModule(m, md))
            return g
        }
        const g = Object.assign({}, groupDefaults, { bare: true, inset: e.groupInset ?? bar.group.inset, hover: false })
        g.inset = pair(g.inset, [0, 0])
        g.modules = [normModule(e, bar.module)]
        return g
    }
    function normModule(m, defs) {
        if (typeof m === "string") m = { type: m }
        const o = Object.assign({}, defs, m)
        o.padding = pair(o.padding, [10, 10]); o.gap = pair(o.gap, [2, 2]); o.inset = pair(o.inset, [0, 0])
        // waybar habit: format-alt
        if (m.formatAlt !== undefined) o.when = Object.assign({}, o.when, { alt: Object.assign({ format: m.formatAlt }, (o.when || {}).alt || {}) })
        return o
    }

    // ---------------------------------------------------------------- colours / text
    // width a powerline cap takes along the bar (round caps are just corners)
    function capSize(kind, cross) { return kind === "arrow" || kind === "arrow-in" || kind === "slant" || kind === "slant-back" ? Math.round(cross / 2) : 0 }
    // does a style draw anything? (transparent, borderless, capless boxes are never created)
    function draws(s) {
        const vis = c => c !== undefined && c !== null && c !== "" && c !== "transparent" && c !== "none"
        return vis(s.bg) || vis(s.hoverBg) || (s.borderWidth > 0 && vis(s.border)) || !!s.line
            || (s.capStart && s.capStart !== "none") || (s.capEnd && s.capEnd !== "none")
    }
    readonly property color warn: Qt.tint(Theme.danger, Qt.rgba(1, 0.78, 0.1, 0.6))
    function color(tok, fallback) {
        if (tok === undefined || tok === null || tok === "") return fallback === undefined ? "transparent" : color(fallback)
        if (typeof tok !== "string") return tok
        let a = -1
        const slash = tok.lastIndexOf("/")
        if (slash > 0) { a = parseFloat(tok.slice(slash + 1)); tok = tok.slice(0, slash) }
        const named = { accent: Theme.accent, accent2: Theme.accent2, text: Theme.text, fg: Theme.text, dim: Theme.textDim,
                        faint: Theme.textFaint, bg: Theme.bgPanel, panel: Theme.bgPanel, card: Theme.bgCard,
                        danger: Theme.danger, ok: Theme.ok, warn: warn, border: Theme.border }
        if (tok === "transparent" || tok === "none") return "transparent"
        const c = named[tok] ?? tok
        return a >= 0 ? Theme.alpha(c, a) : c
    }
    // "{name}" / "{name:5}" (pad left) / "{name:-5}" (pad right) from vars; values are escaped,
    // the format itself may carry <b>, <i>, <u>, <font color="accent"> markup
    function render(fmt, vars) {
        if (fmt === undefined || fmt === null) return ""
        let s = String(fmt).replace(/\{(\w+)(?::(-?\d+))?\}/g, (all, k, w) => {
            if (!(k in vars)) return all
            let v = vars[k]
            v = v === undefined || v === null ? "" : String(v)
            if (w) { const n = parseInt(w); v = n > 0 ? v.padStart(n) : v.padEnd(-n) }
            return esc(v)
        })
        if (s.indexOf("color=") >= 0) s = s.replace(/color=(["'])([\w/.#]+)\1/g, (all, q, t) => `color="${String(color(t))}"`)
        return s
    }
    function esc(s) { return s.indexOf("&") < 0 && s.indexOf("<") < 0 && s.indexOf(">") < 0 ? s : s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;") }
    function fontSize(v, base) {
        if (typeof v === "string" && /^[+-]/.test(v)) return base + parseInt(v)
        return v ? +v : base
    }

    // ---------------------------------------------------------------- the classic bar
    // Settings → Bar / Workspaces, laid out exactly like the hand-built bar before the engine
    function legacy() {
        const b = Config.bar, mods = b.modules, ws = Config.workspaces
        const islands = b.style === "islands", floating = b.style === "floating", solid = b.style === "solid"
        const centered = b.layout === "centered", bottom = b.position === "bottom"
        const acc = b.accentLabels
        const lbl = t => acc ? `<font color="accent">${t}</font>` : t
        const flush = ws.style === "dwl" && !centered
        const scrollFor = a => a === "volume" ? { scrollUp: "volume:up", scrollDown: "volume:down" }
            : a === "workspaces" ? { scrollUp: "workspace:prev", scrollDown: "workspace:next" } : { scrollUp: "none", scrollDown: "none" }
        const stats = scrollFor(b.scrollStats)
        const island = {
            type: "group",
            bg: islands ? "bg/" + b.opacity : "transparent",
            border: b.outline && islands ? "text/0.12" : "", borderWidth: b.outline && islands ? 1 : 0,
            radius: b.radius
        }
        const hg = b.hoverGrow ? 4 : 0
        const big = n => "+" + n

        const wsGroup = Object.assign({}, island, {
            gap: flush ? [0, 0] : centered ? [0, 0] : [6, 0],
            inset: flush ? [0, 0] : undefined,
            radius: flush ? 0 : b.radius,
            scrollUp: "workspace:prev", scrollDown: "workspace:next",
            modules: [
                { type: "workspaces", padding: flush ? [0, 0] : [8, 8], gap: [0, 0] },
                { type: "title", show: mods.title && !centered, padding: [12, 10], maxLength: b.titleWidth }
            ]
        })
        const clockGroup = Object.assign({}, island, {
            gap: centered ? [6, 0] : [0, 0],
            modules: [{ type: "clock", timeFormat: b.clock, bold: b.clockBold, hoverGrow: hg,
                        scrollUp: scrollFor(b.scrollClock).scrollUp, scrollDown: scrollFor(b.scrollClock).scrollDown }]
        })
        const right = []
        if (mods.media) right.push(Object.assign({ type: "media", width: b.mediaWidth, marquee: b.marquee, popup: b.mediaPopup, hoverGrow: hg }, stats))
        right.push({ type: "caffeine", fg: acc ? "accent" : "", fontSize: big(2), padding: [10, 7] })
        if (mods.cpu) right.push(Object.assign({ type: "cpu", format: `${lbl("CPU")} {usage}%`, hoverGrow: hg }, stats))
        if (mods.ram) right.push(Object.assign({ type: "ram", format: `${lbl("RAM")} {percent}%`, hoverGrow: hg }, stats))
        if (mods.gpu) right.push(Object.assign({ type: "gpu", format: `${lbl("GPU")} {usage}%`, hoverGrow: hg }, stats))
        if (mods.temp) right.push(Object.assign({ type: "temp", hoverGrow: hg }, stats))
        if (mods.volume) right.push({ type: "volume", fontSize: big(2), padding: [10, 0], gap: [2, 0], hoverGrow: hg })
        if (mods.network) right.push(Object.assign({ type: "network", fg: acc ? "accent" : "", fontSize: big(2), padding: [10, 7], hoverGrow: hg }, stats))
        if (mods.tray) right.push({ type: "tray", padding: [10, 6], gap: [0, 0], iconSize: b.trayIconSize })
        if (mods.power) right.push(Object.assign({ type: "power", fg: acc ? "accent" : "", fontSize: big(4), padding: [10, 6], gap: [2, 10] }, stats))
        const rightGroup = Object.assign({}, island, { gap: [0, 6], modules: right })

        return {
            position: bottom ? "bottom" : "top",
            size: floating ? b.height - 4 : b.height,
            margin: floating ? [3, 1, 6] : [0, 0, 0],
            bg: solid || floating ? "bg/" + b.opacity : "transparent",
            radius: floating ? b.radius : 0,
            // Mango's outer gap is one value (8) for top and bottom; Hyprland puts gaps_out top=3 under
            // the bar — reserve 5 less on Mango so windows land on the same 3px
            exclusive: Wm.kind === "mango" && !bottom ? b.height - 5 - (floating ? 0 : 0) : true,
            group: { inset: floating ? [1, 1] : bottom ? [4, 2] : [4, 2] },
            start: [centered ? clockGroup : wsGroup],
            center: [centered ? wsGroup : clockGroup],
            end: [rightGroup]
        }
    }
}
