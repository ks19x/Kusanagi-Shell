// BmGpu.qml — GPU load (amdgpu). Vars: usage, vramUsed, vramTotal (GiB). Level = usage.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property var vars: ({ usage: SysInfo.gpu, vramUsed: SysInfo.vramUsed.toFixed(1), vramTotal: SysInfo.vramTotal.toFixed(1) })
    readonly property real level: SysInfo.gpu
    readonly property string format: "GPU {usage}%"
    readonly property string tooltip: SysInfo.vramTotal ? `VRAM ${vars.vramUsed} / ${vars.vramTotal} GiB` : ""
}
