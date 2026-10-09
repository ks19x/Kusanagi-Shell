// BmBluetooth.qml — Bluetooth (hidden without an adapter). Vars: device (first connected), count, battery
// (its %, "" when it doesn't say). Status: off | on | connected | blocked. Click = the Bluetooth tab,
// right click = on / off.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property var first: Bt.connected.length ? Bt.connected[0] : null
    readonly property int bat: Bt.battery(first)
    readonly property var vars: ({ device: first ? first.name : "", count: Bt.connected.length, battery: bat >= 0 ? bat + "%" : "" })
    readonly property string status: Bt.blocked ? "blocked" : !Bt.on ? "off" : first ? "connected" : "on"
    readonly property var icons: ({ off: String.fromCodePoint(0xf00b2), blocked: String.fromCodePoint(0xf00b2),
                                    on: String.fromCodePoint(0xf00af), connected: String.fromCodePoint(0xf00b1) })
    readonly property string format: status === "connected" ? "{icon} {device}" : "{icon}"
    readonly property bool shown: Bt.available
    readonly property var actions: ({ click: "panel:bluetooth", rightClick: "bluetooth:toggle", middleClick: "settings:bluetooth" })
    readonly property string tooltip: "Bluetooth: " + Bt.summary + (bat >= 0 ? " · " + bat + "%" : "")
        + (Bt.connected.length > 1 ? "\n" + Bt.connected.map(d => d.name).join(", ") : "")
}
