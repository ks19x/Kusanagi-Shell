// Launcher.qml — spotlight app launcher (Super+Space).
//   type          fuzzy-search apps (name, generic name, keywords); most-used float to the top
//   = 2*(3+4)     calculator — Enter copies the result
//   > command     run a shell command — Shift+Enter runs it in a terminal
//   : heart       emoji and symbols — Enter copies, Shift+Enter types it (wtype)
//   / notes       files in your home — Enter opens, Shift+Enter opens the folder
//   ? query       search the web (Config.launcher.searchEngine); also the last result of any search
// App searches also find Kusanagi itself: "lock", "bar settings", "replay", "preset zen"…
// ↑/↓ (or Ctrl+J/K, Tab) to move, Enter to launch, Esc to close. Loaded only while open.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Widgets
import QtQuick
import QtQuick.Effects

PanelWindow {
    id: root

    property bool showing: false
    function open() { showing = true }
    function close() { showing = false }
    function toggle() { showing = !showing }
    function searchFor(text) { actionsOf = null; search.text = text; search.cursorPosition = text.length }

    anchors { top: true; left: true; right: true; bottom: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "quickshell-launcher"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: showing ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    visible: showing || card.opacity > 0.01
    mask: Region { item: root.showing ? backdrop : null }

    onShowingChanged: {
        if (showing) { actionsOf = null; search.text = ""; sel = 0; search.forceActiveFocus() }
        else { symbols = []; files = [] }      // the symbol table only lives while it's used
    }

    // ---------- app actions (→ on an app: "New private window", "Big Picture"…) ----------
    property var actionsOf: null                                  // the app whose actions are listed
    function hasActions(item) { return !!item && item.kind === "app" && item.entry.actions && item.entry.actions.length > 0 }
    function openActions(item) {
        if (!hasActions(item)) return false
        actionsOf = item.entry; search.text = ""; sel = 0
        return true
    }
    function closeActions() { actionsOf = null; search.text = ""; sel = 0 }
    function actionsOfSelected() { openActions(results[sel]) }

    // ---------- layout ----------
    // Config.launcher.style: card · spotlight (a search pill, results once you type) · fullscreen · side
    readonly property string style: Config.launcher.style
    readonly property bool full: style === "fullscreen"
    readonly property bool side: style === "side"
    readonly property bool spot: style === "spotlight"
    readonly property real cardW: full ? width : side ? Math.min(Config.launcher.width, 460) : spot ? Math.min(Config.launcher.width, 600) : Config.launcher.width
    readonly property bool listShown: !(spot && !search.text && !actionsOf)
    readonly property int iconPx: full ? Math.max(56, Config.launcher.iconSize) : Config.launcher.iconSize
    property int sel: 0                                           // selected result (list or grid)
    readonly property bool grid: full || Config.launcher.layout === "grid"
    readonly property int rowH: Math.max(54, root.iconPx + 20)
    readonly property int cellW: Math.max(108, root.iconPx + 64)
    readonly property int cellH: root.iconPx + 58
    readonly property int cols: Math.max(1, Math.floor(((full ? Math.min(width - 120, 1400) : cardW) - 16) / cellW))
    readonly property int gridRows: full ? Math.max(2, Math.floor((height - 200) / cellH)) : Math.max(2, Math.ceil(Config.launcher.rows / 2))

    // ---------- usage counts (most-launched first) ----------
    FileView {
        id: usageFile
        path: Quickshell.env("HOME") + "/.config/kusanagi/launcher-usage.json"
        blockLoading: true
        printErrors: false
        onLoadFailed: err => { if (err === FileViewError.FileNotFound) writeAdapter() }
        JsonAdapter { id: usage; property var counts: ({}) }
    }
    function bump(id) {
        const c = Object.assign({}, usage.counts)
        c[id] = (c[id] || 0) + 1
        usage.counts = c
        usageFile.writeAdapter()
    }

    // ---------- search ----------
    readonly property string query: search.text
    readonly property string mode: query.startsWith("=") ? "calc" : query.startsWith(">") ? "run"
        : query.startsWith(":") ? "emoji" : query.startsWith("/") ? "files" : query.startsWith("?") ? "web" : "apps"

    // ---------- emoji & symbols (data/symbols.tsv, read the first time ":" is typed) ----------
    property var symbols: []
    FileView {
        id: symbolFile
        path: root.mode === "emoji" && !root.symbols.length ? Qt.resolvedUrl("data/symbols.tsv").toString().replace(/^file:\/\//, "") : ""
        printErrors: false
        onLoaded: root.symbols = text().split("\n").filter(l => l && l[0] !== "#").map(l => {
            const f = l.split("\t"); return { ch: f[0], name: f[1] || "", keys: f[2] || "" }
        })
    }
    function symbolResults(q) {
        const counts = usage.counts || {}
        const out = []
        for (const x of symbols) {
            const used = counts["sym:" + x.ch] || 0
            let sc = !q ? (used ? 1000 + used : 0) : Math.max(score(x.name, q), score(x.keys, q) * 0.6)
            if (q && sc > 0) sc += Math.min(30, Math.log2(used + 1) * 8)
            if (sc > 0) out.push({ kind: "emoji", name: x.name, comment: x.keys, glyph: x.ch, s: sc })
        }
        out.sort((a, b) => b.s - a.s)
        // nothing typed: your most used, then the first smileys
        return (!q && !out.length ? symbols.slice(0, 48).map(x => ({ kind: "emoji", name: x.name, comment: x.keys, glyph: x.ch })) : out.slice(0, 60))
    }

    // ---------- files (fd when installed, else find; a moment after you stop typing) ----------
    property var files: []
    Timer { id: fileDelay; interval: 220; onTriggered: { fileProc.running = false; fileProc.running = true } }
    onQueryChanged: {
        const f = query.startsWith("/")
        if (f && query.slice(1).trim().length >= 2) fileDelay.restart(); else files = []
    }
    // fd / fdfind (Debian's name) / plain find — paths relative to ~, folders end in /
    readonly property string findScript: [
        'q=$1; cd "$HOME" || exit 1',
        'if command -v fd >/dev/null 2>&1; then f=fd; elif command -v fdfind >/dev/null 2>&1; then f=fdfind; else f=""; fi',
        'if [ -n "$f" ]; then $f -i -F -H --max-results 40 -E .git -E node_modules -E .cache -E Trash -- "$q"',
        'else find . -maxdepth 6 \\( -name .cache -o -name .git -o -name node_modules -o -name Trash \\) -prune -o -iname "*$q*" -print 2>/dev/null | sed "s|^[.]/||" | head -40 | while IFS= read -r p; do if [ -d "$p" ]; then echo "$p/"; else echo "$p"; fi; done; fi'
    ].join("\n")
    Process {
        id: fileProc
        command: ["sh", "-c", root.findScript, "sh", root.query.slice(1).trim()]
        stdout: StdioCollector {
            onStreamFinished: {
                const home = Quickshell.env("HOME")
                root.files = text.split("\n").filter(l => l && l !== ".").map(l => {
                    const path = l.replace(/\/$/, ""), dir = l.endsWith("/"), i = path.lastIndexOf("/")
                    return { kind: "file", name: i >= 0 ? path.slice(i + 1) : path, comment: "~/" + (i >= 0 ? path.slice(0, i) : ""),
                             path: home + "/" + path, icon: dir ? 0xf024b : 0xf0214 }
                })
            }
        }
    }

    // ---------- the web ----------
    function webItem(q) {
        return { kind: "web", name: "Search the web for “" + q + "”", comment: Config.launcher.searchEngine.replace(/^https?:\/\/(www\.)?/, "").split("/")[0], q: q, icon: 0xf059f }
    }

    // ---------- Kusanagi's own commands ----------
    function ipc(target, fn, arg) { Quickshell.execDetached(["kusanagi", "msg", target, fn].concat(arg !== undefined ? [arg] : [])) }
    readonly property var commands: {
        const c = [
            { name: "Lock screen", keys: "lock away", icon: 0xf033e, run: () => ipc("lock", "lock") },
            { name: "Power menu", keys: "shut down power off reboot restart log out suspend sleep", icon: 0xf0425, run: () => ipc("power", "open") },
            { name: "Suspend", keys: "sleep", icon: 0xf04b2, run: () => Quickshell.execDetached(["sh", "-c", "loginctl suspend || systemctl suspend"]) },
            { name: "Control panel", keys: "quick settings tiles", icon: 0xf056e, run: () => ipc("panel", "home") },
            { name: "Notifications", keys: "inbox", icon: 0xf009a, run: () => ipc("notifs", "open") },
            { name: "Do not disturb", keys: "dnd silence notifications", icon: 0xf009b, run: () => { Notifs.dnd = !Notifs.dnd } },
            { name: "Caffeine", keys: "keep awake stay awake inhibit idle", icon: 0xf0176, run: () => Caffeine.toggle() },
            { name: "Game mode", keys: "gaming performance", icon: 0xf0297, run: () => GameMode.toggle() },
            { name: "Wallpaper", keys: "background picker", icon: 0xf0e09, run: () => ipc("wallpaper", "toggle") },
            { name: "Clipboard history", keys: "paste copy", icon: 0xf0147, run: () => ipc("clipboard", "toggle") },
            { name: "Screenshot", keys: "region capture print", icon: 0xf0e51, run: () => Quickshell.execDetached(["sh", "-c", "sleep 0.3; kusanagi screenshot region"]) },
            { name: "Screenshot (whole screen)", keys: "full capture print", icon: 0xf0e51, run: () => Quickshell.execDetached(["sh", "-c", "sleep 0.3; kusanagi screenshot full"]) },
            { name: "Colour picker", keys: "color pick eyedropper hex", icon: 0xf020a, run: () => Quickshell.execDetached(["sh", "-c", "sleep 0.3; kusanagi colorpick"]) },
            { name: Recorder.mode === "record" ? "Stop recording" : "Record the screen", keys: "record video capture", icon: 0xf044a, run: () => Recorder.record() },
            { name: Recorder.mode === "replay" ? "Stop the replay buffer" : "Start the replay buffer", keys: "replay instant clip", icon: 0xf0450, run: () => Recorder.replay() },
            { name: "Save replay clip", keys: "clip replay save", icon: 0xf0fd8, run: () => Recorder.save() },
            { name: "Check for updates", keys: "packages upgrade", icon: 0xf06b0, run: () => Updates.check() },
            { name: "Install updates", keys: "packages upgrade system", icon: 0xf06b0, run: () => Updates.upgrade() },
            { name: "Next preset", keys: "look theme cycle", icon: 0xf0e09, run: () => Presets.next() },
            { name: "Kusanagi setup", keys: "wizard welcome first", icon: 0xf0493, run: () => ipc("setup", "open") },
            { name: "Kusanagi doctor", keys: "check problems missing dependencies", icon: 0xf04d9,
              run: () => Quickshell.execDetached([Config.launcher.terminal, "-e", "sh", "-c", "kusanagi doctor; echo; echo 'press Enter'; read x"]) },
            { name: "Emoji & symbols", keys: "emoji symbol unicode character", icon: 0xf0785, fill: ":" },
            { name: "Search files", keys: "find files documents", icon: 0xf0214, fill: "/" }
        ]
        for (const p of SettingsPages.pages)
            c.push({ name: "Settings: " + p.name, keys: "settings " + p.keys, icon: p.icon, run: () => ipc("settings", "page", p.id) })
        for (const p of Presets.all)
            c.push({ name: "Preset: " + p.name, keys: "preset look theme " + (p.note || ""), icon: 0xf0e09, run: () => Presets.applyNamed(p.id || p.name) })
        return c
    }

    // 0 = no match; higher is better. prefix > word start > substring > in-order letters
    function score(text, q) {
        if (!text) return 0
        const t = text.toLowerCase()
        if (t === q) return 120
        if (t.startsWith(q)) return 100 - Math.min(20, t.length - q.length)
        const words = t.split(/[\s\-_.]+/)
        if (words.some(w => w.startsWith(q))) return 80
        const i = t.indexOf(q)
        if (i >= 0) return 60 - Math.min(20, i)
        // subsequence ("ffx" → firefox)
        let k = 0
        for (let j = 0; j < t.length && k < q.length; j++) if (t[j] === q[k]) k++
        return k === q.length ? 30 - Math.min(20, t.length - q.length) : 0
    }

    readonly property var apps: {
        const seen = {}
        return DesktopEntries.applications.values.filter(e => {
            if (e.noDisplay || seen[e.id]) return false
            seen[e.id] = true
            return true
        })
    }

    readonly property var results: {
        if (mode === "calc") {
            const r = calc(query.slice(1))
            return r === null ? [] : [{ kind: "calc", name: r, comment: query.slice(1).trim() + "  =", icon: 0xf00ec }]
        }
        if (mode === "run") {
            const cmd = query.slice(1).trim()
            return cmd ? [{ kind: "run", name: cmd, comment: "Enter to run  ·  Shift+Enter in a terminal", icon: 0xf018d }] : []
        }
        if (mode === "emoji") return symbolResults(query.slice(1).trim().toLowerCase())
        if (mode === "files") return files
        if (mode === "web") { const w = query.slice(1).trim(); return w ? [webItem(w)] : [] }
        if (actionsOf) {
            const q = query.trim().toLowerCase()
            return actionsOf.actions
                .filter(a => !q || score(a.name, q) > 0)
                .map(a => ({ kind: "action", action: a, app: actionsOf, name: a.name, comment: actionsOf.name }))
        }
        const q = query.trim().toLowerCase()
        const counts = usage.counts || {}
        const scored = []
        for (const e of apps) {
            const used = counts[e.id] || 0
            let s
            if (!q) s = used ? 1000 + used : 0
            else {
                s = Math.max(score(e.name, q), score(e.genericName, q) * 0.8,
                             Math.max(0, ...(e.keywords || []).map(k => score(k, q))) * 0.7,
                             score(e.id, q) * 0.6)
                if (s > 0 && Config.launcher.sortByUsage) s += Math.min(30, Math.log2(used + 1) * 8)
            }
            if (s > 0 || !q) scored.push({ kind: "app", entry: e, s: s, name: e.name })
        }
        // Kusanagi's commands: below apps that match as well (only when you type)
        if (q && Config.launcher.commands)
            for (const c of commands) {
                const s = Math.max(score(c.name, q), score(c.keys, q) * 0.7) * 0.85
                if (s >= 25) scored.push({ kind: "command", cmd: c, name: c.name, comment: "Kusanagi", icon: c.icon, s: s })
            }
        scored.sort((a, b) => b.s - a.s || a.name.localeCompare(b.name))
        const top = scored.slice(0, 40)
        if (q && Config.launcher.webSearch) top.push(webItem(query.trim()))
        return top
    }

    // tiny safe arithmetic: digits, operators, parentheses, a few Math functions
    function calc(expr) {
        const e = expr.trim().replace(/\^/g, "**").replace(/×/g, "*").replace(/÷/g, "/")
        if (!e || !/^[0-9+\-*/%.() ,a-z*]*$/i.test(e)) return null
        const allowed = ["sqrt", "sin", "cos", "tan", "log", "abs", "round", "floor", "ceil", "pi", "e", "pow", "min", "max"]
        const words = e.match(/[a-z]+/gi) || []
        if (!words.every(w => allowed.includes(w.toLowerCase()))) return null
        try {
            const v = Function("with (Math) { return (" + e.replace(/\bpi\b/gi, "PI").replace(/\be\b/g, "E") + ") }")()
            if (typeof v !== "number" || !isFinite(v)) return null
            return String(Math.round(v * 1e10) / 1e10)
        } catch (err) { return null }
    }

    function activate(item, inTerminal) {
        if (!item) return
        if (item.kind === "app") {
            const e = item.entry
            bump(e.id)
            if (e.runInTerminal) Quickshell.execDetached([Config.launcher.terminal, "-e"].concat(e.command))
            else e.execute()
        } else if (item.kind === "action") {
            bump(item.app.id)
            item.action.execute()
        } else if (item.kind === "calc") {
            Quickshell.execDetached(["wl-copy", item.name])
        } else if (item.kind === "emoji") {
            bump("sym:" + item.glyph)
            // typing needs our window gone first, so focus is back where you were
            if (inTerminal) Quickshell.execDetached(["sh", "-c", 'sleep 0.25; if command -v wtype >/dev/null; then wtype -- "$1"; else printf %s "$1" | wl-copy; fi', "sh", item.glyph])
            else Quickshell.execDetached(["wl-copy", item.glyph])
        } else if (item.kind === "file") {
            const target = inTerminal ? item.path.slice(0, item.path.lastIndexOf("/")) || "/" : item.path
            Quickshell.execDetached(["xdg-open", target])
        } else if (item.kind === "web") {
            Quickshell.execDetached(["xdg-open", Config.launcher.searchEngine.replace("%s", encodeURIComponent(item.q))])
        } else if (item.kind === "command") {
            if (item.cmd.fill) { searchFor(item.cmd.fill); return }
            close()
            item.cmd.run()
            return
        } else if (item.kind === "run") {
            Quickshell.execDetached(inTerminal
                ? [Config.launcher.terminal, "-e", "sh", "-c", item.name + "; exec $SHELL"]
                : ["sh", "-c", item.name])
        }
        close()
    }

    // ---------- view ----------
    Rectangle {
        id: backdrop
        anchors.fill: parent
        color: Theme.alpha("#000000", Config.look.backdrop * card.opacity)
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    RectangularShadow {

        visible: Config.look.shadows
        anchors.fill: card
        opacity: card.opacity
        scale: card.scale
        offset.y: 14
        blur: 40
        radius: card.radius
        color: Theme.alpha("#000000", 0.5)
    }

    Rectangle {
        id: card
        width: root.cardW
        height: root.full || root.side ? root.height
            : 64 + (!root.results.length || !root.listShown ? 0
            : root.grid ? Math.min(Math.ceil(root.results.length / root.cols), root.gridRows) * root.cellH + 16
            : Math.min(root.results.length, Config.launcher.rows) * root.rowH + 16)
        // side: slides in from the left edge; the others drop in where they sit
        x: root.side ? (root.showing ? 0 : -Math.round(width * 0.35)) : Math.round((root.width - width) / 2)
        y: root.full || root.side ? 0
           : Math.round(root.height * (Config.launcher.position === "center" ? 0.5 : 0.28) - (Config.launcher.position === "center" ? height / 2 : 0))
             + (root.showing ? 0 : -14)
        radius: root.full ? 0 : root.spot ? 30 : Config.look.radius
        topLeftRadius: root.side || root.full ? 0 : radius
        bottomLeftRadius: root.side || root.full ? 0 : radius
        color: Theme.alpha(Theme.bgPanel, root.full ? Math.min(0.88, Config.panel.opacity) : Config.panel.opacity)
        border.width: Theme.surfaceBorderWidth
        border.color: Theme.surfaceBorder
        clip: true

        opacity: root.showing ? 1 : 0
        scale: root.side ? 1 : root.showing ? 1 : root.full ? 1.04 : 0.95
        Behavior on opacity { NumberAnimation { duration: Config.ms(root.showing ? 220 : 140) } }
        Behavior on scale { NumberAnimation { duration: Config.ms(root.showing ? 380 : 160); easing.type: root.showing ? Easing.OutBack : Easing.InCubic; easing.overshoot: Config.bounce(1.3) } }
        Behavior on y { NumberAnimation { duration: Config.ms(root.showing ? 380 : 160); easing.type: Easing.OutQuint } }
        Behavior on x { enabled: root.side; NumberAnimation { duration: Config.ms(root.showing ? 340 : 160); easing.type: Easing.OutQuint } }
        Behavior on height { NumberAnimation { duration: Config.ms(220); easing.type: Easing.OutQuint } }

        MouseArea { anchors.fill: parent }

        // search field
        Item {
            id: field
            // fullscreen: a search pill centred near the top; elsewhere the card's first row
            width: root.full ? Math.min(560, parent.width - 80) : parent.width
            height: 64
            x: Math.round((parent.width - width) / 2)
            y: root.full ? 56 : root.side ? 18 : 0
            Rectangle {
                visible: root.full || root.side
                anchors { fill: parent; margins: 6 }
                radius: height / 2
                color: Theme.alpha(Theme.text, 0.07)
                border.width: 1
                border.color: Theme.alpha(Theme.text, 0.08)
            }

            CpIcon {
                x: 22
                anchors.verticalCenter: parent.verticalCenter
                cp: root.actionsOf ? 0xf0141 : ({ calc: 0xf00ec, run: 0xf018d, emoji: 0xf0785, files: 0xf0214, web: 0xf059f })[root.mode] ?? 0xf0349
                font.pixelSize: 20
                color: Theme.accent
            }
            TextInput {
                id: search
                anchors { left: parent.left; leftMargin: 56; right: parent.right; rightMargin: 22; verticalCenter: parent.verticalCenter }
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: root.spot ? 20 : 18
                selectionColor: Theme.alpha(Theme.accent, 0.4)
                clip: true
                onTextChanged: root.sel = 0
                Keys.onPressed: e => {
                    const n = root.results.length
                    const down = e.key === Qt.Key_Down || e.key === Qt.Key_Tab || (e.modifiers & Qt.ControlModifier && e.key === Qt.Key_J)
                    const up = e.key === Qt.Key_Up || e.key === Qt.Key_Backtab || (e.modifiers & Qt.ControlModifier && e.key === Qt.Key_K)
                    const cur = root.results[root.sel]
                    if (e.key === Qt.Key_Escape) { if (root.actionsOf) root.closeActions(); else root.close(); e.accepted = true }
                    else if (!root.grid && e.key === Qt.Key_Right && !root.actionsOf && root.openActions(cur)) e.accepted = true
                    else if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && (e.modifiers & Qt.AltModifier) && !root.actionsOf && root.openActions(cur)) e.accepted = true
                    else if (root.actionsOf && (e.key === Qt.Key_Left && !root.grid || (e.key === Qt.Key_Backspace && search.text === ""))) { root.closeActions(); e.accepted = true }
                    // grid: ←/→ step, ↑/↓ jump a row; list: ↑/↓ step
                    const step = root.grid && (e.key === Qt.Key_Down || e.key === Qt.Key_Up) ? root.cols : 1
                    if (root.grid && e.key === Qt.Key_Right && n) { root.sel = Math.min(n - 1, root.sel + 1); e.accepted = true }
                    else if (root.grid && e.key === Qt.Key_Left && n) { root.sel = Math.max(0, root.sel - 1); e.accepted = true }
                    else if (down && n) { root.sel = step > 1 ? Math.min(n - 1, root.sel + step) : (root.sel + 1) % n; e.accepted = true }
                    else if (up && n) { root.sel = step > 1 ? Math.max(0, root.sel - step) : (root.sel - 1 + n) % n; e.accepted = true }
                    else if (e.key === Qt.Key_PageDown && n) { root.sel = Math.min(n - 1, root.sel + Config.launcher.rows); e.accepted = true }
                    else if (e.key === Qt.Key_PageUp && n) { root.sel = Math.max(0, root.sel - Config.launcher.rows); e.accepted = true }
                    else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) {
                        root.activate(root.results[root.sel], e.modifiers & Qt.ShiftModifier)
                        e.accepted = true
                    }
                }
            }
            CpText {
                anchors { left: search.left; verticalCenter: parent.verticalCenter }
                visible: !search.text
                text: root.actionsOf ? root.actionsOf.name + " actions    ← back"
                    : "Search apps    = calc    > run    : emoji    / files    ? web"
                font.pixelSize: 15
                color: Theme.textDim
            }
        }

        Rectangle {
            visible: root.results.length > 0
            anchors { top: field.bottom; left: parent.left; right: parent.right; leftMargin: 16; rightMargin: 16 }
            height: 1
            color: Theme.alpha(Theme.text, 0.07)
        }

        ListView {
            id: list
            visible: !root.grid && root.listShown
            currentIndex: root.sel
            anchors { top: field.bottom; topMargin: 8; left: parent.left; right: parent.right; bottom: parent.bottom; margins: 8 }
            clip: true
            model: root.grid || !root.visible ? [] : root.results
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: Config.ms(160)
            highlightMoveVelocity: -1
            highlightResizeDuration: 0
            highlight: Rectangle {
                radius: Math.max(6, Config.look.radius - 6)
                color: Theme.alpha(Theme.accent, 0.16)
                border.width: 1
                border.color: Theme.alpha(Theme.accent, 0.35)
            }

            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool current: ListView.isCurrentItem
                width: list.width
                height: root.rowH

                // app icon, or a glyph for calc / run
                Item {
                    id: iconBox
                    x: 12
                    width: root.iconPx; height: root.iconPx
                    anchors.verticalCenter: parent.verticalCenter
                    // "" when the icon theme doesn't have it → glyph instead
                    readonly property var iconEntry: row.modelData.kind === "app" ? row.modelData.entry : row.modelData.kind === "action" ? row.modelData.app : null
                    readonly property string iconSrc: iconEntry && (row.modelData.kind === "action" && row.modelData.action.icon || iconEntry.icon)
                        ? Quickshell.iconPath(row.modelData.kind === "action" && row.modelData.action.icon ? row.modelData.action.icon : iconEntry.icon, true) : ""
                    IconImage {
                        anchors.fill: parent
                        visible: iconBox.iconSrc !== ""
                        source: iconBox.iconSrc
                        asynchronous: true
                    }
                    CpIcon {
                        anchors.centerIn: parent
                        visible: iconBox.iconSrc === "" && !row.modelData.glyph
                        cp: row.modelData.kind === "app" ? 0xf003b : (row.modelData.icon || 0)
                        font.pixelSize: 22
                        color: Theme.accent
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: !!row.modelData.glyph
                        text: row.modelData.glyph || ""
                        font.pixelSize: Math.round(root.iconPx * 0.75)
                        color: Theme.text
                    }
                }

                Column {
                    anchors { left: iconBox.right; leftMargin: 14; right: hint.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    spacing: 1
                    CpText {
                        width: parent.width
                        text: row.modelData.name
                        font.pixelSize: row.modelData.kind === "calc" ? 17 : 13
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    CpText {
                        width: parent.width
                        visible: Config.launcher.descriptions && text !== ""
                        text: row.modelData.kind === "app"
                            ? (row.modelData.entry.comment || row.modelData.entry.genericName || "")
                            : row.modelData.comment
                        font.pixelSize: 11
                        color: Theme.textDim
                        elide: Text.ElideRight
                    }
                }

                CpText {
                    id: hint
                    anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    text: row.modelData.kind === "calc" ? "copy  ↵" : row.modelData.kind === "emoji" ? "copy ↵  ·  type ⇧↵"
                        : row.modelData.kind === "file" ? "open ↵  ·  folder ⇧↵" : root.hasActions(row.modelData) ? "→ actions  ↵" : "↵"
                    font.pixelSize: 11
                    color: Theme.accent
                    opacity: row.current ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Config.ms(120) } }
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: root.sel = row.index
                    onClicked: root.activate(row.modelData, false)
                }
                CpIconButton {
                    visible: root.hasActions(row.modelData) && !root.actionsOf
                    anchors { right: parent.right; rightMargin: row.current ? 104 : 8; verticalCenter: parent.verticalCenter }
                    width: 26; height: 26
                    icon: 0xf0142
                    iconSize: 16
                    onClicked: root.openActions(row.modelData)
                }
            }
        }

        GridView {
            id: gridView
            visible: root.grid && root.listShown
            anchors { top: field.bottom; topMargin: 8; left: parent.left; right: parent.right; bottom: parent.bottom; margins: 8 }
            // centred: the side margins take whatever the columns leave
            anchors.leftMargin: Math.max(8, Math.floor((parent.width - root.cols * root.cellW) / 2))
            anchors.rightMargin: anchors.leftMargin
            cellWidth: root.cellW
            cellHeight: root.cellH
            clip: true
            model: root.grid && root.visible ? root.results : []
            currentIndex: root.sel
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: Config.ms(160)
            highlight: Rectangle {
                radius: Math.max(8, Config.look.radius - 6)
                color: Theme.alpha(Theme.accent, 0.16)
                border.width: 1
                border.color: Theme.alpha(Theme.accent, 0.35)
            }

            delegate: Item {
                id: tile
                required property var modelData
                required property int index
                width: root.cellW
                height: root.cellH
                readonly property var iconEntry: modelData.kind === "app" ? modelData.entry : modelData.kind === "action" ? modelData.app : null
                readonly property string iconSrc: iconEntry && iconEntry.icon ? Quickshell.iconPath(iconEntry.icon, true) : ""

                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    Item {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: root.iconPx; height: root.iconPx
                        scale: tileArea.containsMouse || GridView.isCurrentItem ? 1.08 : 1
                        Behavior on scale { NumberAnimation { duration: Config.ms(180); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(2) } }
                        IconImage { anchors.fill: parent; visible: tile.iconSrc !== ""; source: tile.iconSrc; asynchronous: true }
                        CpIcon {
                            anchors.centerIn: parent
                            visible: tile.iconSrc === "" && !tile.modelData.glyph
                            cp: tile.modelData.kind === "app" ? 0xf003b : (tile.modelData.icon || 0)
                            font.pixelSize: root.iconPx * 0.7
                            color: Theme.accent
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: !!tile.modelData.glyph
                            text: tile.modelData.glyph || ""
                            font.pixelSize: root.iconPx * 0.8
                            color: Theme.text
                        }
                    }
                    CpText {
                        width: root.cellW - 12
                        horizontalAlignment: Text.AlignHCenter
                        text: tile.modelData.name
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignTop
                    }
                }
                MouseArea {
                    id: tileArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onEntered: root.sel = tile.index
                    onClicked: root.activate(tile.modelData, false)
                }
                CpIconButton {
                    visible: root.hasActions(tile.modelData) && !root.actionsOf
                    anchors { right: parent.right; top: parent.top; margins: 4 }
                    width: 22; height: 22
                    icon: 0xf0142
                    iconSize: 14
                    onClicked: root.openActions(tile.modelData)
                }
            }
        }
    }
}
