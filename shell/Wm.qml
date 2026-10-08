pragma Singleton
// Wm.qml — everything Kusanagi needs from the compositor, behind one interface:
// MangoWM, Hyprland (Lua config 0.55+ or classic hyprland.conf), niri, and a generic fallback.
//   kind               "mango" | "hyprland" | "niri" | "other"
//   workspaces         [{ n, active, occupied, urgent, ref }]  (1-based, padded to Config.workspaces.shown)
//   activeIndex        focused workspace number (for wallpaper parallax)
//   fullscreen         the focused window is fullscreen (game mode)
//   focusWorkspace(e) · scroll(steps) · setEffects(on) · quit() · monitorsCommand · configFile
//   layout             the compositor's own gaps / border (Config.windows overrides them live)
// niri is followed with ONE `niri msg -j event-stream` (no polling); the others use Quickshell's
// own Hyprland module / the ext-workspace protocol, so they cost no extra process at all.
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import Quickshell.Wayland
import Quickshell.WindowManager
import QtQuick

Singleton {
    id: root

    readonly property string kind: Quickshell.env("MANGO_INSTANCE_SIGNATURE") ? "mango"
        : Quickshell.env("HYPRLAND_INSTANCE_SIGNATURE") ? "hyprland"
        : Quickshell.env("NIRI_SOCKET") ? "niri" : "other"
    readonly property string name: ({ mango: "MangoWM", hyprland: "Hyprland", niri: "niri", other: "Wayland" })[kind]

    readonly property string home: Quickshell.env("HOME")
    readonly property string configHome: Quickshell.env("XDG_CONFIG_HOME") || home + "/.config"

    // Hyprland 0.55+ with a Lua config takes Lua dispatchers; classic hyprland.conf takes the old ones
    readonly property bool hyprLua: luaConfig.loaded
    FileView {
        id: luaConfig
        path: root.kind === "hyprland" ? root.configHome + "/hypr/hyprland.lua" : ""
        printErrors: false
    }

    function run(cmd) { Quickshell.execDetached(cmd) }
    function hypr(lua, classic) { run(["hyprctl", "dispatch", hyprLua ? lua : classic]) }

    // ---------------- workspaces ----------------
    readonly property var workspaces: {
        const shown = Config.workspaces.shown
        const list = []
        if (kind === "mango" || kind === "other") {
            // ext-workspace (Mango's tags): hidden = no windows
            for (const w of WindowManager.windowsets) {
                const n = parseInt(w.name)
                if (!(n >= 1 && n <= 9)) continue
                if (n > shown && !w.shouldDisplay && !w.active) continue
                list.push({ n: n, active: w.active, occupied: w.shouldDisplay, urgent: w.urgent, ref: w })
            }
        } else if (kind === "hyprland") {
            const byId = {}
            for (const w of Hyprland.workspaces.values) if (w.id > 0) byId[w.id] = w
            const focused = Hyprland.focusedWorkspace ? Hyprland.focusedWorkspace.id : -1
            const ids = Array.from({ length: shown }, (_, i) => i + 1)
            for (const id in byId) if (!ids.includes(+id)) ids.push(+id)
            for (const id of ids) {
                const w = byId[id]
                list.push({ n: id, active: id === focused, ref: null,
                            occupied: !!w && w.toplevels.values.length > 0, urgent: !!w && w.urgent })
            }
        } else if (kind === "niri") {
            // the focused output's workspaces, by index; padded so the bar keeps its shape
            const out = niri.focusedOutput
            const mine = niri.workspaces.filter(w => !out || w.output === out).sort((a, b) => a.idx - b.idx)
            for (const w of mine) {
                if (w.idx > shown && !niri.occupied[w.id] && !w.is_active) continue
                list.push({ n: w.idx, active: w.is_active, occupied: !!niri.occupied[w.id], urgent: !!w.is_urgent, ref: w })
            }
            for (let n = list.length + 1; n <= shown; n++) list.push({ n: n, active: false, occupied: false, urgent: false, ref: null })
        }
        return list.sort((a, b) => a.n - b.n)
    }
    readonly property int activeIndex: { const a = workspaces.find(w => w.active); return a ? a.n : 1 }

    function focusWorkspace(e) {
        if (kind === "mango" || kind === "other") { if (e.ref) e.ref.activate() }
        else if (kind === "hyprland") hypr(`hl.dsp.focus({workspace="${e.n}"})`, `workspace ${e.n}`)
        else if (kind === "niri") run(["niri", "msg", "action", "focus-workspace", String(e.n)])
    }
    function scroll(steps) {
        const prev = steps > 0
        if (kind === "mango") run(["mmsg", "dispatch", prev ? "viewtoleft,0" : "viewtoright,0"])
        else if (kind === "hyprland") hypr(`hl.dsp.focus({workspace="e${prev ? "-1" : "+1"}"})`, `workspace e${prev ? "-1" : "+1"}`)
        else if (kind === "niri") run(["niri", "msg", "action", prev ? "focus-workspace-up" : "focus-workspace-down"])
    }

    // ---------------- focused window ----------------
    // overlays (launcher, panel) take keyboard focus without being windows — keep the last real one
    property var lastToplevel: null
    Connections {
        target: ToplevelManager
        function onActiveToplevelChanged() { if (ToplevelManager.activeToplevel) root.lastToplevel = ToplevelManager.activeToplevel }
    }
    Component.onCompleted: { lastToplevel = ToplevelManager.activeToplevel; if (canSetLayout) relayout.start() }
    readonly property bool fullscreen: kind === "hyprland"
        ? (Hyprland.focusedWorkspace ? Hyprland.focusedWorkspace.hasFullscreen : false)
        : (lastToplevel ? lastToplevel.fullscreen : false)

    // ---------------- game-mode effects ----------------
    // your configured Mango values, so turning effects back on restores them exactly
    property var mangoDefaults: ({ blur: 1, shadows: 1, animations: 1, layer_animations: 1 })
    Process {
        running: root.kind === "mango"
        command: ["sh", "-c", "cat \"$1/mango/config.conf\" \"$1/mango/rice.conf\" 2>/dev/null | grep -E '^(blur|shadows|animations|layer_animations|gappih|gappoh|borderpx)='", "sh", root.configHome]
        stdout: StdioCollector {
            onStreamFinished: {
                const d = Object.assign({}, root.mangoDefaults)
                for (const l of text.split("\n")) { const m = l.match(/^(\w+)=(\d+)/); if (m) d[m[1]] = +m[2] }
                root.mangoDefaults = d
                root.layout = { gapsIn: d.gappih ?? root.layout.gapsIn, gapsOut: d.gappoh ?? root.layout.gapsOut, border: d.borderpx ?? root.layout.border }
            }
        }
    }
    readonly property bool canToggleEffects: kind === "mango" || kind === "hyprland"
    function setEffects(on) {
        if (kind === "mango") {
            const v = k => on ? mangoDefaults[k] : 0
            run(["sh", "-c", `mmsg dispatch setoption,blur,${v("blur")}; mmsg dispatch setoption,shadows,${v("shadows")};` +
                             ` mmsg dispatch setoption,animations,${v("animations")}; mmsg dispatch setoption,layer_animations,${v("layer_animations")}`])
        } else if (kind === "hyprland") {
            if (hyprLua) run(["hyprctl", "eval", `hl.config({ decoration = { blur = { enabled = ${on} }, shadow = { enabled = ${on} } }, animations = { enabled = ${on} } })`])
            else run(["hyprctl", "--batch", `keyword decoration:blur:enabled ${on ? 1 : 0}; keyword decoration:shadow:enabled ${on ? 1 : 0}; keyword animations:enabled ${on ? 1 : 0}`])
        }
        // niri: blur/animation switches live in its config file — left alone
    }

    // ---------------- gaps / borders ----------------
    // gapsIn = px BETWEEN two windows (Mango's gappih; Hyprland's gaps_in is per side, so half of it)
    readonly property bool canSetLayout: kind === "mango" || kind === "hyprland"
    property var layout: ({ gapsIn: 8, gapsOut: 8, border: 2 })   // the config's values: slider seed + restore
    Process {
        // Hyprland: ask it, before Kusanagi overrides anything (a gaps option prints "css": "4 4 4 4")
        running: root.kind === "hyprland" && !Config.windows.override
        command: ["sh", "-c", "for o in gaps_in gaps_out border_size; do hyprctl -j getoption general:$o | tr -d '\\n'; echo; done"]
        stdout: StdioCollector {
            onStreamFinished: {
                const n = text.split("\n").map(l => { const m = l.match(/"(?:css|custom|int)"\s*:\s*"?(\d+)/); return m ? +m[1] : NaN })
                if (n.length >= 3 && !n.slice(0, 3).some(isNaN)) root.layout = { gapsIn: n[0] * 2, gapsOut: n[1], border: n[2] }
            }
        }
    }
    function setLayout(l) {
        const i = Math.max(0, Math.round(l.gapsIn)), o = Math.max(0, Math.round(l.gapsOut)), b = Math.max(0, Math.round(l.border))
        if (kind === "mango") {
            run(["sh", "-c", `mmsg dispatch setoption,gappih,${i}; mmsg dispatch setoption,gappiv,${i}; mmsg dispatch setoption,gappoh,${o};` +
                             ` mmsg dispatch setoption,gappov,${o}; mmsg dispatch setoption,borderpx,${b}`])
        } else if (kind === "hyprland") {
            const half = Math.round(i / 2)
            if (hyprLua) run(["hyprctl", "eval", `hl.config({ general = { gaps_in = ${half}, gaps_out = ${o}, border_size = ${b} } })`])
            else run(["hyprctl", "--batch", `keyword general:gaps_in ${half}; keyword general:gaps_out ${o}; keyword general:border_size ${b}`])
        }
    }
    // Mango keeps the override across reload_config / restarts through this file, which its config
    // sources last (source-optional=~/.config/kusanagi/mango.conf); empty while the override is off
    FileView {
        id: mangoLayout
        path: root.kind === "mango" ? root.home + "/.config/kusanagi/mango.conf" : ""
        blockLoading: true
        printErrors: false
    }
    property bool layoutApplied: false
    function applyLayout() {
        const w = Config.windows
        if (kind === "mango") {
            const t = w.override ? `# written by Kusanagi (Settings → Display → Windows) — turn the override off there to drop it\n` +
                `gappih=${w.gapsIn}\ngappiv=${w.gapsIn}\ngappoh=${w.gapsOut}\ngappov=${w.gapsOut}\nborderpx=${w.border}\n` : ""
            if (mangoLayout.text() !== t) mangoLayout.setText(t)
        }
        if (w.override) { setLayout(w); layoutApplied = true }
        else if (layoutApplied) {
            layoutApplied = false
            if (kind === "hyprland") run(["hyprctl", "reload"])   // back to whatever the config says
            else setLayout(layout)
        }
    }
    Timer { id: relayout; interval: 30; onTriggered: root.applyLayout() }
    Connections {
        target: Config.windows
        function onOverrideChanged() { relayout.restart() }
        function onGapsInChanged() { relayout.restart() }
        function onGapsOutChanged() { relayout.restart() }
        function onBorderChanged() { relayout.restart() }
    }
    // a Hyprland config reload drops runtime values: put the override back
    Connections {
        target: root.kind === "hyprland" ? Hyprland : null
        function onRawEvent(ev) { if (ev.name === "configreloaded" && Config.windows.override) relayout.restart() }
    }

    // ---------------- session ----------------
    // screens off / on (idle). Wakes by itself on niri; elsewhere Idle.qml calls screens(true) on input.
    function screens(on) {
        if (kind === "mango") for (const s of Quickshell.screens) run(["mmsg", "dispatch", (on ? "wakeup_monitor," : "sleep_monitor,") + s.name])
        else if (kind === "hyprland") hypr(on ? "hl.dsp.dpms({ action = \"on\" })" : "hl.dsp.dpms({ action = \"off\" })", on ? "dpms on" : "dpms off")
        else if (kind === "niri") { if (!on) run(["niri", "msg", "action", "power-off-monitors"]) }
        else run(["sh", "-c", "command -v wlopm >/dev/null && wlopm " + (on ? "--on" : "--off") + " '*'"])
    }

    function quit() {
        if (kind === "mango") run(["mmsg", "dispatch", "quit"])
        else if (kind === "hyprland") hypr("hl.dsp.exit()", "exit")
        else if (kind === "niri") run(["niri", "msg", "action", "quit", "--skip-confirmation"])
        else run(["sh", "-c", "loginctl terminate-session \"$XDG_SESSION_ID\""])
    }

    // ---------------- monitors / config ----------------
    readonly property var monitorsCommand: kind === "mango" ? ["mmsg", "get", "all-monitors"]
        : kind === "hyprland" ? ["hyprctl", "monitors", "-j"]
        : kind === "niri" ? ["niri", "msg", "-j", "outputs"] : []
    // normalise to [{ name, w, h, hz, scale, x, y }]
    function parseMonitors(text) {
        try {
            const d = JSON.parse(text)
            if (kind === "mango") return (d.monitors || []).map(m => ({ name: m.name, w: m.width, h: m.height, hz: 0, scale: m.scale ?? 1, x: m.x ?? 0, y: m.y ?? 0 }))
            if (kind === "hyprland") return d.map(m => ({ name: m.name, w: m.width, h: m.height, hz: m.refreshRate ?? 0, scale: m.scale ?? 1, x: m.x ?? 0, y: m.y ?? 0 }))
            if (kind === "niri") return Object.keys(d).map(k => {
                const o = d[k], mode = o.modes && o.current_mode !== null ? o.modes[o.current_mode] : null, lg = o.logical || {}
                return { name: k, w: mode ? mode.width : lg.width, h: mode ? mode.height : lg.height,
                         hz: mode ? mode.refresh_rate / 1000 : 0, scale: lg.scale ?? 1, x: lg.x ?? 0, y: lg.y ?? 0 }
            })
        } catch (e) {}
        return []
    }
    readonly property string configFile: kind === "mango" ? configHome + "/mango/config.conf"
        : kind === "hyprland" ? configHome + (hyprLua ? "/hypr/hyprland.lua" : "/hypr/hyprland.conf")
        : kind === "niri" ? configHome + "/niri/config.kdl" : ""

    // ---------------- niri state (event stream) ----------------
    QtObject {
        id: niri
        property var workspaces: []
        property var windows: ({})          // id → workspace_id
        property var occupied: ({})         // workspace id → true
        readonly property string focusedOutput: { const f = workspaces.find(w => w.is_focused); return f ? f.output : "" }
        function recount() {
            const occ = {}
            for (const id in windows) if (windows[id] !== null) occ[windows[id]] = true
            occupied = occ
        }
    }
    Process {
        id: niriStream
        running: root.kind === "niri"
        command: ["niri", "msg", "-j", "event-stream"]
        stdout: SplitParser {
            onRead: line => {
                let ev
                try { ev = JSON.parse(line) } catch (e) { return }
                if (ev.WorkspacesChanged) niri.workspaces = ev.WorkspacesChanged.workspaces
                else if (ev.WorkspaceActivated) {
                    const a = ev.WorkspaceActivated, ws = niri.workspaces.slice()
                    const out = (ws.find(w => w.id === a.id) || {}).output
                    for (let i = 0; i < ws.length; i++) {
                        const w = Object.assign({}, ws[i])
                        if (w.output === out) w.is_active = w.id === a.id
                        if (a.focused) w.is_focused = w.id === a.id
                        ws[i] = w
                    }
                    niri.workspaces = ws
                } else if (ev.WorkspaceUrgencyChanged) {
                    niri.workspaces = niri.workspaces.map(w => w.id === ev.WorkspaceUrgencyChanged.id ? Object.assign({}, w, { is_urgent: ev.WorkspaceUrgencyChanged.urgent }) : w)
                } else if (ev.WindowsChanged) {
                    const m = {}
                    for (const w of ev.WindowsChanged.windows) m[w.id] = w.workspace_id
                    niri.windows = m; niri.recount()
                } else if (ev.WindowOpenedOrChanged) {
                    const w = ev.WindowOpenedOrChanged.window, m = Object.assign({}, niri.windows)
                    m[w.id] = w.workspace_id; niri.windows = m; niri.recount()
                } else if (ev.WindowClosed) {
                    const m = Object.assign({}, niri.windows)
                    delete m[ev.WindowClosed.id]; niri.windows = m; niri.recount()
                }
            }
        }
        // niri restarted its socket: follow again
        onExited: if (root.kind === "niri") niriRetry.start()
    }
    Timer { id: niriRetry; interval: 2000; onTriggered: niriStream.running = true }
}
