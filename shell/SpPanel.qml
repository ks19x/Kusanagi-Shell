// SpPanel.qml — control panel: size, default tab, tiles (pick + order), sections
import QtQuick

Column {
    id: page
    spacing: 22

    readonly property var tileNames: ({
        nightlight: "Night light", dnd: "Do not disturb", mic: "Microphone", gamemode: "Game mode",
        screenshot: "Screenshot", record: "Record", colorpicker: "Colour picker", wallpaper: "Wallpaper",
        clipboard: "Clipboard", lock: "Lock", settings: "Settings", launcher: "Apps"
    })

    SpGroup {
        title: "Layout"
        CpRow {
            width: parent.width; label: "Width"
            CpStepper { value: Config.panel.width; from: 480; to: 720; step: 20; suffix: "px"; onChanged: v => Config.panel.width = v }
        }
        CpRow {
            width: parent.width; label: "Opens on"
            CpSegmented {
                width: 360; current: Config.panel.defaultTab
                options: [{ label: "Home", value: 0 }, { label: "System", value: 1 }, { label: "Inbox", value: 2 }, { label: "Quick", value: 3 }]
                onPicked: v => Config.panel.defaultTab = v
            }
        }
        CpRow {
            width: parent.width; label: "Opening animation"
            CpSegmented {
                width: 360; current: Config.panel.morph
                options: [{ label: "Grow from clock", value: "island" }, { label: "Drop down", value: "drop" }, { label: "Fade", value: "fade" }]
                fontSize: 10
                onPicked: v => Config.panel.morph = v
            }
        }
        CpRow { width: parent.width; label: "Now playing card"; CpSwitch { on: Config.panel.showMedia; onToggled: v => Config.panel.showMedia = v } }
        CpRow { width: parent.width; label: "Quick stats row"; CpSwitch { on: Config.panel.showStats; onToggled: v => Config.panel.showStats = v } }
    }

    SpGroup {
        title: "Tiles"
        hint: "Tap to add or remove — they appear in the order you add them (4 per row)."
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: Config.allTiles
                CpChip {
                    required property string modelData
                    readonly property int pos: Config.panel.tiles.indexOf(modelData)
                    label: (pos >= 0 ? (pos + 1) + "  " : "") + page.tileNames[modelData]
                    on: pos >= 0
                    onClicked: Config.panel.tiles = on ? Config.panel.tiles.filter(t => t !== modelData)
                                                       : Config.panel.tiles.concat([modelData])
                }
            }
        }
    }

    CpChip { label: "Reset the control panel"; icon: 0xf0709; onClicked: Config.reset("panel") }
}
