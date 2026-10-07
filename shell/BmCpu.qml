// BmCpu.qml — CPU load. Vars: usage. Level = usage.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property var vars: ({ usage: SysInfo.cpu })
    readonly property real level: SysInfo.cpu
    readonly property string format: "CPU {usage}%"
}
