// Launcher.qml — spotlight app launcher (Super+Space).
//   type          fuzzy-search apps (name, generic name, keywords); most-used float to the top
//   = 2*(3+4)     calculator — Enter copies the result
//   > command     run a shell command — Shift+Enter runs it in a terminal
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

    onShowingChanged: if (showing) { actionsOf = null; search.text = ""; sel = 0; search.forceActiveFocus() }

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
    readonly property string mode: query.startsWith("=") ? "calc" : query.startsWith(">") ? "run" : "apps"

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
        scored.sort((a, b) => b.s - a.s || a.name.localeCompare(b.name))
        return scored.slice(0, 40)
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
                cp: root.actionsOf ? 0xf0141 : root.mode === "calc" ? 0xf00ec : root.mode === "run" ? 0xf018d : 0xf0349
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
                    : "Search apps    = calculate    > run a command"
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
                        visible: iconBox.iconSrc === ""
                        cp: row.modelData.kind === "app" ? 0xf003b : (row.modelData.icon || 0)
                        font.pixelSize: 22
                        color: Theme.accent
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
                    text: row.modelData.kind === "calc" ? "copy  ↵" : root.hasActions(row.modelData) ? "→ actions  ↵" : "↵"
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
                            visible: tile.iconSrc === ""
                            cp: tile.modelData.kind === "app" ? 0xf003b : (tile.modelData.icon || 0)
                            font.pixelSize: root.iconPx * 0.7
                            color: Theme.accent
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
