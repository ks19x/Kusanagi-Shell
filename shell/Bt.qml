pragma Singleton
// Bt.qml — Bluetooth for the tab, the tile, the bar module and Settings (Quickshell.Bluetooth over BlueZ).
// Named Bt because Quickshell.Bluetooth already exports a "Bluetooth" type. Nothing here touches BlueZ
// when Config.bluetooth.enabled is off, and nothing is shown without an adapter.
// Scanning runs only while a Bluetooth view is open (hold()/release()); so does the pairing agent
// (scripts/bt-agent.py), which turns "confirm 123456" / "type a PIN" requests into `request`.
import Quickshell
import Quickshell.Io
import Quickshell.Bluetooth
import QtQuick

Singleton {
    id: root

    readonly property var adapter: Config.bluetooth.enabled ? Bluetooth.defaultAdapter : null
    readonly property bool available: adapter !== null
    readonly property bool on: available && adapter.enabled
    readonly property bool blocked: available && adapter.state === BluetoothAdapterState.Blocked
    readonly property bool busy: available && (adapter.state === BluetoothAdapterState.Enabling || adapter.state === BluetoothAdapterState.Disabling)
    readonly property bool scanning: on && adapter.discovering

    readonly property var all: available ? adapter.devices.values : []
    function rank(d) { return d.connected ? 0 : d.state === BluetoothDeviceState.Connecting ? 1 : 2 }
    readonly property var paired: all.filter(d => d.paired || d.bonded)
        .sort((a, b) => rank(a) - rank(b) || a.name.localeCompare(b.name))
    // new devices worth showing: they have a real name (unnamed ones are mostly beacons)
    readonly property var nearby: all.filter(d => !d.paired && !d.bonded && d.deviceName !== "" && !d.blocked)
        .sort((a, b) => (b.pairing ? 1 : 0) - (a.pairing ? 1 : 0) || a.name.localeCompare(b.name))
    readonly property var connected: all.filter(d => d.connected)

    // one line for tiles, the bar and tooltips
    readonly property string summary: !available ? "No adapter" : blocked ? "Blocked" : !on ? "Off"
        : connected.length === 1 ? connected[0].name : connected.length > 1 ? connected.length + " devices" : "On"

    function toggle() { setOn(!on) }
    function setOn(v) {
        if (!available) return
        if (v && blocked) { unblock.running = true; return }
        adapter.enabled = v
    }
    Process { id: unblock; command: ["rfkill", "unblock", "bluetooth"]; onExited: if (root.available) root.adapter.enabled = true }

    // ---- open views: scanning + the agent ----
    property int holders: 0
    property bool weScan: false
    function hold() { holders++ }
    function release() { holders = Math.max(0, holders - 1) }
    readonly property bool wantScan: holders > 0 && on && Config.bluetooth.autoScan && !GameMode.quiet
    onWantScanChanged: syncScan()
    function syncScan() {
        if (!available) return
        if (wantScan && !adapter.discovering) { adapter.discovering = true; weScan = true }
        else if (!wantScan && weScan) { if (adapter.discovering) adapter.discovering = false; weScan = false }
    }
    function scan(v) { if (!on) return; adapter.discovering = v; weScan = v }

    // ---- devices ----
    function connectToggle(d) {
        if (d.connected || d.state === BluetoothDeviceState.Connecting) d.disconnect()
        else d.connect()
    }
    property var pairingDev: null
    function pair(d) {
        if (agent.ready) d.pair()
        else pairLater = d                          // the agent starts (its binding) and pairs once it's ready
        pairingDev = d
    }
    property var pairLater: null
    function cancelPair(d) { d.cancelPair(); if (pairingDev === d) pairingDev = null; request = null }
    function forget(d) { d.forget() }
    // once paired: trust it (reconnects on its own later) and connect
    Connections {
        target: root.pairingDev
        function onPairedChanged() {
            const d = root.pairingDev
            if (!d || !d.paired) return
            d.trusted = true
            d.connect()
            root.pairingDev = null
            root.request = null
        }
        function onPairingChanged() {
            const d = root.pairingDev
            if (d && !d.pairing && !d.paired) { root.pairingDev = null; root.request = null }   // failed / cancelled
        }
    }

    function battery(d) { return d && d.batteryAvailable ? Math.round(d.battery * 100) : -1 }
    function icon(d) {
        const i = d ? d.icon : ""
        if (/headset|headphone/.test(i)) return 0xf02cb
        if (/audio|speaker/.test(i)) return 0xf04c3
        if (/mouse/.test(i)) return 0xf037d
        if (/keyboard/.test(i)) return 0xf030c
        if (/gaming|joystick/.test(i)) return 0xf0297
        if (/phone/.test(i)) return 0xf011c
        if (/computer|laptop/.test(i)) return 0xf0322
        if (/watch/.test(i)) return 0xf0598
        if (/tablet/.test(i)) return 0xf04f7
        return 0xf00af
    }
    function status(d) {
        if (d.pairing) return "Pairing…"
        if (d.state === BluetoothDeviceState.Connecting) return "Connecting…"
        if (d.state === BluetoothDeviceState.Disconnecting) return "Disconnecting…"
        const b = battery(d)
        if (d.connected) return b >= 0 ? "Connected · " + b + "%" : "Connected"
        return d.paired || d.bonded ? "Not connected" : "Tap to pair"
    }

    // ---- pairing agent ----
    // request: { kind: confirm | pin | passkey | authorize | display, device, name, code } — answered by answer()
    property var request: null
    function nameOf(path) {
        const d = all.find(x => x.dbusPath === path)
        return d ? d.name : "A device"
    }
    function answer(ok, value) {
        if (!request) return
        const k = request.kind
        request = null
        if (k === "display") return
        agent.write(!ok ? "no\n" : k === "pin" ? "pin " + (value || "") + "\n" : k === "passkey" ? "passkey " + (value || "0") + "\n" : "yes\n")
    }
    Process {
        id: agent
        property bool ready: false
        command: ["python3", Quickshell.shellDir + "/scripts/bt-agent.py"]
        running: root.available && root.on && (root.holders > 0 || root.pairingDev !== null)
        stdinEnabled: true
        onRunningChanged: if (!running) {
            ready = false; root.request = null
            // no agent (no python3 / PyGObject): plain pairing still works for most headphones and mice
            if (root.pairLater) { root.pairLater.pair(); root.pairLater = null }
        }
        stdout: SplitParser {
            onRead: line => {
                let e
                try { e = JSON.parse(line) } catch (x) { return }
                if (e.ev === "ready") {
                    agent.ready = true
                    if (root.pairLater) { root.pairLater.pair(); root.pairLater = null }
                } else if (e.ev === "cancel") root.request = null
                else if (e.ev === "error") console.warn("bluetooth agent:", e.message)
                else root.request = { kind: e.ev, device: e.device, name: root.nameOf(e.device), code: e.passkey || e.code || "" }
            }
        }
    }
}
