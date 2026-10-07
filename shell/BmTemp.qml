// BmTemp.qml — temperature. Options: sensor cpu | gpu. Vars: temp, cpu, gpu. Level = temp (°C).
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property int t: m && m.eff.sensor === "gpu" ? SysInfo.gpuTemp : SysInfo.cpuTemp
    readonly property var vars: ({ temp: t, cpu: SysInfo.cpuTemp, gpu: SysInfo.gpuTemp })
    readonly property real level: t
    readonly property string format: "{temp}°C"
    readonly property string tooltip: `CPU ${SysInfo.cpuTemp}°C   GPU ${SysInfo.gpuTemp}°C`
}
