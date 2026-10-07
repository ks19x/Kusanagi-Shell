// BmBattery.qml — battery (UPower), hidden without one. Vars: capacity, time, icon.
// Status: charging | discharging | full. Thresholds count down (warning 30, critical 15).
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import Quickshell.Services.UPower
import QtQuick

Item {
    id: p
    property var m
    readonly property var dev: UPower.displayDevice
    readonly property bool has: !!dev && dev.isLaptopBattery
    readonly property real pct: has ? Math.round(dev.percentage * (dev.percentage <= 1 ? 100 : 1)) : 0
    readonly property bool charging: has && (dev.state === UPowerDeviceState.Charging || dev.state === UPowerDeviceState.PendingCharge)
    readonly property bool full: has && dev.state === UPowerDeviceState.FullyCharged
    function hm(s) { if (!(s > 0)) return ""; const h = Math.floor(s / 3600), mi = Math.floor(s % 3600 / 60); return h + "h " + mi + "m" }
    readonly property var vars: ({ capacity: pct, time: has ? hm(charging ? dev.timeToFull : dev.timeToEmpty) : "" })
    readonly property real level: pct
    readonly property bool lowIsBad: true
    readonly property var thresholds: ({ warning: 30, critical: 15 })
    readonly property string status: !has ? "" : full ? "full" : charging ? "charging" : "discharging"
    readonly property var icons: [0xf008e, 0xf007a, 0xf007b, 0xf007c, 0xf007d, 0xf007e, 0xf007f, 0xf0080, 0xf0081, 0xf0082, 0xf0079].map(c => String.fromCodePoint(c))
    readonly property string format: charging ? String.fromCodePoint(0xf0084) + " {capacity}%" : "{icon} {capacity}%"
    readonly property bool shown: has
    readonly property string tooltip: vars.time ? vars.time + (charging ? " until full" : " left") : ""
}
