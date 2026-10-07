// BmDisk.qml — root filesystem. Vars: percent, used, total, free (GiB). Level = percent.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property real pct: SysInfo.diskTotal ? 100 * SysInfo.diskUsed / SysInfo.diskTotal : 0
    readonly property var vars: ({ percent: Math.round(pct), used: Math.round(SysInfo.diskUsed), total: Math.round(SysInfo.diskTotal),
                                   free: Math.round(SysInfo.diskTotal - SysInfo.diskUsed) })
    readonly property real level: pct
    readonly property string format: String.fromCodePoint(0xf02ca) + " {percent}%"
    readonly property string tooltip: `${vars.used} / ${vars.total} GiB used`
    readonly property bool shown: SysInfo.diskTotal > 0
}
