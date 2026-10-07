// BmPower.qml — power menu button.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property var vars: ({})
    readonly property string format: "⏻"
    readonly property var actions: ({ click: "power" })
}
