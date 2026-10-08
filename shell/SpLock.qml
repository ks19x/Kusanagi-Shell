// SpLock.qml — lock screen engine (hyprlock / Kusanagi) and its look, locking when idle, password prompts
import Quickshell
import Quickshell.Io
import QtQuick

Column {
    id: page
    spacing: 22

    function ipc(what) { Quickshell.execDetached(["kusanagi", "msg", "lock", what]) }

    SpGroup {
        title: "Lock screen"
        hint: "Super+L. Try the Kusanagi one with Test first — it unlocks itself after 30 s (or press Esc)."
        CpSegmented {
            width: parent.width
            current: Config.lock.engine
            options: [{ label: "hyprlock", value: "hyprlock" }, { label: "Kusanagi", value: "kusanagi" }]
            onPicked: v => Config.lock.engine = v
        }
        Row {
            spacing: 8
            CpChip { label: "Test the Kusanagi lock"; icon: 0xf0fc6; onClicked: page.ipc("test") }
            CpChip { label: "Lock now"; icon: 0xf033e; onClicked: page.ipc("lock") }
        }
        CpText {
            width: parent.width
            wrapMode: Text.WordWrap
            font.pixelSize: 10
            color: Theme.textDim
            text: "If Kusanagi ever crashes while locked, the screen stays locked (that's the point). Switch to a TTY (Ctrl+Alt+F3), log in and run  WAYLAND_DISPLAY=wayland-0 swaylock  — it takes over and lets you unlock."
        }
    }

    SpGroup {
        id: idleGroup
        title: "When you're away"
        icon: 0xf04b2
        hint: "Kusanagi locks, turns the screens off and can suspend when you stop using the computer — no hypridle or swayidle needed. Videos, Caffeine and game mode keep it awake."
        CpRow { width: parent.width; label: "Handle idle"; hint: "Idle: " + Idle.status; CpSwitch { on: Config.idle.enabled; onToggled: v => Config.idle.enabled = v } }
        CpRow {
            width: parent.width; label: "Lock after"
            CpSegmented {
                width: 340; current: Config.idle.lock
                options: [{ label: "Never", value: 0 }, { label: "3 min", value: 3 }, { label: "5 min", value: 5 }, { label: "10 min", value: 10 }, { label: "20 min", value: 20 }]
                onPicked: v => Config.idle.lock = v
            }
        }
        CpRow {
            width: parent.width; label: "Screens off after"
            CpSegmented {
                width: 340; current: Config.idle.screenOff
                options: [{ label: "Never", value: 0 }, { label: "5 min", value: 5 }, { label: "10 min", value: 10 }, { label: "15 min", value: 15 }, { label: "30 min", value: 30 }]
                onPicked: v => Config.idle.screenOff = v
            }
        }
        CpRow {
            width: parent.width; label: "Suspend after"
            CpSegmented {
                width: 340; current: Config.idle.suspend
                options: [{ label: "Never", value: 0 }, { label: "30 min", value: 30 }, { label: "1 h", value: 60 }, { label: "2 h", value: 120 }]
                onPicked: v => Config.idle.suspend = v
            }
        }
        CpRow { width: parent.width; label: "Stay awake while media plays"; hint: "music or a video in any player"; CpSwitch { on: Config.idle.media; onToggled: v => Config.idle.media = v } }
        CpRow { width: parent.width; label: "Heads-up before locking"; hint: "a small pill 10 s before; move the mouse to stay"; CpSwitch { on: Config.idle.notify; onToggled: v => Config.idle.notify = v } }
        // another idle daemon would lock twice
        Process {
            running: true
            command: ["sh", "-c", "for p in hypridle swayidle xidlehook; do pgrep -x $p >/dev/null && echo $p; done"]
            stdout: StdioCollector { onStreamFinished: idleGroup.clash = text.trim() }
        }
        property string clash: ""
        CpText {
            visible: idleGroup.clash !== "" && Config.idle.enabled
            width: parent.width; wrapMode: Text.WordWrap
            font.pixelSize: 11; color: Theme.danger
            text: idleGroup.clash.split("\n").join(", ") + " is running too and will also lock / blank the screen — take it out of your compositor's autostart."
        }
    }

    SpGroup {
        title: "Design"
        icon: 0xf033e
        hint: "The Kusanagi lock screen. Try one with Test — it unlocks itself after 30 s."
        SpStylePicker {
            kind: "lock"
            cardW: 128
            current: Config.lock.style
            options: [{ value: "center", label: "Centered" }, { value: "card", label: "Card" }, { value: "split", label: "Split" },
                      { value: "minimal", label: "Minimal" }, { value: "stacked", label: "Stacked" }, { value: "terminal", label: "Terminal" }]
            onPicked: v => Config.lock.style = v
        }
    }

    SpGroup {
        title: "Power menu"
        icon: 0xf0425
        hint: "Lock, log out, suspend, reboot, shut down (Super+` / Ctrl+Alt+Del). Letters work too: L E S R P."
        SpStylePicker {
            kind: "power"
            cardW: 128
            current: Config.power.style
            options: [{ value: "row", label: "Row" }, { value: "tiles", label: "Tiles" }, { value: "list", label: "List" },
                      { value: "fullscreen", label: "Fullscreen" }, { value: "pill", label: "Pill" }]
            onPicked: v => { Config.power.style = v; Quickshell.execDetached(["kusanagi", "msg", "power", "open"]) }
        }
    }

    SpGroup {
        title: "Look"
        hint: "Applies to the Kusanagi lock screen."
        CpSlider {
            width: parent.width; height: 30
            icon: 0xf0e09; label: "Background blur"
            value: Config.lock.blur
            onMoved: v => Config.lock.blur = Math.round(v * 100) / 100
        }
        CpSlider {
            width: parent.width; height: 30
            icon: 0xf050e; label: "Darken"
            value: Config.lock.dim / 0.8
            valueText: Math.round(Config.lock.dim * 100) + "%"
            onMoved: v => Config.lock.dim = Math.round(v * 0.8 * 100) / 100
        }
        CpRow {
            width: parent.width; label: "Clock"
            CpSegmented {
                width: 300; current: Config.lock.clock
                options: [{ label: "20:31", value: "HH:mm" }, { label: "20:31:07", value: "HH:mm:ss" }, { label: "8:31 PM", value: "h:mm AP" }]
                onPicked: v => Config.lock.clock = v
            }
        }
        CpRow { width: parent.width; label: "Show your picture"; hint: "~/.face"; CpSwitch { on: Config.lock.avatar; onToggled: v => Config.lock.avatar = v } }
        CpRow { width: parent.width; label: "Media controls"; CpSwitch { on: Config.lock.media; onToggled: v => Config.lock.media = v } }
        CpRow {
            width: parent.width; label: "Greeting"; hint: "empty = your user name"
            CpField { width: 220; text: Config.lock.greeting; placeholder: Quickshell.env("USER"); onEdited: t => Config.lock.greeting = t }
        }
    }

    SpGroup {
        title: "Password prompts"
        icon: 0xf0483
        hint: "When an app needs admin rights (GParted, mounting a disk, pkexec …) Kusanagi asks for your password in its own style."
        CpRow {
            width: parent.width; label: "Kusanagi asks for passwords"
            hint: !Config.polkit.enabled ? "off — another polkit agent has to run" : Polkit.registered ? "active" : "waiting: another polkit agent is running (or no polkit daemon)"
            CpSwitch { on: Config.polkit.enabled; onToggled: v => Config.polkit.enabled = v }
        }
    }
}
