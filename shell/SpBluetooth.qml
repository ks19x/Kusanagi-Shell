// SpBluetooth.qml — Bluetooth: on / off, visible to others, your devices and new ones to pair (the same
// rows as the control panel tab), and what's missing when there's no adapter (bluez, bluetoothd).
import Quickshell
import Quickshell.Io
import QtQuick

Column {
    id: page
    spacing: 22

    Component.onCompleted: Bt.hold()
    Component.onDestruction: Bt.release()

    // no adapter: is it the daemon, the package, or the hardware?
    property string why: ""
    Process {
        running: Config.bluetooth.enabled && !Bt.available
        command: ["sh", "-c", "command -v bluetoothd >/dev/null || [ -x /usr/libexec/bluetooth/bluetoothd ] || [ -x /usr/lib/bluetooth/bluetoothd ] || { echo nobluez; exit; }; " +
                              "pgrep -x bluetoothd >/dev/null || { echo nodaemon; exit; }; echo noadapter"]
        stdout: StdioCollector { onStreamFinished: page.why = text.trim() }
    }

    SpGroup {
        title: "Bluetooth"
        hint: Bt.available ? (Bt.adapter.name || "Adapter") + "  ·  " + Bt.summary : ""
        CpRow {
            width: parent.width; label: "Use Bluetooth in Kusanagi"
            hint: "Off: no tab, tile or bar module — Kusanagi leaves Bluetooth alone."
            CpSwitch { on: Config.bluetooth.enabled; onToggled: v => Config.bluetooth.enabled = v }
        }
        CpText {
            visible: Config.bluetooth.enabled && !Bt.available
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: 12
            color: Theme.textDim
            text: page.why === "nobluez" ? "BlueZ (the Bluetooth service) isn't installed."
                : page.why === "nodaemon" ? "The Bluetooth service (bluetoothd) isn't running."
                : page.why === "noadapter" ? "No Bluetooth adapter found — this computer may not have one (a USB dongle works)."
                : "Looking…"
        }
        CpChip {
            visible: Config.bluetooth.enabled && !Bt.available && (page.why === "nobluez" || page.why === "nodaemon")
            on: true
            label: "Set up Bluetooth"
            icon: 0xf0493
            onClicked: Quickshell.execDetached(["kusanagi", "bluetooth", "setup"])
        }
        CpRow {
            visible: Bt.available
            width: parent.width; label: "On"
            hint: Bt.blocked ? "Blocked by a switch (rfkill) — turning it on unblocks it." : ""
            CpSwitch { on: Bt.on; onToggled: v => Bt.setOn(v) }
        }
        CpRow {
            visible: Bt.on
            width: parent.width; label: "Visible to other devices"
            hint: "So a phone or another computer can find this one."
            CpSwitch { on: Bt.on && Bt.adapter.discoverable; onToggled: v => Bt.adapter.discoverable = v }
        }
        CpRow {
            visible: Bt.available
            width: parent.width; label: "Look for devices while open"
            hint: "Scans only while the Bluetooth tab or this page is open."
            CpSwitch { on: Config.bluetooth.autoScan; onToggled: v => Config.bluetooth.autoScan = v }
        }
    }

    BtRequest { width: parent.width }

    SpGroup {
        visible: Bt.on
        title: "My devices"
        hint: "Click to connect or disconnect. Hover for Forget."
        Repeater {
            model: Bt.paired
            BtDeviceRow { required property var modelData; dev: modelData; width: parent.width }
        }
        CpText { visible: Bt.paired.length === 0; text: "Nothing paired yet."; font.pixelSize: 12; color: Theme.textDim }
    }

    SpGroup {
        visible: Bt.on
        title: "Nearby"
        hint: "Put the device in pairing mode, then click it."
        Repeater {
            model: Bt.nearby
            BtDeviceRow { required property var modelData; dev: modelData; width: parent.width }
        }
        Row {
            spacing: 8
            CpText { anchors.verticalCenter: parent.verticalCenter; visible: Bt.nearby.length === 0; text: Bt.scanning ? "Looking…" : "Nothing found."; font.pixelSize: 12; color: Theme.textDim }
            CpChip { label: Bt.scanning ? "Stop looking" : "Look for devices"; icon: 0xf0450; onClicked: Bt.scan(!Bt.scanning) }
        }
    }
}
