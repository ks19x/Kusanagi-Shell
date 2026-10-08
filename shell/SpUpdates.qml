// SpUpdates.qml — package updates (Updates.qml → `kusanagi updates`), any distro + Flatpak
import Quickshell
import QtQuick

Column {
    id: page
    spacing: 22

    SpGroup {
        title: Updates.checking ? "Checking…" : !Updates.known ? "Not checked yet" : Updates.count ? Updates.count + (Updates.count === 1 ? " update waiting" : " updates waiting") : "Up to date"
        hint: Updates.checkedAt ? "Last check " + Qt.formatTime(new Date(Updates.checkedAt), "HH:mm") + ". Checking never needs your password and never installs anything." : "Checking never needs your password and never installs anything."
        Flow {
            width: parent.width
            spacing: 8
            CpChip { label: "Check now"; icon: 0xf0450; onClicked: Updates.check() }
            CpChip { visible: Updates.count > 0; label: "Install in a terminal"; icon: 0xf06b0; on: true; onClicked: Updates.upgrade() }
        }
        Column {
            visible: Updates.list.length > 0
            width: parent.width
            spacing: 4
            Repeater {
                model: Updates.list.slice(0, 40)
                Row {
                    required property var modelData
                    spacing: 10
                    CpText { text: modelData.name; font.pixelSize: 11; width: 360; elide: Text.ElideRight }
                    CpText { text: modelData.source; font.pixelSize: 11; color: Theme.textDim }
                }
            }
            CpText { visible: Updates.list.length > 40; text: "… and " + (Updates.list.length - 40) + " more"; font.pixelSize: 11; color: Theme.textDim }
        }
    }

    SpGroup {
        title: "Checking"
        hint: "Checks run only while the Updates module is on a bar (Settings → Bar) or notifications are on."
        CpRow {
            width: parent.width; label: "Check every"
            CpSegmented {
                width: 340; current: Config.updates.interval
                options: [{ label: "Never", value: 0 }, { label: "1 h", value: 1 }, { label: "3 h", value: 3 }, { label: "6 h", value: 6 }, { label: "12 h", value: 12 }]
                onPicked: v => Config.updates.interval = v
            }
        }
        CpRow { width: parent.width; label: "Notify me"; hint: "a notification when new updates show up"; CpSwitch { on: Config.updates.notify; onToggled: v => Config.updates.notify = v } }
    }
}
