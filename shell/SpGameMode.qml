// SpGameMode.qml — what game mode switches off, and how it comes and goes
import QtQuick

Column {
    spacing: 22

    SpGroup {
        title: "Game mode"
        hint: "Super+G toggles it by hand. Auto turns it on when a window goes fullscreen and off when you leave it."
        Row {
            spacing: 14
            Rectangle {
                width: 46; height: 46; radius: 23
                color: GameMode.active ? Theme.accent : Theme.alpha(Theme.text, 0.08)
                Behavior on color { ColorAnimation { duration: Config.ms(200) } }
                CpIcon { anchors.centerIn: parent; cp: 0xf0297; font.pixelSize: 22; color: GameMode.active ? Theme.bgPanel : Theme.textDim }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                CpText { text: GameMode.active ? (GameMode.manual ? "On — switched on by hand" : "On — a game is fullscreen") : "Off"; font.pixelSize: 14; font.bold: true }
                CpText { text: GameMode.auto ? "auto: on" : "auto: off"; font.pixelSize: 11; color: Theme.textDim }
            }
        }
        CpRow { width: parent.width; label: "Turn on automatically for fullscreen"; CpSwitch { on: GameMode.auto; onToggled: v => GameMode.setAuto(v) } }
        CpChip { label: GameMode.active ? "Turn off now" : "Turn on now"; icon: 0xf0297; onClicked: GameMode.toggle() }
    }

    SpGroup {
        title: "While it's on"
        CpRow { width: parent.width; label: "Compositor effects off"; hint: "blur, shadows and animations — the biggest GPU win"; CpSwitch { on: Config.gamemode.effects; onToggled: v => Config.gamemode.effects = v } }
        CpRow { width: parent.width; label: "Performance CPU governor"; hint: "feral gamemode for as long as it's on"; CpSwitch { on: Config.gamemode.feral; onToggled: v => Config.gamemode.feral = v } }
        CpRow { width: parent.width; label: "Quiet shell"; hint: "bar stats stop polling, song marquee and wallpaper slideshow pause"; CpSwitch { on: Config.gamemode.quiet; onToggled: v => Config.gamemode.quiet = v } }
        CpRow { width: parent.width; label: "Hold notifications"; hint: "they still land in the Inbox; critical ones still pop up"; CpSwitch { on: Config.gamemode.dnd; onToggled: v => Config.gamemode.dnd = v } }
    }

    SpGroup {
        title: "Switching"
        CpRow {
            width: parent.width; label: "Grace before switching off"
            hint: "alt-tab out and back within this and nothing changes"
            CpSegmented {
                width: 300; current: Config.gamemode.grace
                options: [{ label: "None", value: 0 }, { label: "0.8 s", value: 800 }, { label: "2 s", value: 2000 }, { label: "5 s", value: 5000 }]
                onPicked: v => Config.gamemode.grace = v
            }
        }
        CpRow {
            width: parent.width; label: "Show the on/off pill"
            CpSegmented {
                width: 300; current: Config.gamemode.announce
                options: [{ label: "By hand", value: "manual" }, { label: "Always", value: "always" }, { label: "Never", value: "never" }]
                onPicked: v => Config.gamemode.announce = v
            }
        }
    }
}
