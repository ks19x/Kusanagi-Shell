// BmLauncher.qml — app launcher button.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property var vars: ({})
    readonly property string format: String.fromCodePoint(0xf003b)
    readonly property var actions: ({ click: "launcher", rightClick: "settings" })
}
