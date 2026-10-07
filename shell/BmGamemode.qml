// BmGamemode.qml — shown while game mode is on (always: true = also when off). Status: on | off.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property var vars: ({})
    readonly property string status: GameMode.active ? "on" : "off"
    readonly property string format: String.fromCodePoint(0xf0297)
    readonly property bool shown: GameMode.active || (m && m.eff.always === true)
    readonly property var actions: ({ click: "gamemode" })
    readonly property string tooltip: GameMode.active ? "Game mode on" : "Game mode off"
}
