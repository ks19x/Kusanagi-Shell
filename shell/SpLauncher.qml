// SpLauncher.qml — app launcher and clipboard
import Quickshell
import QtQuick

Column {
    spacing: 22

    SpGroup {
        title: "App launcher"
        hint: "Super+Space · type to search · = calculator · > run a command"
        CpText { text: "Design"; font.pixelSize: 11; font.bold: true; color: Theme.textDim; topPadding: 4 }
        SpStylePicker {
            kind: "launcher"
            current: Config.launcher.style
            options: [{ value: "card", label: "Card", note: "a list under the search" },
                      { value: "spotlight", label: "Spotlight", note: "just a search, results as you type" },
                      { value: "fullscreen", label: "Fullscreen", note: "every app in a big grid" },
                      { value: "side", label: "Side panel", note: "slides in from the left" }]
            onPicked: v => Config.launcher.style = v
        }
        CpRow {
            visible: Config.launcher.style === "card" || Config.launcher.style === "spotlight"
            width: parent.width; label: "Position"
            CpSegmented {
                width: 240; current: Config.launcher.position
                options: [{ label: "Upper third", value: "upper" }, { label: "Centre", value: "center" }]
                onPicked: v => Config.launcher.position = v
            }
        }
        CpRow {
            visible: Config.launcher.style !== "fullscreen"
            width: parent.width; label: "Layout"
            CpSegmented {
                width: 220; current: Config.launcher.layout
                options: [{ label: "List", value: "list" }, { label: "Grid", value: "grid" }]
                onPicked: v => Config.launcher.layout = v
            }
        }
        CpRow {
            width: parent.width; label: "Icon size"
            CpStepper { value: Config.launcher.iconSize; from: 20; to: 64; step: 4; suffix: "px"; onChanged: v => Config.launcher.iconSize = v }
        }
        CpRow {
            width: parent.width; label: "Width"
            CpStepper { value: Config.launcher.width; from: 480; to: 900; step: 40; suffix: "px"; onChanged: v => Config.launcher.width = v }
        }
        CpRow {
            width: parent.width; label: "Visible results"
            CpStepper { value: Config.launcher.rows; from: 3; to: 12; onChanged: v => Config.launcher.rows = v }
        }
        CpRow { width: parent.width; label: "Show descriptions"; CpSwitch { on: Config.launcher.descriptions; onToggled: v => Config.launcher.descriptions = v } }
        CpRow { width: parent.width; label: "Most-used apps first"; CpSwitch { on: Config.launcher.sortByUsage; onToggled: v => Config.launcher.sortByUsage = v } }
        CpRow {
            width: parent.width; label: "Terminal"; hint: "for terminal apps and Shift+Enter on > commands"
            CpField { width: 160; text: Config.launcher.terminal; onAccepted: t => { if (t.trim()) Config.launcher.terminal = t.trim() } }
        }
        Row {
            spacing: 8
            CpChip { label: "Open launcher"; icon: 0xf003b; onClicked: Quickshell.execDetached(["kusanagi", "msg", "launcher", "open"]) }
            CpChip {
                label: "Forget app usage"; icon: 0xf02da
                onClicked: Quickshell.execDetached(["sh", "-c", "printf '{\"counts\": {}}\\n' > \"$HOME/.config/kusanagi/launcher-usage.json\""])
            }
        }
    }

    SpGroup {
        title: "Search"
        hint: "Start with  =  to calculate,  >  to run a command,  :  for emoji and symbols,  /  for files,  ?  for the web."
        CpRow { width: parent.width; label: "Kusanagi commands"; hint: "lock, settings pages, presets, recording… show up when you type"; CpSwitch { on: Config.launcher.commands; onToggled: v => Config.launcher.commands = v } }
        CpRow { width: parent.width; label: "Web search as the last result"; CpSwitch { on: Config.launcher.webSearch; onToggled: v => Config.launcher.webSearch = v } }
        CpRow {
            width: parent.width; label: "Search engine"
            CpSegmented {
                width: 360; current: Config.launcher.searchEngine
                options: [{ label: "DuckDuckGo", value: "https://duckduckgo.com/?q=%s" }, { label: "Google", value: "https://www.google.com/search?q=%s" },
                          { label: "Brave", value: "https://search.brave.com/search?q=%s" }, { label: "Startpage", value: "https://www.startpage.com/do/search?q=%s" }]
                onPicked: v => Config.launcher.searchEngine = v
            }
        }
        CpRow {
            width: parent.width; label: "…or your own"; hint: "%s is the search"
            CpField { width: 300; text: Config.launcher.searchEngine; onEdited: t => { if (t.includes("%s")) Config.launcher.searchEngine = t } }
        }
    }

    SpGroup {
        title: "Clipboard"
        hint: "Super+V · Enter copies · Delete removes"
        Row {
            spacing: 8
            CpChip { label: "Open history"; icon: 0xf0147; onClicked: Quickshell.execDetached(["kusanagi", "msg", "clipboard", "toggle"]) }
            CpChip { label: "Clear history"; icon: 0xf0a7a; onClicked: Quickshell.execDetached(["cliphist", "wipe"]) }
        }
    }
}
