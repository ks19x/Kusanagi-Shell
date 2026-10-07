// BmCaffeine.qml — shown while the screen is kept awake (always: true = also when off). Status: on | off.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property var vars: ({})
    readonly property string status: Caffeine.active ? "on" : "off"
    readonly property var icons: ({ on: String.fromCodePoint(0xf0176), off: String.fromCodePoint(0xf0176) })
    readonly property string format: "{icon}"
    readonly property bool shown: Caffeine.active || (m && m.eff.always === true)
    readonly property var actions: ({ click: "caffeine" })
    readonly property string tooltip: Caffeine.active ? "Caffeine: the screen won't sleep or lock — click to turn off" : "Caffeine off — click to keep the screen awake"
}
