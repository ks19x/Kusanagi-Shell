pragma Singleton
// Theme.qml
// Colours are read live from ~/.config/kusanagi/colors.json (lib/palette.py, run on every
// wallpaper change), so every panel re-themes instantly without restarting Quickshell.
import Quickshell
import Quickshell.Io
import QtQuick

Singleton {
    id: theme

    // Main properties you can tweak
    readonly property int radius: 5
    readonly property real tiltStrength: 8
    readonly property string fontFamily: Config.look.font     // Settings → Appearance
    property string iconFont: "JetBrainsMono Nerd Font"

    readonly property int animFast: 120
    readonly property int animMed: 220
    readonly property int animSlow: 380

    // Colours: the wallpaper's (theme.py) unless Config.look.palette picks a preset.
    // Presets only re-colour the shell; foot / borders keep following the wallpaper.
    readonly property var presets: ({
        "catppuccin-mocha": { bg: "#1e1e2e", card: "#313244", text: "#cdd6f4", dim: "#a6adc8", faint: "#585b70", accent: "#cba6f7", accent2: "#f5c2e7", danger: "#f38ba8", ok: "#a6e3a1" },
        "catppuccin-latte": { bg: "#eff1f5", card: "#ccd0da", text: "#4c4f69", dim: "#6c6f85", faint: "#9ca0b0", accent: "#8839ef", accent2: "#ea76cb", danger: "#d20f39", ok: "#40a02b" },
        "gruvbox":          { bg: "#1d2021", card: "#32302f", text: "#ebdbb2", dim: "#a89984", faint: "#504945", accent: "#fabd2f", accent2: "#fe8019", danger: "#fb4934", ok: "#b8bb26" },
        "nord":             { bg: "#2e3440", card: "#3b4252", text: "#eceff4", dim: "#d8dee9", faint: "#4c566a", accent: "#88c0d0", accent2: "#81a1c1", danger: "#bf616a", ok: "#a3be8c" },
        "rose-pine":        { bg: "#191724", card: "#26233a", text: "#e0def4", dim: "#908caa", faint: "#403d52", accent: "#ebbcba", accent2: "#c4a7e7", danger: "#eb6f92", ok: "#9ccfd8" },
        "tokyo-night":      { bg: "#1a1b26", card: "#24283b", text: "#c0caf5", dim: "#a9b1d6", faint: "#414868", accent: "#7aa2f7", accent2: "#bb9af7", danger: "#f7768e", ok: "#9ece6a" },
        "everforest":       { bg: "#272e33", card: "#374145", text: "#d3c6aa", dim: "#9da9a0", faint: "#4f5b58", accent: "#a7c080", accent2: "#83c092", danger: "#e67e80", ok: "#a7c080" },
        "kanagawa":         { bg: "#1f1f28", card: "#2a2a37", text: "#dcd7ba", dim: "#c8c093", faint: "#54546d", accent: "#7e9cd8", accent2: "#957fb8", danger: "#e82424", ok: "#98bb6c" },
        "mono":             { bg: "#0e0e0e", card: "#1c1c1c", text: "#e6e6e6", dim: "#9a9a9a", faint: "#3a3a3a", accent: "#e6e6e6", accent2: "#b0b0b0", danger: "#ff5f5f", ok: "#8fd18f" }
    })
    readonly property var pp: presets[Config.look.palette] ?? null

    readonly property color bg: alpha(bgPanel, 0.7)
    readonly property color text: pp ? pp.text : c.text
    readonly property color textDim: pp ? pp.dim : c.textDim
    readonly property color danger: pp ? pp.danger : c.danger
    // Settings → Appearance: a pinned accent beats the palette's
    readonly property color accent: Config.look.accent !== "" ? Config.look.accent : pp ? pp.accent : c.accent
    readonly property color accent2: pp ? pp.accent2 : c.accent2
    readonly property color border: pp ? pp.card : c.border
    readonly property color bgPanel: pp ? pp.bg : c.bgPanel
    readonly property color bgCard: pp ? pp.card : c.bgCard
    readonly property color borderAccent: pp ? pp.faint : c.borderAccent
    readonly property color textFaint: pp ? pp.faint : c.textFaint
    readonly property color ok: pp ? pp.ok : c.ok
    readonly property color trackBg: pp ? pp.card : c.trackBg

    // outline of panels / cards / popups (Settings → Appearance)
    readonly property int surfaceBorderWidth: Config.look.borders ? 1 : 0
    readonly property color surfaceBorder: Config.look.borderAccent ? alpha(accent, 0.55) : alpha(text, 0.08)

    function alpha(c, a) {
        if (typeof c === "string") c = Qt.lighter(c, 1.0)   // "#rrggbb" → color
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    FileView {
        path: Quickshell.env("HOME") + "/.config/kusanagi/colors.json"     // written by `kusanagi wallpaper`
        watchChanges: true
        onFileChanged: reload()
        blockLoading: true
        printErrors: false

        JsonAdapter {
            id: c
            // fallbacks until theme.py has run
            property color text: "#ffffff"
            property color textDim: "#c2c2c2"
            property color danger: "#ff003c"
            property color accent: "#ffffff"
            property color accent2: "#ffffff"
            property color border: "#151515"
            property color bgPanel: "#050505"
            property color bgCard: "#0d0d0d"
            property color borderAccent: "#2a2a2a"
            property color textFaint: "#4a4a4a"
            property color ok: "#00ff9c"
            property color trackBg: "#161616"
        }
    }
}
