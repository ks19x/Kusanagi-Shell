pragma Singleton
// Presets.qml — whole looks in one click. A preset sets the look-related settings (bar, workspaces,
// motion, surfaces, OSD / notification / launcher style, sometimes palette + font) and leaves the rest
// (wallpaper, lock, game mode, modules you rely on…) alone, unless the preset is about them.
// Your own: "Save current look" snapshots those sections into ~/.config/kusanagi/presets.json.
import Quickshell
import Quickshell.Io
import QtQuick

Singleton {
    id: root

    // ---- built in ----
    readonly property var builtin: [
        {
            id: "kusanagi", name: "Kusanagi", note: "Translucent islands, pill workspaces, springy",
            look: { radius: 16, animSpeed: 1.0, bounce: 1.0, backdrop: 0.25, borders: true, borderAccent: false, shadows: true , palette: "wallpaper" },
            bar: { style: "islands", position: "top", height: 28, layout: "classic", accentLabels: false, opacity: 0.5, radius: 10,
                   fontSize: 11, outline: false, clockBold: true, hoverGrow: true,
                   modules: { title: false, media: true, cpu: true, ram: true, gpu: false, temp: false, volume: true, network: true, tray: true, power: true } },
            workspaces: { style: "pills", shown: 5, glow: true, activeColor: "accent" },
            panel: { morph: "island" }, osd: { style: "pill" }, notifications: { style: "comfortable" }, launcher: { layout: "list", iconSize: 32 }
        },
        {
            id: "minimal", name: "Minimal", note: "dwl-style flat bar: tag blocks, window title, status text",
            look: { radius: 6, animSpeed: 0.7, bounce: 0, backdrop: 0.15, borders: true, borderAccent: false, shadows: false , palette: "wallpaper" },
            bar: { style: "solid", position: "top", height: 24, layout: "classic", accentLabels: false, opacity: 1.0, radius: 0,
                   fontSize: 11, outline: false, clockBold: false, hoverGrow: false,
                   modules: { title: true, media: false, cpu: true, ram: true, gpu: false, temp: false, volume: true, network: false, tray: true, power: false } },
            workspaces: { style: "dwl", shown: 9, glow: false, activeColor: "accent" },
            panel: { morph: "fade" }, osd: { style: "minimal" }, notifications: { style: "compact" }, launcher: { layout: "list", iconSize: 24 }
        },
        {
            id: "glass", name: "Floating glass", note: "A see-through floating bar, accent outlines, bouncy",
            look: { radius: 22, animSpeed: 1.0, bounce: 1.4, backdrop: 0.3, borders: true, borderAccent: true, shadows: true , palette: "wallpaper" },
            bar: { style: "floating", position: "top", height: 32, layout: "classic", accentLabels: true, opacity: 0.35, radius: 16,
                   fontSize: 11, outline: false, clockBold: true, hoverGrow: true,
                   modules: { title: false, media: true, cpu: true, ram: true, gpu: true, temp: true, volume: true, network: true, tray: true, power: true } },
            workspaces: { style: "pills", shown: 5, glow: true, activeColor: "accent" },
            panel: { morph: "island" }, osd: { style: "pill" }, notifications: { style: "comfortable" }, launcher: { layout: "grid", iconSize: 40 }
        },
        {
            id: "zen", name: "Zen", note: "Bottom bar, clock left, dots centred, almost no stats, unhurried",
            look: { radius: 18, animSpeed: 1.4, bounce: 1.2, backdrop: 0.3, borders: true, borderAccent: false, shadows: true , palette: "wallpaper" },
            bar: { style: "islands", position: "bottom", height: 30, layout: "centered", accentLabels: false, opacity: 0.45, radius: 15,
                   fontSize: 11, outline: false, clockBold: true, hoverGrow: true,
                   modules: { title: false, media: true, cpu: false, ram: false, gpu: false, temp: false, volume: true, network: false, tray: true, power: true } },
            workspaces: { style: "dots", shown: 5, glow: true, activeColor: "accent" },
            panel: { morph: "island" }, osd: { style: "pill" }, notifications: { style: "comfortable" }, launcher: { layout: "grid", iconSize: 48 }
        },
        {
            id: "terminal", name: "Terminal", note: "Square everything, mono palette, roman tags, instant",
            look: { radius: 0, animSpeed: 0.7, bounce: 0, backdrop: 0.2, borders: true, borderAccent: true, shadows: false, palette: "mono" },
            bar: { style: "solid", position: "top", height: 22, layout: "classic", accentLabels: true, opacity: 0.92, radius: 0,
                   fontSize: 11, outline: false, clockBold: false, hoverGrow: false,
                   modules: { title: true, media: true, cpu: true, ram: true, gpu: true, temp: true, volume: true, network: true, tray: true, power: false } },
            workspaces: { style: "roman", shown: 5, glow: false, activeColor: "text" },
            panel: { morph: "fade" }, osd: { style: "minimal" }, notifications: { style: "compact" }, launcher: { layout: "list", iconSize: 20 }
        },
        {
            id: "neon", name: "Neon", note: "Tokyo Night, floating bar, glowing kanji tags, extra bouncy",
            look: { radius: 18, animSpeed: 1.0, bounce: 1.6, backdrop: 0.35, borders: true, borderAccent: true, shadows: true, palette: "tokyo-night" },
            bar: { style: "floating", position: "top", height: 30, layout: "classic", accentLabels: true, opacity: 0.55, radius: 15,
                   fontSize: 11, outline: false, clockBold: true, hoverGrow: true,
                   modules: { title: false, media: true, cpu: true, ram: true, gpu: true, temp: false, volume: true, network: true, tray: true, power: true } },
            workspaces: { style: "kanji", shown: 5, glow: true, activeColor: "accent2" },
            panel: { morph: "island" }, osd: { style: "pill" }, notifications: { style: "comfortable" }, launcher: { layout: "grid", iconSize: 44 }
        },
        {
            id: "paper", name: "Paper", note: "Light Latte palette, calm solid bar, soft and readable",
            look: { radius: 12, animSpeed: 1.0, bounce: 0.6, backdrop: 0.15, borders: true, borderAccent: false, shadows: true, palette: "catppuccin-latte" },
            bar: { style: "solid", position: "top", height: 28, layout: "classic", accentLabels: false, opacity: 0.94, radius: 0,
                   fontSize: 11, outline: false, clockBold: true, hoverGrow: false,
                   modules: { title: true, media: true, cpu: false, ram: false, gpu: false, temp: false, volume: true, network: true, tray: true, power: true } },
            workspaces: { style: "numbers", shown: 5, glow: false, activeColor: "accent" },
            panel: { morph: "drop" }, osd: { style: "pill" }, notifications: { style: "comfortable" }, launcher: { layout: "list", iconSize: 28 }
        },
        {
            id: "hud", name: "Gamer HUD", note: "Every stat on show (GPU, temps), dwl tags, instant, no frills",
            look: { radius: 8, animSpeed: 0.7, bounce: 0, backdrop: 0.2, borders: true, borderAccent: true, shadows: false, palette: "wallpaper" },
            bar: { style: "islands", position: "top", height: 26, layout: "classic", accentLabels: true, opacity: 0.7, radius: 6,
                   fontSize: 11, outline: true, clockBold: true, hoverGrow: false,
                   modules: { title: false, media: false, cpu: true, ram: true, gpu: true, temp: true, volume: true, network: true, tray: true, power: true } },
            workspaces: { style: "dwl", shown: 5, glow: false, activeColor: "accent" },
            panel: { morph: "fade" }, osd: { style: "minimal" }, notifications: { style: "compact" }, launcher: { layout: "list", iconSize: 24 }
        },
        {
            id: "nordic", name: "Nordic", note: "Nord palette, clear text bar, clock left + numbers centred",
            look: { radius: 14, animSpeed: 1.0, bounce: 0.8, backdrop: 0.25, borders: false, borderAccent: false, shadows: true, palette: "nord" },
            bar: { style: "clear", position: "top", height: 28, layout: "centered", accentLabels: true, opacity: 0.5, radius: 10,
                   fontSize: 12, outline: false, clockBold: true, hoverGrow: false,
                   modules: { title: false, media: true, cpu: true, ram: true, gpu: false, temp: false, volume: true, network: true, tray: true, power: true } },
            workspaces: { style: "numbers", shown: 5, glow: false, activeColor: "accent" },
            panel: { morph: "drop" }, osd: { style: "minimal" }, notifications: { style: "comfortable" }, launcher: { layout: "list", iconSize: 32 }
        },
        {
            id: "ink", name: "Ink", note: "Kanagawa colours, kanji tags, solid bottom bar, brushed calm",
            look: { radius: 10, animSpeed: 1.2, bounce: 0.5, backdrop: 0.3, borders: true, borderAccent: false, shadows: true, palette: "kanagawa" },
            bar: { style: "solid", position: "bottom", height: 28, layout: "classic", accentLabels: false, opacity: 0.9, radius: 0,
                   fontSize: 11, outline: false, clockBold: false, hoverGrow: false,
                   modules: { title: true, media: true, cpu: false, ram: false, gpu: false, temp: false, volume: true, network: false, tray: true, power: true } },
            workspaces: { style: "kanji", shown: 5, glow: false, activeColor: "accent" },
            panel: { morph: "island" }, osd: { style: "minimal" }, notifications: { style: "comfortable" }, launcher: { layout: "list", iconSize: 28 }
        },

    ]

    // the sections a preset may touch, and which keys of them make up a "look"
    readonly property var lookKeys: ({
        look: ["radius", "animSpeed", "bounce", "backdrop", "borders", "borderAccent", "shadows", "palette", "font", "accent"],
        bar: ["style", "position", "height", "layout", "accentLabels", "opacity", "radius", "fontSize", "outline", "clockBold", "hoverGrow", "clock", "modules"],
        workspaces: ["style", "shown", "glow", "activeColor", "icons"],
        panel: ["morph", "opacity"],
        osd: ["style", "position"],
        notifications: ["style", "position"],
        launcher: ["layout", "iconSize"]
    })

    // ---- your own ----
    FileView {
        id: file
        path: Quickshell.env("HOME") + "/.config/kusanagi/presets.json"
        blockLoading: true
        printErrors: false
        onLoadFailed: err => { if (err === FileViewError.FileNotFound) writeAdapter() }
        JsonAdapter { id: store; property var saved: [] }
    }
    readonly property var saved: store.saved || []

    property string lastApplied: ""

    function apply(p) {
        for (const section in lookKeys) {
            if (!p[section]) continue
            const target = Config[section], src = p[section]
            for (const k of lookKeys[section]) {
                if (!(k in src)) continue
                if (k === "modules") { for (const m in src.modules) target.modules[m] = src.modules[m] }
                else target[k] = src[k]
            }
        }
        lastApplied = p.id || p.name
    }

    readonly property var all: builtin.concat(saved)
    function applyNamed(name) {
        const n = name.toLowerCase()
        const p = all.find(x => (x.id || "").toLowerCase() === n || x.name.toLowerCase() === n)
        if (p) apply(p)
    }
    function next() {
        const i = all.findIndex(x => (x.id || x.name) === lastApplied)
        apply(all[(i + 1) % all.length])
    }

    function snapshot(name) {
        const p = { id: "user:" + name, name: name, note: "Saved " + Qt.formatDateTime(new Date(), "d MMM, HH:mm") }
        for (const section in lookKeys) {
            p[section] = {}
            for (const k of lookKeys[section]) {
                if (k === "modules") { p[section].modules = {}; for (const m of ["title", "media", "cpu", "ram", "gpu", "temp", "volume", "network", "tray", "power"]) p[section].modules[m] = Config.bar.modules[m] }
                else p[section][k] = Config[section][k]
            }
        }
        return p
    }

    function save(name) {
        name = (name || "").trim() || "My look " + (saved.length + 1)
        store.saved = saved.filter(s => s.name !== name).concat([snapshot(name)])
        file.writeAdapter()
        lastApplied = "user:" + name
    }

    function remove(name) {
        store.saved = saved.filter(s => s.name !== name)
        file.writeAdapter()
    }
}
