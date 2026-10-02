// SpNotifications.qml — notification popups, do-not-disturb, OSD
import Quickshell
import QtQuick

Column {
    id: page
    spacing: 22

    function osdPreview() { Quickshell.execDetached(["kusanagi", "msg", "osd", "preview", "volume"]) }

    SpGroup {
        title: "Notifications"
        CpRow { width: parent.width; label: "Do not disturb"; hint: "popups stay quiet, history still fills"; CpSwitch { on: Notifs.dnd; onToggled: v => Notifs.dnd = v } }
        CpRow {
            width: parent.width; label: "Popups appear"
            CpSegmented {
                width: 300; current: Config.notifications.position
                options: [{ label: "Top left", value: "top-left" }, { label: "Top centre", value: "top-center" }, { label: "Top right", value: "top-right" }]
                onPicked: v => Config.notifications.position = v
            }
        }
        CpRow {
            width: parent.width; label: "Stay for"
            CpStepper { value: Config.notifications.timeout / 1000; from: 2; to: 15; suffix: " s"; onChanged: v => Config.notifications.timeout = v * 1000 }
        }
        CpRow {
            width: parent.width; label: "At most on screen"
            CpStepper { value: Config.notifications.max; from: 1; to: 8; onChanged: v => Config.notifications.max = v }
        }
        CpRow {
            width: parent.width; label: "Style"
            CpSegmented {
                width: 260; current: Config.notifications.style
                options: [{ label: "Comfortable", value: "comfortable" }, { label: "Compact", value: "compact" }]
                onPicked: v => Config.notifications.style = v
            }
        }
        CpRow { width: parent.width; label: "Countdown line"; CpSwitch { on: Config.notifications.progress; onToggled: v => Config.notifications.progress = v } }
        CpRow { width: parent.width; label: "Pictures"; hint: "album art, avatars"; CpSwitch { on: Config.notifications.images; onToggled: v => Config.notifications.images = v } }
        CpChip {
            label: "Send a test notification"; icon: 0xf009e
            onClicked: Quickshell.execDetached(["notify-send", "-a", "Kusanagi", "Hello from Kusanagi", "This is what notifications look like."])
        }
    }

    SpGroup {
        title: "On-screen display"
        hint: "The pill that shows up when volume, mic or game mode change."
        CpRow {
            width: parent.width; label: "Position"
            CpSegmented {
                width: 300; current: Config.osd.position
                options: [{ label: "Top", value: "top" }, { label: "Bottom", value: "bottom" }, { label: "Right edge", value: "right" }]
                onPicked: v => { Config.osd.position = v; page.osdPreview() }
            }
        }
        CpRow {
            width: parent.width; label: "Stay for"
            CpStepper {
                value: Config.osd.timeout / 100; from: 6; to: 40; step: 2
                suffix: "00 ms"
                onChanged: v => Config.osd.timeout = v * 100
            }
        }
        CpRow {
            width: parent.width; label: "Style"
            CpSegmented {
                width: 260; current: Config.osd.style
                options: [{ label: "Pill", value: "pill" }, { label: "Minimal", value: "minimal" }]
                onPicked: v => { Config.osd.style = v; page.osdPreview() }
            }
        }
        CpRow { width: parent.width; label: "Show the number"; CpSwitch { on: Config.osd.showValue; onToggled: v => { Config.osd.showValue = v; page.osdPreview() } } }
        CpRow { width: parent.width; label: "Volume"; CpSwitch { on: Config.osd.volume; onToggled: v => Config.osd.volume = v } }
        CpRow { width: parent.width; label: "Microphone"; CpSwitch { on: Config.osd.mic; onToggled: v => Config.osd.mic = v } }
        CpRow { width: parent.width; label: "Game mode"; CpSwitch { on: Config.osd.gamemode; onToggled: v => Config.osd.gamemode = v } }
        CpChip { label: "Preview"; icon: 0xf0208; onClicked: page.osdPreview() }
    }

    SpGroup {
        title: "Screenshots"
        hint: "The preview card after Print / Super+Shift+S."
        CpRow {
            width: parent.width; label: "Corner"
            CpSegmented {
                width: 380; current: Config.screenshot.position
                options: [{ label: "Bottom right", value: "bottom-right" }, { label: "Bottom left", value: "bottom-left" },
                          { label: "Top right", value: "top-right" }, { label: "Top left", value: "top-left" }]
                fontSize: 10
                onPicked: v => Config.screenshot.position = v
            }
        }
        CpRow {
            width: parent.width; label: "Stay for"
            CpStepper { value: Config.screenshot.timeout / 1000; from: 2; to: 20; suffix: " s"; onChanged: v => Config.screenshot.timeout = v * 1000 }
        }
        CpRow {
            width: parent.width; label: "Editor"; hint: "command — the file is added at the end"
            CpField { width: 200; text: Config.screenshot.editor; onAccepted: t => { if (t.trim()) Config.screenshot.editor = t.trim() } }
        }
    }
}
