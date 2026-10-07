// BmUptime.qml — uptime and load. Vars: uptime, load.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property var vars: ({ uptime: SysInfo.uptime, load: SysInfo.load })
    readonly property string format: String.fromCodePoint(0xf0954) + " {uptime}"
    readonly property string tooltip: "load " + SysInfo.load
}
