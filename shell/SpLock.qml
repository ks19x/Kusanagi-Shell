// SpLock.qml — lock screen engine (hyprlock / Kusanagi) and its look
import Quickshell
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
}
