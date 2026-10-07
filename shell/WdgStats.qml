// WdgStats.qml — CPU, RAM and GPU (or temperature) rings. SysInfo reads only while this is on screen.
import QtQuick

Row {
    id: s
    property var w
    readonly property var o: w ? w.spec : ({})
    spacing: 18
    Component.onCompleted: SysInfo.widgetUsers++
    Component.onDestruction: SysInfo.widgetUsers--
    CpGauge { value: SysInfo.cpu; label: "CPU"; size: 96; animated: false }
    CpGauge { value: SysInfo.ram; label: "RAM"; detail: SysInfo.ramUsed.toFixed(1) + " GiB"; size: 96; animated: false }
    CpGauge {
        readonly property bool temp: s.o.third === "temp"
        value: temp ? SysInfo.cpuTemp : SysInfo.gpu
        text: temp ? SysInfo.cpuTemp + "°" : Math.round(SysInfo.gpu) + "%"
        label: temp ? "TEMP" : "GPU"
        warnAt: temp ? 70 : 101; hotAt: temp ? 85 : 101
        size: 96; animated: false
    }
}
