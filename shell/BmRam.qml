// BmRam.qml — memory. Vars: percent, used, total, free (GiB). Level = percent.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property var vars: ({ percent: SysInfo.ram, used: SysInfo.ramUsed.toFixed(1), total: SysInfo.ramTotal.toFixed(1),
                                   free: (SysInfo.ramTotal - SysInfo.ramUsed).toFixed(1) })
    readonly property real level: SysInfo.ram
    readonly property string format: "RAM {percent}%"
    readonly property string tooltip: `${vars.used} / ${vars.total} GiB`
}
