// SpBarEditor.qml — Settings → Bar → Custom layout: edit Config.bars (docs/bar.md) without JSON.
//   bars: pick / add / remove · live preview · bar options · start / center / end as chips
//   (click one to select it; groups list their modules) · inspector for the selection, with
//   colour tokens, steppers, caps, actions, and the raw JSON for anything else.
// Every change writes Config.bars; the bar on screen follows as you go.
import Quickshell
import QtQuick

Column {
    id: ed
    spacing: 22

    // ---------------------------------------------------------------- model
    readonly property var bars: Config.bars && Config.bars.length ? JSON.parse(JSON.stringify(Config.bars)) : []
    property int bi: 0
    readonly property int bIndex: Math.min(bi, Math.max(0, bars.length - 1))
    readonly property var bar: bars[bIndex] ?? ({})
    readonly property bool vertical: bar.position === "left" || bar.position === "right"

    // selection: section, entry index, module index inside a group (-1 = the entry itself)
    property string selSec: ""
    property int selGi: -1
    property int selMi: -1
    property string addingTo: ""          // a section (or "group") whose "+" picker is open
    function select(sec, gi, mi) { selSec = sec; selGi = gi; selMi = mi; addingTo = "" }
    function clearSel() { selSec = ""; selGi = -1; selMi = -1 }

    function secName(s) { return s === "center" ? "Centre" : s === "start" ? (vertical ? "Top" : "Left") : (vertical ? "Bottom" : "Right") }
    function norm(e) { return typeof e === "string" ? { type: e } : e }
    function edit(fn) { const d = JSON.parse(JSON.stringify(Config.bars)); fn(d); Config.bars = d }
    function editBar(fn) { edit(d => fn(d[bIndex])) }
    // the selected raw object inside d; strings become objects so they can take options
    function target(d) {
        const b = d[bIndex]
        if (!b || !selSec || !b[selSec] || selGi < 0 || selGi >= b[selSec].length) return null
        const list = b[selSec]
        list[selGi] = norm(list[selGi])
        if (selMi < 0) return list[selGi]
        const e = list[selGi]
        if (!e.modules || selMi >= e.modules.length) return null
        e.modules[selMi] = norm(e.modules[selMi])
        return e.modules[selMi]
    }
    // the list the selection lives in (section list, or its group's modules) + index in it
    function ownerList(d) {
        const b = d[bIndex]
        if (!b || !selSec || !b[selSec]) return null
        if (selMi < 0) return { list: b[selSec], i: selGi }
        const e = norm(b[selSec][selGi]); b[selSec][selGi] = e
        return e.modules ? { list: e.modules, i: selMi } : null
    }
    readonly property var selected: { const d = JSON.parse(JSON.stringify(bars)); return target(d) }
    readonly property bool selIsGroup: !!selected && selected.type === "group"
    // only changes with the kind of thing selected (the inspector's folds are rebuilt on that alone)
    readonly property string selKind: !selected ? "" : selIsGroup ? "group" : "module"

    function setKey(key, v) { edit(d => { const t = target(d); if (!t) return; if (v === undefined || v === null || v === "") delete t[key]; else t[key] = v }) }
    function setBarKey(key, v) { editBar(b => { if (v === undefined || v === null || v === "") delete b[key]; else b[key] = v }) }

    function fresh(type) {
        if (type === "group") return { type: "group", bg: "bg/0.6", radius: 8, padding: [4, 4], modules: ["clock"] }
        if (type === "custom") return { type: "custom", exec: "echo hello", interval: 10 }
        if (type === "text") return { type: "text", text: "text" }
        return type
    }
    function addEntry(sec, type) {
        const n = (bar[sec] || []).length
        editBar(b => { b[sec] = b[sec] || []; b[sec].push(fresh(type)) })
        select(sec, n, -1)
    }
    function addToGroup(type) {
        if (type === "group") return
        const sec = selSec, gi = selGi, n = (norm(bar[sec][gi]).modules || []).length
        edit(d => { const e = norm(d[bIndex][sec][gi]); d[bIndex][sec][gi] = e; e.modules = e.modules || []; e.modules.push(fresh(type)) })
        select(sec, gi, n)
    }
    function move(delta) {
        let ni = -1
        edit(d => {
            const o = ownerList(d); if (!o) return
            const j = o.i + delta
            if (j < 0 || j >= o.list.length) return
            const x = o.list.splice(o.i, 1)[0]; o.list.splice(j, 0, x); ni = j
        })
        if (ni >= 0) { if (selMi < 0) selGi = ni; else selMi = ni }
    }
    function toSection(sec) {
        if (selMi >= 0 || sec === selSec) return
        let n = 0
        edit(d => { const b = d[bIndex]; const x = b[selSec].splice(selGi, 1)[0]; b[sec] = b[sec] || []; b[sec].push(x); n = b[sec].length - 1 })
        select(sec, n, -1)
    }
    // drag and drop from the preview: keys are "sec:gi:mi" (mi -1 = a section entry); to may be "section:<name>"
    function moveByKeys(fromK, toK, after) {
        const pk = k => { const a = k.split(":"); return { sec: a[0], gi: +a[1], mi: +a[2] } }
        const f = pk(fromK)
        edit(d => {
            const b = d[bIndex]
            if (!b[f.sec]) return
            let item, srcGroup = null
            if (f.mi < 0) item = b[f.sec].splice(f.gi, 1)[0]
            else { srcGroup = norm(b[f.sec][f.gi]); b[f.sec][f.gi] = srcGroup; item = srcGroup.modules.splice(f.mi, 1)[0] }
            if (item === undefined) return
            const isGroup = norm(item).type === "group"
            if (toK.startsWith("section:")) {
                const s = toK.split(":")[1]; b[s] = b[s] || []; b[s].push(item)
            } else {
                const t = pk(toK)
                let tg = t.gi, tm = t.mi
                // account for the removal when it came from before the target in the same list
                if (f.mi < 0 && f.sec === t.sec && f.gi < tg) tg--
                if (f.mi >= 0 && t.mi >= 0 && f.sec === t.sec && f.gi === t.gi && f.mi < tm) tm--
                b[t.sec] = b[t.sec] || []
                if (t.mi < 0 || isGroup) b[t.sec].splice(Math.max(0, tg) + (after ? 1 : 0), 0, item)
                else { const g = norm(b[t.sec][tg]); b[t.sec][tg] = g; g.modules.splice(Math.max(0, tm) + (after ? 1 : 0), 0, item) }
            }
            // a group emptied by the move goes away
            if (srcGroup && srcGroup.modules.length === 0) { const L = b[f.sec], i = L.indexOf(srcGroup); if (i >= 0) L.splice(i, 1) }
        })
        clearSel()
    }
    function removeSel() { edit(d => { const o = ownerList(d); if (o) o.list.splice(o.i, 1) }); clearSel() }
    function duplicate() { edit(d => { const o = ownerList(d); if (o) o.list.splice(o.i + 1, 0, JSON.parse(JSON.stringify(o.list[o.i]))) }) }
    function wrap() {
        if (selMi >= 0) return
        edit(d => { const l = d[bIndex][selSec]; l[selGi] = { type: "group", bg: "bg/0.6", radius: 8, padding: [4, 4], modules: [l[selGi]] } })
    }
    function unwrap() {
        if (selMi >= 0 || !selIsGroup) return
        edit(d => { const l = d[bIndex][selSec]; const g = norm(l[selGi]); l.splice(selGi, 1, ...(g.modules || [])) })
        clearSel()
    }
    function replaceSel(obj) {
        edit(d => {
            const o = ownerList(d); if (!o) return
            o.list[o.i] = obj
        })
    }

    // ---------------------------------------------------------------- catalogues
    readonly property var types: [
        { t: "workspaces", label: "Workspaces", icon: 0xf0570 }, { t: "title", label: "Window title", icon: 0xf05b1 },
        { t: "taskbar", label: "Taskbar", icon: 0xf0e2a }, { t: "clock", label: "Clock", icon: 0xf0150 },
        { t: "media", label: "Media", icon: 0xf075a }, { t: "cpu", label: "CPU", icon: 0xf0ee0 },
        { t: "ram", label: "RAM", icon: 0xf035b }, { t: "gpu", label: "GPU", icon: 0xf08ae },
        { t: "temp", label: "Temperature", icon: 0xf050f }, { t: "disk", label: "Disk", icon: 0xf02ca },
        { t: "network", label: "Network", icon: 0xf0200 }, { t: "volume", label: "Volume", icon: 0xf057e },
        { t: "mic", label: "Microphone", icon: 0xf036c }, { t: "battery", label: "Battery", icon: 0xf0079 },
        { t: "tray", label: "Tray", icon: 0xf003b }, { t: "notifications", label: "Notifications", icon: 0xf009a },
        { t: "weather", label: "Weather", icon: 0xf0590 }, { t: "uptime", label: "Uptime", icon: 0xf0954 },
        { t: "caffeine", label: "Caffeine", icon: 0xf0176 }, { t: "gamemode", label: "Game mode", icon: 0xf0297 },
        { t: "launcher", label: "Launcher", icon: 0xf003b }, { t: "power", label: "Power", icon: 0xf0425 },
        { t: "text", label: "Text", icon: 0xf0284 }, { t: "sep", label: "Separator", icon: 0xf01d8 },
        { t: "spacer", label: "Spacer", icon: 0xf0c0b }, { t: "custom", label: "Command", icon: 0xf018d },
        { t: "group", label: "Group", icon: 0xf0569 }
    ]
    function typeInfo(t) { return types.find(x => x.t === t) ?? { t: t, label: t, icon: 0xf0166 } }
    // what each module's format can use, and its own options (beyond the common style keys)
    readonly property var typeDocs: ({
        workspaces: "options: style (pills dots numbers roman kanji custom dwl), icons, glow, colors { active occupied empty urgent onActive }",
        title: "{title} {app} · maxLength", taskbar: "options: titles, titleWidth, iconSize, spacing, colors { active hover text }",
        clock: "{time} {date} · timeFormat, dateFormat (Qt: HH mm ss · h AP · ddd dddd · d MMM yyyy)",
        media: "{track} {title} {artist} {album} {player} · width, marquee, popup · states playing paused",
        cpu: "{usage} · level = usage", ram: "{percent} {used} {total} {free}", gpu: "{usage} {vramUsed} {vramTotal}",
        temp: "{temp} {cpu} {gpu} · sensor cpu|gpu", disk: "{percent} {used} {total} {free}",
        network: "{icon} {ifname} {ip} {down} {up} · states wifi ethernet disconnected",
        volume: "{icon} {volume} · step · state muted", mic: "{icon} {volume} · state muted",
        battery: "{icon} {capacity} {time} · states charging discharging full, warning ≤30 critical ≤15",
        tray: "options: iconSize, spacing", notifications: "{icon} {count} · states dnd unread none",
        weather: "{icon} {temp}{unit} {feels} {desc} {place}", uptime: "{uptime} {load}",
        caffeine: "always: true shows it while off too · states on off", gamemode: "always: true · states on off",
        launcher: "a button (click = launcher)", power: "a button (click = power menu)",
        text: "text: shown as is (markup ok)", sep: "format: the glyph", spacer: "size: px",
        custom: "exec (sh -c), interval s, stream true, refresh true, game true · prints text or JSON {text tooltip class percentage}"
    })
    readonly property var barFields: [
        { key: "position", label: "Edge", kind: "enum", options: ["top", "bottom", "left", "right"] },
        { key: "size", label: "Thickness", kind: "int", from: 14, to: 96, step: 2, def: 28 },
        { key: "length", label: "Length", kind: "enum", options: [{ label: "Full", value: 0 }, { label: "Fit", value: "auto" }, { label: "¾", value: 0.75 }, { label: "½", value: 0.5 }, { label: "⅓", value: 0.33 }], def: 0 },
        { key: "align", label: "Align (short bars)", kind: "enum", options: ["start", "center", "end"], def: "center" },
        { key: "margin", label: "Margin edge · inner · sides", kind: "triple", from: 0, to: 60 },
        { key: "padding", label: "Padding at the ends", kind: "int", from: 0, to: 60, def: 0 },
        { key: "spacing", label: "Between entries", kind: "int", from: 0, to: 40, def: 0 },
        { key: "bg", label: "Background", kind: "color" },
        { key: "radius", label: "Roundness", kind: "int", from: 0, to: 40, def: 0 },
        { key: "border", label: "Border", kind: "color" },
        { key: "borderWidth", label: "Border width", kind: "int", from: 0, to: 6, def: 0 },
        { key: "line", label: "Edge line", kind: "line" },
        { key: "fg", label: "Text colour", kind: "color" },
        { key: "fontSize", label: "Text size (0 = Appearance)", kind: "int", from: 0, to: 24, def: 0 },
        { key: "font", label: "Font", kind: "text", placeholder: "Appearance font" },
        { key: "exclusive", label: "Keep windows clear", kind: "bool", def: true },
        { key: "autohide", label: "Hide until the pointer hits the edge", kind: "bool", def: false },
        { key: "layer", label: "Layer", kind: "enum", options: ["top", "overlay", "bottom"], def: "top" },
        { key: "screen", label: "Screen (blank = all)", kind: "text", placeholder: "DP-1" }
    ]
    readonly property var styleFields: [
        { key: "bg", label: "Background", kind: "color" },
        { key: "radius", label: "Roundness", kind: "int", from: 0, to: 40, def: 0 },
        { key: "border", label: "Border", kind: "color" },
        { key: "borderWidth", label: "Border width", kind: "int", from: 0, to: 6, def: 0 },
        { key: "padding", label: "Padding before · after", kind: "pair", from: 0, to: 60 },
        { key: "gap", label: "Margin before · after", kind: "pair", from: 0, to: 60 },
        { key: "inset", label: "Inset edge · inner", kind: "pair", from: 0, to: 30 },
        { key: "capStart", label: "Start cap", kind: "caps" },
        { key: "capEnd", label: "End cap", kind: "caps" },
        { key: "capStartBg", label: "Behind the start cap", kind: "color" },
        { key: "capEndBg", label: "Behind the end cap", kind: "color" },
        { key: "line", label: "Indicator line", kind: "line" }
    ]
    readonly property var moduleFields: [
        { key: "format", label: "Format", kind: "text", mono: true, placeholder: "default" },
        { key: "formatAlt", label: "Alt format (click: alt)", kind: "text", mono: true },
        { key: "fg", label: "Text colour", kind: "color" },
        { key: "hoverBg", label: "Hover background", kind: "color" },
        { key: "hoverFg", label: "Hover text colour", kind: "color" },
        { key: "fontSize", label: "Text size (+2 / 14)", kind: "text", placeholder: "bar's" },
        { key: "bold", label: "Bold", kind: "bool", def: false },
        { key: "hoverGrow", label: "Grow on hover (px)", kind: "int", from: 0, to: 8, def: 0 },
        { key: "rotate", label: "Turn text (side bars)", kind: "bool", def: false }
    ].concat(styleFields).concat([
        { key: "click", label: "Click", kind: "text", mono: true, placeholder: "default" },
        { key: "rightClick", label: "Right click", kind: "text", mono: true, placeholder: "default" },
        { key: "middleClick", label: "Middle click", kind: "text", mono: true, placeholder: "default" },
        { key: "scrollUp", label: "Scroll up", kind: "text", mono: true, placeholder: "default" },
        { key: "scrollDown", label: "Scroll down", kind: "text", mono: true, placeholder: "default" },
        { key: "tooltip", label: "Tooltip", kind: "text", placeholder: "default" }
    ])
    readonly property var groupFields: styleFields.concat([
        { key: "spacing", label: "Between modules", kind: "int", from: 0, to: 30, def: 0 },
        { key: "click", label: "Click", kind: "text", mono: true },
        { key: "scrollUp", label: "Scroll up", kind: "text", mono: true },
        { key: "scrollDown", label: "Scroll down", kind: "text", mono: true }
    ])

    // fields grouped into plain-language folds (first ones open)
    function folds(fields, plan) {
        return plan.map(f => ({ title: f.title, hint: f.hint, open: !!f.open, fields: fields.filter(x => f.keys.includes(x.key)) }))
                   .filter(f => f.fields.length)
    }
    readonly property var modulePlan: [
        { title: "Text", hint: "what it says and how", open: true, keys: ["format", "formatAlt", "fg", "fontSize", "bold", "hoverFg", "hoverGrow", "rotate", "tooltip"] },
        { title: "Background & shape", hint: "colour, corners, border, powerline caps, underline", open: true,
          keys: ["bg", "hoverBg", "radius", "border", "borderWidth", "capStart", "capEnd", "capStartBg", "capEndBg", "line"] },
        { title: "Spacing", hint: "room inside, outside and across", keys: ["padding", "gap", "inset", "spacing"] },
        { title: "Clicks & scrolling", hint: "built-in actions or any shell command", keys: ["click", "rightClick", "middleClick", "scrollUp", "scrollDown"] }
    ]
    readonly property var barPlan: [
        { title: "Placement", hint: "edge, size, length, margins", open: true, keys: ["position", "size", "length", "align", "margin", "exclusive", "autohide", "layer", "screen"] },
        { title: "Look", hint: "background, corners, border, text", open: true, keys: ["bg", "radius", "border", "borderWidth", "line", "fg", "fontSize", "font"] },
        { title: "Spacing", hint: "room at the ends and between entries", keys: ["padding", "spacing"] }
    ]

    // ---------------------------------------------------------------- field rows
    component Field: Item {
        id: f
        required property var desc
        property var src: ({})
        property var setter: null
        readonly property var cur: src ? src[desc.key] : undefined
        // inline components don't see the file's ids: their own copies
        readonly property var tokens: ["accent", "accent2", "text", "dim", "faint", "bg", "card", "danger", "warn", "ok", "transparent"]
        readonly property var caps: [{ label: "none", value: "none" }, { label: "( ", value: "round" }, { label: "◀", value: "arrow" },
                                     { label: "▶|", value: "arrow-in" }, { label: "◢", value: "slant" }, { label: "◥", value: "slant-back" }]
        width: parent ? parent.width : 600
        height: Math.max(34, ctl.height)
        function put(v) { if (setter) setter(desc.key, v) }
        CpText {
            width: 190
            anchors.verticalCenter: parent.verticalCenter
            text: f.desc.label
            font.pixelSize: 11
            color: f.cur !== undefined ? Theme.text : Theme.textDim
            wrapMode: Text.WordWrap
        }
        Loader {
            id: ctl
            x: 200
            width: f.width - 200
            anchors.verticalCenter: parent.verticalCenter
            sourceComponent: ({ text: textC, color: colorC, int: intC, pair: pairC, triple: tripleC, bool: boolC, enum: enumC, caps: capsC, line: lineC })[f.desc.kind]
        }
        Component {
            id: textC
            CpField {
                width: ctl.width
                mono: !!f.desc.mono
                text: f.cur === undefined ? "" : typeof f.cur === "string" ? f.cur : JSON.stringify(f.cur)
                placeholder: (f.desc.placeholder || "") + "   ⏎ applies"
                onAccepted: t => f.put(t.length ? (/^-?\d+(\.\d+)?$/.test(t) && f.desc.key !== "format" && f.desc.key !== "text" ? +t : t) : undefined)
            }
        }
        Component {
            id: colorC
            Row {
                spacing: 8
                Rectangle {
                    width: 26; height: 26; radius: 13
                    anchors.verticalCenter: parent.verticalCenter
                    color: f.cur ? BarSpec.color(Array.isArray(f.cur) ? f.cur[0] : f.cur) : "transparent"
                    border.width: 1; border.color: Theme.alpha(Theme.text, 0.25)
                }
                CpField {
                    width: 150
                    mono: true
                    text: f.cur === undefined ? "" : Array.isArray(f.cur) ? f.cur.join(", ") : f.cur
                    placeholder: "token / #hex"
                    // "a, b" = a gradient
                    onAccepted: t => { const parts = t.split(",").map(s => s.trim()).filter(s => s); f.put(parts.length > 1 ? parts : parts[0]) }
                }
                Row {
                    spacing: 3
                    anchors.verticalCenter: parent.verticalCenter
                    Repeater {
                        model: f.tokens
                        Rectangle {
                            required property string modelData
                            width: 16; height: 16; radius: 8
                            color: BarSpec.color(modelData)
                            border.width: f.cur === modelData ? 2 : 1
                            border.color: f.cur === modelData ? Theme.accent : Theme.alpha(Theme.text, 0.25)
                            TapHandler { onTapped: f.put(f.cur === parent.modelData ? undefined : parent.modelData) }
                            HoverHandler { cursorShape: Qt.PointingHandCursor }
                        }
                    }
                }
            }
        }
        Component {
            id: intC
            CpStepper {
                value: f.cur !== undefined ? +f.cur : (f.desc.def ?? 0)
                from: f.desc.from ?? 0; to: f.desc.to ?? 100; step: f.desc.step ?? 1; suffix: "px"
                onChanged: v => f.put(v)
            }
        }
        Component {
            id: pairC
            Row {
                spacing: 18
                readonly property var v: Array.isArray(f.cur) ? f.cur : f.cur !== undefined ? [f.cur, f.cur] : null
                CpStepper { value: parent.v ? parent.v[0] : 0; from: f.desc.from; to: f.desc.to; onChanged: n => f.put([n, parent.v ? parent.v[1] : n]) }
                CpStepper { value: parent.v ? parent.v[1] : 0; from: f.desc.from; to: f.desc.to; onChanged: n => f.put([parent.v ? parent.v[0] : n, n]) }
                CpIconButton { visible: f.cur !== undefined; icon: 0xf0156; tip: "back to default"; onClicked: f.put(undefined) }
            }
        }
        Component {
            id: tripleC
            Row {
                spacing: 12
                readonly property var v: BarSpec.triple(f.cur)
                CpStepper { value: parent.v[0]; from: 0; to: f.desc.to; onChanged: n => f.put([n, parent.v[1], parent.v[2]]) }
                CpStepper { value: parent.v[1]; from: 0; to: f.desc.to; onChanged: n => f.put([parent.v[0], n, parent.v[2]]) }
                CpStepper { value: parent.v[2]; from: 0; to: f.desc.to; onChanged: n => f.put([parent.v[0], parent.v[1], n]) }
            }
        }
        Component {
            id: boolC
            CpSwitch { on: f.cur !== undefined ? !!f.cur : !!f.desc.def; onToggled: v => f.put(v) }
        }
        Component {
            id: enumC
            CpSegmented {
                width: Math.min(ctl.width, 70 * options.length)
                fontSize: 10
                options: f.desc.options.map(o => typeof o === "object" ? o : { label: o, value: o })
                current: f.cur !== undefined ? f.cur : f.desc.def
                onPicked: v => f.put(v)
            }
        }
        Component {
            id: capsC
            CpSegmented {
                width: Math.min(ctl.width, 330)
                fontSize: 11
                options: f.caps
                current: f.cur ?? "none"
                onPicked: v => f.put(v === "none" ? undefined : v)
            }
        }
        Component {
            id: lineC
            Row {
                spacing: 10
                readonly property var l: f.cur && typeof f.cur === "object" ? f.cur : null
                CpSwitch { anchors.verticalCenter: parent.verticalCenter; on: !!parent.l; onToggled: v => f.put(v ? { pos: "bottom", width: 2, color: "accent" } : undefined) }
                CpSegmented {
                    visible: !!parent.l
                    width: 220; fontSize: 10
                    options: ["top", "bottom", "left", "right"].map(o => ({ label: o, value: o }))
                    current: parent.l ? parent.l.pos ?? "bottom" : "bottom"
                    onPicked: v => f.put(Object.assign({}, parent.l, { pos: v }))
                }
                CpStepper { visible: !!parent.l; value: parent.l ? parent.l.width ?? 2 : 2; from: 1; to: 8; onChanged: n => f.put(Object.assign({}, parent.l, { width: n })) }
                CpField {
                    visible: !!parent.l
                    width: 100; mono: true
                    text: parent.l ? parent.l.color ?? "accent" : ""
                    onAccepted: t => f.put(Object.assign({}, parent.l, { color: t || "accent" }))
                }
            }
        }
    }

    // ---------------------------------------------------------------- bars + preview
    SpGroup {
        title: "Bars"
        hint: "One spec per bar; each shows on every screen unless you name one."
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: ed.bars.length
                CpChip {
                    required property int index
                    label: (index + 1) + " · " + (ed.bars[index].position || "top")
                    icon: 0xf04e9
                    on: index === ed.bIndex
                    onClicked: { ed.bi = index; ed.clearSel() }
                }
            }
            CpChip {
                label: "Add bar"; icon: 0xf0415
                onClicked: {
                    ed.edit(d => d.push({ position: ed.bars.some(b => (b.position || "top") === "top") ? "bottom" : "top", size: 28, bg: "bg/0.6", module: { padding: [8, 8], gap: [0, 0] }, center: ["clock"] }))
                    ed.bi = ed.bars.length; ed.clearSel()
                }
            }
            CpChip {
                visible: ed.bars.length > 1
                label: "Remove this bar"; icon: 0xf0a7a
                onClicked: { const i = ed.bIndex; ed.edit(d => d.splice(i, 1)); ed.bi = Math.max(0, i - 1); ed.clearSel() }
            }
        }
        BarPreview {
            width: parent.width
            height: ed.vertical ? 260 : Math.round(((ed.bar.size || 28) + 16) * 0.85) + 6
            // close to real size (sections squeeze a little on narrow windows)
            screenW: Math.round(width / 0.85)
            bar: ed.bar
            // click anything in it to select it below
            pickable: true
            selKey: ed.selSec ? ed.selSec + ":" + ed.selGi + ":" + ed.selMi : ""
            onPicked: (sec, gi, mi) => ed.select(sec, gi, mi)
            onMoved: (from, to, after) => ed.moveByKeys(from, to, after)
        }
        CpText { text: "Click a module (or an island's edge) to edit it · drag it to move it, also into or out of islands."; font.pixelSize: 10; color: Theme.textDim }
    }

    // ---------------------------------------------------------------- layout
    SpGroup {
        title: "Modules"
        hint: "Click to select · + adds one · a group is an island holding several modules."
        Repeater {
            model: [{ sec: "start", label: ed.vertical ? "Top" : "Left" }, { sec: "center", label: "Centre" }, { sec: "end", label: ed.vertical ? "Bottom" : "Right" }]
            Column {
                id: secCol
                required property var modelData
                width: parent.width
                spacing: 6
                readonly property var list: ed.bar[modelData.sec] || []
                Row {
                    spacing: 10
                    CpText { width: 60; text: secCol.modelData.label; font.pixelSize: 11; font.bold: true; color: Theme.textDim; anchors.verticalCenter: parent.verticalCenter }
                    Flow {
                        width: secCol.width - 70
                        spacing: 5
                        Repeater {
                            model: secCol.list.length
                            Row {
                                id: entryRow
                                required property int index
                                readonly property var e: ed.norm(secCol.list[index])
                                spacing: 3
                                CpChip {
                                    label: entryRow.e.type === "group" ? "group" : ed.typeInfo(entryRow.e.type).label
                                    icon: ed.typeInfo(entryRow.e.type).icon
                                    on: ed.selSec === secCol.modelData.sec && ed.selGi === entryRow.index && ed.selMi < 0
                                    onClicked: ed.select(secCol.modelData.sec, entryRow.index, -1)
                                }
                                Repeater {
                                    model: entryRow.e.type === "group" ? (entryRow.e.modules || []).length : 0
                                    CpChip {
                                        required property int index
                                        readonly property var me: ed.norm(entryRow.e.modules[index])
                                        label: "› " + ed.typeInfo(me.type).label
                                        on: ed.selSec === secCol.modelData.sec && ed.selGi === entryRow.index && ed.selMi === index
                                        onClicked: ed.select(secCol.modelData.sec, entryRow.index, index)
                                    }
                                }
                            }
                        }
                        CpChip {
                            label: "+"
                            on: ed.addingTo === secCol.modelData.sec
                            onClicked: ed.addingTo = ed.addingTo === secCol.modelData.sec ? "" : secCol.modelData.sec
                        }
                    }
                }
                Flow {
                    visible: ed.addingTo === secCol.modelData.sec
                    x: 70
                    width: secCol.width - 70
                    spacing: 5
                    Repeater {
                        model: ed.types
                        CpChip {
                            required property var modelData
                            label: modelData.label; icon: modelData.icon
                            onClicked: ed.addEntry(secCol.modelData.sec, modelData.t)
                        }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- inspector
    SpGroup {
        visible: !!ed.selected
        title: ed.selected ? (ed.selIsGroup ? "Group" : ed.typeInfo(ed.selected.type).label) + (ed.selMi >= 0 ? " (in a group)" : "") : ""
        hint: ed.selected && !ed.selIsGroup ? (ed.typeDocs[ed.selected.type] || "") : "Modules inside share this island; its style sits behind theirs."

        Flow {
            width: parent.width
            spacing: 6
            CpChip { label: ed.vertical ? "Up" : "Left"; icon: ed.vertical ? 0xf005d : 0xf004d; onClicked: ed.move(-1) }
            CpChip { label: ed.vertical ? "Down" : "Right"; icon: ed.vertical ? 0xf0045 : 0xf0054; onClicked: ed.move(1) }
            Repeater {
                model: ed.selMi < 0 ? ["start", "center", "end"].filter(s => s !== ed.selSec) : []
                CpChip { required property string modelData; label: "→ " + ed.secName(modelData); onClicked: ed.toSection(modelData) }
            }
            CpChip { visible: ed.selMi < 0 && !ed.selIsGroup; label: "Put in a group"; icon: 0xf0569; onClicked: ed.wrap() }
            CpChip { visible: ed.selIsGroup; label: "Ungroup"; icon: 0xf0e02; onClicked: ed.unwrap() }
            CpChip { label: "Duplicate"; icon: 0xf018f; onClicked: ed.duplicate() }
            CpChip { label: "Remove"; icon: 0xf0a7a; onClicked: ed.removeSel() }
        }

        // a group: add modules into it
        Flow {
            visible: ed.selIsGroup
            width: parent.width
            spacing: 5
            CpText { text: "Add inside:"; font.pixelSize: 11; color: Theme.textDim; height: 30; verticalAlignment: Text.AlignVCenter }
            Repeater {
                model: ed.selIsGroup ? ed.types.filter(t => t.t !== "group") : []
                CpChip { required property var modelData; label: modelData.label; onClicked: ed.addToGroup(modelData.t) }
            }
        }

        // type-specific bits the generic fields don't cover
        Field { visible: !!ed.selected && ed.selected.type === "text"; desc: ({ key: "text", label: "Text", kind: "text" }); src: ed.selected; setter: (k, v) => ed.setKey(k, v) }
        Field { visible: !!ed.selected && ed.selected.type === "custom"; desc: ({ key: "exec", label: "Command", kind: "text", mono: true }); src: ed.selected; setter: (k, v) => ed.setKey(k, v) }
        Field { visible: !!ed.selected && ed.selected.type === "custom"; desc: ({ key: "interval", label: "Every (s, 0 = once)", kind: "int", from: 0, to: 3600, def: 5 }); src: ed.selected; setter: (k, v) => ed.setKey(k, v) }
        Field { visible: !!ed.selected && ed.selected.type === "custom"; desc: ({ key: "stream", label: "Keep running (line = update)", kind: "bool" }); src: ed.selected; setter: (k, v) => ed.setKey(k, v) }
        Field { visible: !!ed.selected && ed.selected.type === "clock"; desc: ({ key: "timeFormat", label: "{time} format", kind: "text", mono: true, placeholder: Config.bar.clock }); src: ed.selected; setter: (k, v) => ed.setKey(k, v) }
        Field { visible: !!ed.selected && ed.selected.type === "clock"; desc: ({ key: "dateFormat", label: "{date} format", kind: "text", mono: true, placeholder: "ddd d MMM" }); src: ed.selected; setter: (k, v) => ed.setKey(k, v) }
        Field { visible: !!ed.selected && ed.selected.type === "workspaces"; desc: ({ key: "style", label: "Style", kind: "enum", options: ["pills", "dots", "numbers", "roman", "kanji", "dwl"] }); src: ed.selected; setter: (k, v) => ed.setKey(k, v) }
        Field { visible: !!ed.selected && ["tray", "taskbar"].includes(ed.selected.type); desc: ({ key: "iconSize", label: "Icon size", kind: "int", from: 10, to: 48, def: 16 }); src: ed.selected; setter: (k, v) => ed.setKey(k, v) }
        Field { visible: !!ed.selected && ed.selected.type === "taskbar"; desc: ({ key: "titles", label: "Show titles", kind: "bool" }); src: ed.selected; setter: (k, v) => ed.setKey(k, v) }
        Field { visible: !!ed.selected && ed.selected.type === "spacer"; desc: ({ key: "size", label: "Size", kind: "int", from: 0, to: 400, step: 4, def: 8 }); src: ed.selected; setter: (k, v) => ed.setKey(k, v) }

        Repeater {
            // re-folded only when the kind of selection changes, so open folds stay open while editing
            model: ed.selKind === "" ? [] : ed.folds(ed.selKind === "group" ? ed.groupFields : ed.moduleFields, ed.modulePlan)
            SpFold {
                required property var modelData
                title: modelData.title
                hint: modelData.hint
                open: modelData.open
                Repeater {
                    model: modelData.fields
                    Field {
                        required property var modelData
                        desc: modelData
                        src: ed.selected
                        setter: (k, v) => ed.setKey(k, v)
                        visible: modelData.key !== "rotate" || ed.vertical
                    }
                }
            }
        }

        // anything else: the raw spec
        CpText { text: "JSON (states, when, colors, icons …)"; font.pixelSize: 11; color: Theme.textDim; topPadding: 8 }
        Rectangle {
            width: parent.width
            height: Math.min(260, Math.max(90, json.contentHeight + 20))
            radius: 10
            color: Theme.alpha(Theme.text, 0.05)
            border.width: 1
            border.color: json.activeFocus ? Theme.alpha(Theme.accent, 0.6) : Theme.alpha(Theme.text, 0.08)
            Flickable {
                anchors { fill: parent; margins: 10 }
                contentHeight: json.contentHeight
                clip: true
                TextEdit {
                    id: json
                    width: parent.width
                    text: ed.selected ? JSON.stringify(ed.selected, null, 2) : ""
                    color: Theme.text
                    font.family: "JetBrainsMono Nerd Font"
                    font.pixelSize: 11
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    selectionColor: Theme.alpha(Theme.accent, 0.4)
                    property string error: ""
                    Keys.onPressed: e => { if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && (e.modifiers & Qt.ControlModifier)) { applyJson.apply(); e.accepted = true } }
                }
            }
        }
        Row {
            spacing: 10
            CpChip { id: applyJson; label: "Apply JSON (Ctrl+Enter)"; icon: 0xf012c
                function apply() { try { ed.replaceSel(JSON.parse(json.text)); json.error = "" } catch (e) { json.error = String(e.message || e) } }
                onClicked: apply() }
            CpText { text: json.error; color: Theme.danger; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
        }
    }

    SpGroup {
        title: "This bar"
        Repeater {
            model: ed.folds(ed.barFields, ed.barPlan)
            SpFold {
                required property var modelData
                title: modelData.title
                hint: modelData.hint
                open: modelData.open
                Repeater {
                    model: modelData.fields
                    Field {
                        required property var modelData
                        desc: modelData
                        src: ed.bar
                        setter: (k, v) => ed.setBarKey(k, v)
                        visible: modelData.key !== "align" || (ed.bar.length !== undefined && ed.bar.length !== 0)
                    }
                }
            }
        }
    }

    Flow {
        width: parent.width
        spacing: 6
        CpChip { label: "Edit settings.json"; icon: 0xf0dc9; onClicked: Quickshell.execDetached(["xdg-open", Quickshell.env("HOME") + "/.config/kusanagi/settings.json"]) }
        CpChip { label: "Format reference (docs/bar.md)"; icon: 0xf02d6; onClicked: Quickshell.execDetached(["xdg-open", Quickshell.env("HOME") + "/kusanagi/docs/bar.md"]) }
    }

}
