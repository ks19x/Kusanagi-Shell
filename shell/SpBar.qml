// SpBar.qml — bar style, size, clock, modules, scroll + media behaviour
import QtQuick

Column {
    spacing: 22

    SpGroup {
        title: "Style"
        CpSegmented {
            width: parent.width
            current: Config.bar.style
            options: [
                { label: "Islands", value: "islands" }, { label: "Solid", value: "solid" },
                { label: "Floating", value: "floating" }, { label: "Clear", value: "clear" }
            ]
            onPicked: v => Config.bar.style = v
        }
        CpRow {
            width: parent.width; label: "Position"
            CpSegmented {
                width: 220; current: Config.bar.position
                options: [{ label: "Top", value: "top" }, { label: "Bottom", value: "bottom" }]
                onPicked: v => Config.bar.position = v
            }
        }
        CpRow {
            width: parent.width; label: "Layout"; hint: "where the workspaces and the clock sit"
            CpSegmented {
                width: 340; current: Config.bar.layout
                options: [{ label: "Workspaces · Clock", value: "classic" }, { label: "Clock · Workspaces", value: "centered" }]
                fontSize: 10
                onPicked: v => Config.bar.layout = v
            }
        }
        CpRow {
            width: parent.width; label: "Height"
            CpStepper { value: Config.bar.height; from: 24; to: 40; step: 2; suffix: "px"; onChanged: v => Config.bar.height = v }
        }
        CpSlider {
            width: parent.width; height: 30
            icon: 0xf050e; label: "Background"
            value: Config.bar.opacity
            onMoved: v => Config.bar.opacity = Math.round(v * 100) / 100
        }
        CpSlider {
            width: parent.width; height: 30
            icon: 0xf0830; label: "Roundness"
            value: Config.bar.radius / 12; step: 1 / 12
            valueText: Config.bar.radius + "px"
            onMoved: v => Config.bar.radius = Math.round(v * 12)
        }
        CpRow { width: parent.width; label: "Outline the islands"; CpSwitch { on: Config.bar.outline; onToggled: v => Config.bar.outline = v } }
        CpRow { width: parent.width; label: "Accent labels"; hint: "CPU / RAM / GPU and the icons in your accent colour"; CpSwitch { on: Config.bar.accentLabels; onToggled: v => Config.bar.accentLabels = v } }
        CpRow {
            width: parent.width; label: "Text size"; hint: "icons scale along"
            CpStepper { value: Config.bar.fontSize; from: 9; to: 14; suffix: "px"; onChanged: v => Config.bar.fontSize = v }
        }
        CpRow {
            width: parent.width; label: "Tray icon size"
            CpStepper { value: Config.bar.trayIconSize; from: 10; to: 22; suffix: "px"; onChanged: v => Config.bar.trayIconSize = v }
        }
        CpRow { width: parent.width; label: "Grow on hover"; CpSwitch { on: Config.bar.hoverGrow; onToggled: v => Config.bar.hoverGrow = v } }
    }

    SpGroup {
        title: "Clock"
        CpSegmented {
            width: parent.width
            current: Config.bar.clock
            options: [
                { label: "20:31", value: "HH:mm" }, { label: "20:31:07", value: "HH:mm:ss" },
                { label: "8:31 PM", value: "h:mm AP" }, { label: "Fri 20:31", value: "ddd HH:mm" },
                { label: "2 Oct 20:31", value: "d MMM HH:mm" }
            ]
            onPicked: v => Config.bar.clock = v
        }
        CpRow { width: parent.width; label: "Bold"; CpSwitch { on: Config.bar.clockBold; onToggled: v => Config.bar.clockBold = v } }
        CpRow {
            width: parent.width
            label: "Custom format"
            hint: "Qt date format — HH mm ss · h AP · ddd dddd · d MMM yyyy"
            CpField { width: 200; text: Config.bar.clock; onAccepted: t => { if (t.trim()) Config.bar.clock = t.trim() } }
        }
    }

    SpGroup {
        title: "Modules"
        hint: "What sits on the right side of the bar."
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: [
                    { key: "title", label: "Window title", icon: 0xf05b1 },
                    { key: "media", label: "Media", icon: 0xf075a }, { key: "cpu", label: "CPU", icon: 0xf0ee0 },
                    { key: "ram", label: "RAM", icon: 0xf035b }, { key: "gpu", label: "GPU", icon: 0xf08ae },
                    { key: "temp", label: "Temperature", icon: 0xf050f }, { key: "volume", label: "Volume", icon: 0xf057e },
                    { key: "network", label: "Network", icon: 0xf0200 }, { key: "tray", label: "Tray", icon: 0xf003b },
                    { key: "power", label: "Power", icon: 0xf0425 }
                ]
                CpChip {
                    required property var modelData
                    label: modelData.label
                    icon: modelData.icon
                    on: Config.bar.modules[modelData.key]
                    onClicked: Config.bar.modules[modelData.key] = !on
                }
            }
        }
    }

    SpGroup {
        title: "Scrolling"
        CpRow {
            width: parent.width; label: "On the clock"
            CpSegmented {
                width: 300; current: Config.bar.scrollClock
                options: [{ label: "Volume", value: "volume" }, { label: "Workspaces", value: "workspaces" }, { label: "Nothing", value: "none" }]
                onPicked: v => Config.bar.scrollClock = v
            }
        }
        CpRow {
            width: parent.width; label: "On the right side"; hint: "media, stats, network, power"
            CpSegmented {
                width: 300; current: Config.bar.scrollStats
                options: [{ label: "Volume", value: "volume" }, { label: "Workspaces", value: "workspaces" }, { label: "Nothing", value: "none" }]
                onPicked: v => Config.bar.scrollStats = v
            }
        }
        CpRow {
            width: parent.width; label: "Volume step"
            CpStepper { value: Config.bar.volumeStep; from: 1; to: 20; suffix: "%"; onChanged: v => Config.bar.volumeStep = v }
        }
    }

    SpGroup {
        title: "Media"
        CpRow { width: parent.width; label: "Controls on hover"; hint: "cover, seek bar, prev / play / next"; CpSwitch { on: Config.bar.mediaPopup; onToggled: v => Config.bar.mediaPopup = v } }
        CpRow { width: parent.width; label: "Scroll long titles"; CpSwitch { on: Config.bar.marquee; onToggled: v => Config.bar.marquee = v } }
        CpRow {
            width: parent.width; label: "Window title width"
            CpStepper { value: Config.bar.titleWidth; from: 20; to: 120; step: 10; suffix: " ch"; onChanged: v => Config.bar.titleWidth = v }
        }
        CpRow {
            width: parent.width; label: "Title width"
            CpStepper { value: Config.bar.mediaWidth; from: 10; to: 50; step: 5; suffix: " ch"; onChanged: v => Config.bar.mediaWidth = v }
        }
    }

    CpChip { label: "Reset the bar"; icon: 0xf0709; onClicked: Config.reset("bar") }
}
