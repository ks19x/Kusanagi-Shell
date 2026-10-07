// SpPanel.qml — control panel: size, default tab, tiles (pick + order), sections
import QtQuick

Column {
    id: page
    spacing: 22

    readonly property var tileNames: ({
        nightlight: "Night light", dnd: "Do not disturb", mic: "Microphone", gamemode: "Game mode",
        screenshot: "Screenshot", record: "Record", colorpicker: "Colour picker", wallpaper: "Wallpaper",
        clipboard: "Clipboard", lock: "Lock", settings: "Settings", launcher: "Apps", caffeine: "Caffeine"
    })

    // whole looks for the panel in one click (Home tab only; the other tabs keep their layout)
    readonly property var looks: [
        { value: "default", label: "Default", note: "cards, big clock, everything", set: { tileStyle: "cards", tileColumns: 4, sliderStyle: "thick", header: "big", tabs: true, order: ["tiles", "sliders", "media", "weather", "stats"] } },
        { value: "compact", label: "Compact", note: "pills, slim sliders first", set: { tileStyle: "pills", tileColumns: 2, sliderStyle: "slim", header: "compact", tabs: true, order: ["sliders", "tiles", "media", "stats"] } },
        { value: "icons", label: "Icon grid", note: "round buttons, no header", set: { tileStyle: "icons", tileColumns: 6, sliderStyle: "slim", header: "hidden", tabs: false, order: ["tiles", "sliders", "media"] } },
        { value: "dashboard", label: "Dashboard", note: "music and weather on top", set: { tileStyle: "cards", tileColumns: 3, sliderStyle: "thick", header: "big", tabs: true, order: ["media", "weather", "tiles", "sliders", "stats"] } }
    ]
    readonly property string currentLook: {
        const p = Config.panel
        const l = looks.find(x => x.set.tileStyle === p.tileStyle && x.set.tileColumns === p.tileColumns && x.set.sliderStyle === p.sliderStyle
                                   && x.set.header === p.header && JSON.stringify(x.set.order) === JSON.stringify(p.order))
        return l ? l.value : "custom"
    }
    readonly property var sectionNames: ({ tiles: "Quick tiles", sliders: "Volume + microphone", media: "Now playing", weather: "Weather", stats: "Quick stats" })
    function setOrder(l) { Config.panel.order = l }

    SpGroup {
        title: "Panel looks"
        icon: 0xf056e
        hint: "The Home tab in one click — then change any part of it below."
        SpStylePicker {
            kind: "panelLook"
            cardW: 128
            current: page.currentLook
            options: page.looks
            onPicked: v => { const l = page.looks.find(x => x.value === v); for (const k in l.set) Config.panel[k] = l.set[k] }
        }
    }

    SpGroup {
        title: "Home tab"
        CpText { text: "Tiles"; font.pixelSize: 11; font.bold: true; color: Theme.textDim }
        SpStylePicker {
            kind: "tiles"
            cardW: 128
            current: Config.panel.tileStyle
            options: [{ value: "cards", label: "Cards" }, { value: "pills", label: "Pills" }, { value: "icons", label: "Icons" }]
            onPicked: v => Config.panel.tileStyle = v
        }
        CpRow {
            width: parent.width; label: "Tiles per row"
            CpStepper { value: Config.panel.tileColumns; from: 1; to: 8; onChanged: v => Config.panel.tileColumns = v }
        }
        CpRow {
            width: parent.width; label: "Sliders"
            CpSegmented {
                width: 220; current: Config.panel.sliderStyle
                options: [{ label: "Thick", value: "thick" }, { label: "Slim", value: "slim" }]
                onPicked: v => Config.panel.sliderStyle = v
            }
        }
        CpRow {
            width: parent.width; label: "Header"; hint: "clock, date, you and the buttons at the top"
            CpSegmented {
                width: 280; current: Config.panel.header
                options: [{ label: "Big", value: "big" }, { label: "Compact", value: "compact" }, { label: "Hidden", value: "hidden" }]
                onPicked: v => Config.panel.header = v
            }
        }
        CpRow { width: parent.width; label: "Tab row"; hint: "off: the panel only shows Home (tabs stay on Super+N, IPC…)"; CpSwitch { on: Config.panel.tabs; onToggled: v => Config.panel.tabs = v } }

        CpText { text: "Sections, top to bottom"; font.pixelSize: 11; font.bold: true; color: Theme.textDim; topPadding: 6 }
        Repeater {
            model: ["tiles", "sliders", "media", "weather", "stats"].sort((a, b) => {
                const o = Config.panel.order, ia = o.indexOf(a), ib = o.indexOf(b)
                return (ia < 0 ? 99 : ia) - (ib < 0 ? 99 : ib)
            })
            Rectangle {
                id: secRow
                required property string modelData
                readonly property int at: Config.panel.order.indexOf(modelData)
                readonly property bool on: at >= 0
                width: parent.width; height: 42; radius: 10
                color: Theme.alpha(Theme.text, on ? 0.05 : 0.02)
                CpSwitch {
                    x: 10; anchors.verticalCenter: parent.verticalCenter
                    on: secRow.on
                    onToggled: v => { const o = Config.panel.order.slice(); if (v) o.push(secRow.modelData); else o.splice(o.indexOf(secRow.modelData), 1); page.setOrder(o) }
                }
                CpText { x: 66; anchors.verticalCenter: parent.verticalCenter; text: page.sectionNames[secRow.modelData]; font.pixelSize: 12; color: secRow.on ? Theme.text : Theme.textDim }
                Row {
                    visible: secRow.on
                    anchors { right: parent.right; rightMargin: 8; verticalCenter: parent.verticalCenter }
                    CpIconButton { icon: 0xf005d; enabled: secRow.at > 0; opacity: enabled ? 1 : 0.3
                        onClicked: { const o = Config.panel.order.slice(); o.splice(secRow.at, 1); o.splice(secRow.at - 1, 0, secRow.modelData); page.setOrder(o) } }
                    CpIconButton { icon: 0xf0045; enabled: secRow.at < Config.panel.order.length - 1; opacity: enabled ? 1 : 0.3
                        onClicked: { const o = Config.panel.order.slice(); o.splice(secRow.at, 1); o.splice(secRow.at + 1, 0, secRow.modelData); page.setOrder(o) } }
                }
            }
        }
    }

    SpGroup {
        title: "Layout"
        CpRow {
            width: parent.width; label: "Width"
            CpStepper { value: Config.panel.width; from: 480; to: 720; step: 20; suffix: "px"; onChanged: v => Config.panel.width = v }
        }
        CpRow {
            width: parent.width; label: "Opens on"
            CpSegmented {
                width: 400; current: Config.panel.defaultTab
                fontSize: 10
                options: [{ label: "Home", value: 0 }, { label: "Sound", value: 4 }, { label: "Net", value: 5 }, { label: "System", value: 1 },
                          { label: "Inbox", value: 2 }, { label: "Quick", value: 3 }]
                onPicked: v => Config.panel.defaultTab = v
            }
        }
        CpText { text: "Design"; font.pixelSize: 11; font.bold: true; color: Theme.textDim; topPadding: 4 }
        SpStylePicker {
            kind: "panel"
            current: Config.panel.morph
            options: [{ value: "island", label: "Grow from clock", note: "pours out of the bar's clock" },
                      { value: "drop", label: "Drop down", note: "full width, slides down" },
                      { value: "fade", label: "Float", note: "fades in, centred" },
                      { value: "sheet", label: "Side sheet", note: "full height at the right" }]
            onPicked: v => Config.panel.morph = v
        }
        CpRow {
            width: parent.width; label: "Weather location"; hint: "Empty = guessed from your connection"
            CpField { width: 220; text: Config.weather.location; placeholder: "e.g. Berlin"; onAccepted: t => Config.weather.location = t.trim() }
        }
        CpRow {
            width: parent.width; label: "Units"
            CpSegmented {
                width: 220; current: Config.weather.units
                options: [{ label: "°C · km/h", value: "metric" }, { label: "°F · mph", value: "imperial" }]
                onPicked: v => Config.weather.units = v
            }
        }
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
