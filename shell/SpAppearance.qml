// SpAppearance.qml — accent, font, corners, shadows, motion, surface opacity
import QtQuick

Column {
    spacing: 22

    SpGroup {
        title: "Palette"
        hint: "Colours for the shell. Wallpaper = picked from your wallpaper (and re-picked whenever it changes)."
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: [
                    { id: "wallpaper", name: "Wallpaper" }, { id: "catppuccin-mocha", name: "Mocha" }, { id: "catppuccin-latte", name: "Latte" },
                    { id: "gruvbox", name: "Gruvbox" }, { id: "nord", name: "Nord" }, { id: "rose-pine", name: "Rosé Pine" },
                    { id: "tokyo-night", name: "Tokyo Night" }, { id: "everforest", name: "Everforest" }, { id: "kanagawa", name: "Kanagawa" },
                    { id: "mono", name: "Mono" }
                ]
                // a little card in the palette's own colours
                Rectangle {
                    id: card
                    required property var modelData
                    readonly property var pal: Theme.presets[modelData.id] ?? null
                    readonly property bool on: Config.look.palette === modelData.id
                    width: 112; height: 58
                    radius: 10
                    color: pal ? pal.bg : Theme.bgPanel
                    border.width: on ? 2 : 1
                    border.color: on ? Theme.accent : Theme.alpha(Theme.text, 0.12)
                    scale: pa.containsMouse ? 1.04 : 1
                    Behavior on scale { NumberAnimation { duration: Config.ms(160); easing.type: Easing.OutBack } }
                    Row {
                        x: 10; y: 10
                        spacing: 4
                        Repeater {
                            model: card.pal ? [card.pal.accent, card.pal.accent2, card.pal.ok, card.pal.danger]
                                            : [Theme.accent, Theme.accent2, Theme.ok, Theme.danger]
                            Rectangle { required property var modelData; width: 12; height: 12; radius: 6; color: modelData }
                        }
                    }
                    CpText {
                        x: 10; anchors { bottom: parent.bottom; bottomMargin: 8 }
                        text: card.modelData.name
                        font.pixelSize: 11; font.bold: card.on
                        color: card.pal ? card.pal.text : Theme.text
                    }
                    MouseArea { id: pa; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Config.look.palette = card.modelData.id }
                }
            }
        }
    }

    SpGroup {
        title: "Accent colour"
        hint: "Comes from the palette — or pin one over it."

        Flow {
            width: parent.width
            spacing: 10
            CpChip {
                label: "From palette"
                icon: 0xf03d8
                on: Config.look.accent === ""
                onClicked: Config.look.accent = ""
            }
            Repeater {
                model: [
                    { name: "Rose", c: "#f5a3b5" }, { name: "Peach", c: "#f5b38a" }, { name: "Lemon", c: "#e9d27c" },
                    { name: "Mint", c: "#8fd6b0" }, { name: "Sky", c: "#8cc8f0" }, { name: "Lavender", c: "#b9a6f2" },
                    { name: "Crimson", c: "#e8505b" }, { name: "Snow", c: "#e8e8e8" }
                ]
                Rectangle {
                    required property var modelData
                    readonly property bool on: Config.look.accent.toLowerCase() === modelData.c
                    width: 28; height: 28; radius: 14
                    color: modelData.c
                    border.width: on ? 3 : 0
                    border.color: Theme.text
                    scale: area.containsMouse ? 1.12 : 1
                    Behavior on scale { NumberAnimation { duration: Config.ms(160); easing.type: Easing.OutBack } }
                    MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Config.look.accent = parent.modelData.c }
                }
            }
        }
        CpRow {
            width: parent.width
            label: "Custom colour"
            hint: "hex, e.g. #ff7a90"
            CpField {
                width: 160
                placeholder: "#rrggbb"
                text: Config.look.accent
                onAccepted: t => { if (/^#[0-9a-f]{6}$/i.test(t.trim())) Config.look.accent = t.trim() }
            }
        }
    }

    SpGroup {
        title: "Font"
        icon: 0xf0284
        hint: "For all of Kusanagi's text. Icons always come from the Nerd Font, so any font works."
        CpText { text: "Nerd Fonts"; font.pixelSize: 11; font.bold: true; color: Theme.textDim }
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: ["JetBrainsMono", "Iosevka", "CaskaydiaMono", "FiraCode", "ZedMono", "Lilex",
                        "VictorMono", "SpaceMono", "Mononoki", "BlexMono", "UbuntuMono", "0xProto", "Ubuntu", "UbuntuSans"]
                CpChip {
                    required property string modelData
                    readonly property string family: modelData + " Nerd Font"
                    visible: Qt.fontFamilies().includes(family)
                    label: modelData
                    fontFamily: family
                    on: Config.look.font === family
                    onClicked: Config.look.font = family
                }
            }
        }
        CpText { text: "Every font"; font.pixelSize: 11; font.bold: true; color: Theme.textDim; topPadding: 6 }
        SpFontPicker {
            width: parent.width
            current: Config.look.font
            onPicked: f => Config.look.font = f
        }
    }

    SpGroup {
        title: "Shape & depth"
        CpSlider {
            width: parent.width; height: 30
            icon: 0xf0830; label: "Corner radius"
            value: (Config.look.radius - 4) / 24; step: 1 / 24
            valueText: Config.look.radius + "px"
            onMoved: v => Config.look.radius = Math.round(4 + v * 24)
        }
        CpSlider {
            width: parent.width; height: 30
            icon: 0xf050e; label: "Surface opacity"
            value: (Config.panel.opacity - 0.5) / 0.5
            valueText: Math.round(Config.panel.opacity * 100) + "%"
            onMoved: v => Config.panel.opacity = Math.round((0.5 + v * 0.5) * 100) / 100
        }
        CpRow {
            width: parent.width
            label: "Outlines"
            hint: "a hairline around panels, cards and popups"
            CpSwitch { on: Config.look.borders; onToggled: v => Config.look.borders = v }
        }
        CpRow {
            width: parent.width
            label: "Outlines in the accent colour"
            CpSwitch { on: Config.look.borderAccent; onToggled: v => Config.look.borderAccent = v }
        }
        CpSlider {
            width: parent.width; height: 30
            icon: 0xf050e; label: "Backdrop"
            value: Config.look.backdrop / 0.5
            valueText: Math.round(Config.look.backdrop * 100) + "%"
            onMoved: v => Config.look.backdrop = Math.round(v * 0.5 * 100) / 100
        }
        CpText { width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 10; color: Theme.textDim
                 text: "Backdrop = how much the screen dims behind the panel, launcher and pickers." }
        CpRow {
            width: parent.width
            label: "Shadows"
            hint: "under the panel, launcher, popups and OSD"
            CpSwitch { on: Config.look.shadows; onToggled: v => Config.look.shadows = v }
        }
    }

    SpGroup {
        title: "Motion"
        icon: 0xf0e09
        hint: "How everything moves. Hover a card to see it; fine-tune with the sliders below."
        id: motionGroup
        // speed scales every duration (0 = no animation); bounce scales every overshoot
        readonly property var presets: [
            { id: "instant", name: "Instant", note: "no animation at all", speed: 0, bounce: 0 },
            { id: "snappy", name: "Snappy", note: "quick, barely a spring", speed: 0.6, bounce: 0.3 },
            { id: "smooth", name: "Smooth", note: "the default", speed: 1.0, bounce: 1.0 },
            { id: "bouncy", name: "Bouncy", note: "springs that overshoot", speed: 1.0, bounce: 1.8 },
            { id: "playful", name: "Playful", note: "slower and very springy", speed: 1.25, bounce: 2.0 },
            { id: "gentle", name: "Gentle", note: "calm, soft landings", speed: 1.4, bounce: 0.4 },
            { id: "cinematic", name: "Cinematic", note: "long glides, no bounce", speed: 1.8, bounce: 0 }
        ]
        readonly property string current: (presets.find(p => Math.abs(p.speed - Config.look.animSpeed) < 0.01 && Math.abs(p.bounce - Config.look.bounce) < 0.01) || { id: "custom" }).id
        Flow {
            width: parent.width
            spacing: 8
            Repeater {
                model: motionGroup.presets
                Rectangle {
                    id: mc
                    required property var modelData
                    readonly property bool on: motionGroup.current === modelData.id
                    width: 128; height: 104
                    radius: 12
                    color: mhov.hovered ? Theme.alpha(Theme.text, 0.08) : Theme.alpha(Theme.text, 0.04)
                    border.width: on ? 2 : 1
                    border.color: on ? Theme.accent : Theme.alpha(Theme.text, 0.07)
                    Behavior on border.color { ColorAnimation { duration: 160 } }
                    // the demo: a dot hopping across with this preset's own timing (only while hovered)
                    Rectangle {
                        id: track
                        x: 12; y: 14; width: parent.width - 24; height: 26; radius: 13
                        color: Theme.alpha(Theme.text, 0.05)
                        Rectangle {
                            id: dot
                            width: 18; height: 18; radius: 9
                            y: 4
                            color: Theme.accent
                            property bool atEnd: false
                            x: atEnd ? track.width - width - 4 : 4
                            Behavior on x {
                                enabled: mc.modelData.speed > 0
                                NumberAnimation { duration: 420 * mc.modelData.speed; easing.type: Easing.OutBack; easing.overshoot: 1.6 * mc.modelData.bounce }
                            }
                        }
                        Timer {
                            interval: Math.max(500, 420 * mc.modelData.speed + 380)
                            repeat: true
                            running: mhov.hovered
                            triggeredOnStart: true
                            onTriggered: dot.atEnd = !dot.atEnd
                        }
                    }
                    Column {
                        x: 12; y: 52
                        width: parent.width - 24
                        CpText { text: mc.modelData.name; font.pixelSize: 12; font.bold: true; color: mc.on ? Theme.accent : Theme.text }
                        CpText { width: parent.width; text: mc.modelData.note; font.pixelSize: 9; color: Theme.textDim; wrapMode: Text.WordWrap }
                    }
                    HoverHandler { id: mhov; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: { Config.look.animSpeed = mc.modelData.speed; Config.look.bounce = mc.modelData.bounce } }
                }
            }
        }
        CpSlider {
            width: parent.width; height: 30
            icon: 0xf04c5; label: "Speed"
            value: Config.look.animSpeed / 2
            valueText: Config.look.animSpeed === 0 ? "off" : Config.look.animSpeed <= 1 ? "×" + (1 / Config.look.animSpeed).toFixed(1) + " faster" : "×" + Config.look.animSpeed.toFixed(1) + " slower"
            step: 0.05
            onMoved: v => Config.look.animSpeed = Math.round(v * 2 * 20) / 20
        }
        CpSlider {
            width: parent.width; height: 30
            icon: 0xf0e09; label: "Bounciness"
            value: Config.look.bounce / 2
            valueText: Config.look.bounce === 0 ? "none" : Config.look.bounce.toFixed(1) + "×"
            step: 0.05
            onMoved: v => Config.look.bounce = Math.round(v * 2 * 10) / 10
        }
        CpRow {
            width: parent.width
            label: "Instant open"
            hint: "keep the launcher, panel, pickers and clipboard ready so they open on the very first frame"
            CpSwitch { on: Config.look.preload; onToggled: v => Config.look.preload = v }
        }
    }

    CpChip { label: "Reset appearance"; icon: 0xf0709; onClicked: { Config.reset("look"); Config.panel.opacity = 0.95 } }
}
