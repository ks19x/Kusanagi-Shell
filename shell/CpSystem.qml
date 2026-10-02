// CpSystem.qml — live system view: four ring gauges, cpu + network history, vram/disk, uptime line
import Quickshell
import QtQuick

Column {
    id: root
    spacing: 12

    // value / total bar with a caption, used for VRAM and disk
    component Usage: Column {
        id: u
        property string label
        property real used
        property real total
        width: parent.width
        spacing: 6
        Item {
            width: parent.width; height: 14
            CpText { text: u.label; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1; color: Theme.textDim }
            CpText {
                anchors.right: parent.right
                text: u.total > 0 ? `${u.used.toFixed(1)} / ${u.total.toFixed(1)} GiB` : "—"
                font.pixelSize: 11
            }
        }
        Rectangle {
            width: parent.width; height: 6; radius: 3
            color: Theme.alpha(Theme.text, 0.1)
            Rectangle {
                width: u.total > 0 ? parent.width * Math.min(1, u.used / u.total) : 0
                height: parent.height; radius: 3
                color: Theme.accent
                Behavior on width { NumberAnimation { duration: Config.ms(700); easing.type: Easing.OutCubic } }
            }
        }
    }

    // ---------- gauges ----------
    CpCard {
        width: parent.width
        height: 150
        Row {
            anchors.centerIn: parent
            spacing: (parent.width - 4 * 92 - 24) / 3
            CpGauge {
                value: SysInfo.cpu
                label: "CPU"
                detail: SysInfo.cpuGhz ? `${SysInfo.cpuGhz.toFixed(2)} GHz` : `${SysInfo.cpuThreads} threads`
            }
            CpGauge {
                value: SysInfo.ram
                label: "MEMORY"
                detail: `${SysInfo.ramUsed.toFixed(1)} / ${Math.round(SysInfo.ramTotal)} GiB`
            }
            CpGauge {
                value: SysInfo.gpu
                label: "GPU"
                detail: SysInfo.gpuMhz ? `${SysInfo.gpuMhz} MHz` : "—"
            }
            CpGauge {
                value: SysInfo.cpuTemp
                text: SysInfo.cpuTemp + "°"
                label: "TEMP"
                detail: `GPU ${SysInfo.gpuTemp}°`
                warnAt: 70; hotAt: 85
            }
        }
    }

    // ---------- cpu history ----------
    CpCard {
        width: parent.width
        height: 86
        CpText { x: 14; y: 10; text: "CPU LOAD"; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1; color: Theme.textDim }
        CpText {
            anchors { right: parent.right; rightMargin: 14; top: parent.top; topMargin: 8 }
            text: `load ${SysInfo.load}`
            font.pixelSize: 10
            color: Theme.textDim
        }
        CpSpark {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 12; topMargin: 0 }
            height: 48
            values: SysInfo.cpuHistory
            max: 100
        }
    }

    // ---------- network + memory/disk ----------
    Row {
        width: parent.width
        spacing: 12

        CpCard {
            width: (parent.width - 12) / 2
            height: 112
            CpText { x: 14; y: 10; text: "NETWORK"; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1; color: Theme.textDim }
            CpText {
                anchors { right: parent.right; rightMargin: 14; top: parent.top; topMargin: 9 }
                text: SysInfo.netUp ? SysInfo.netIf : "offline"
                font.pixelSize: 10
                color: Theme.textDim
            }
            Row {
                x: 14; y: 30
                spacing: 16
                Row {
                    spacing: 4
                    CpIcon { cp: 0xf0045; font.pixelSize: 13; color: Theme.accent }
                    CpText { text: SysInfo.rate(SysInfo.netDown); font.pixelSize: 12; font.bold: true }
                }
                Row {
                    spacing: 4
                    CpIcon { cp: 0xf005d; font.pixelSize: 13; color: Theme.textDim }
                    CpText { text: SysInfo.rate(SysInfo.netUpRate); font.pixelSize: 12; font.bold: true }
                }
            }
            CpSpark {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 12 }
                height: 40
                values: SysInfo.netHistory
            }
        }

        CpCard {
            width: (parent.width - 12) / 2
            height: 112
            Column {
                anchors { fill: parent; margins: 14; topMargin: 12 }
                spacing: 14
                Usage { label: "VRAM"; used: SysInfo.vramUsed; total: SysInfo.vramTotal }
                Usage { label: "DISK  /"; used: SysInfo.diskUsed; total: SysInfo.diskTotal }
            }
        }
    }

    CpText {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        text: `${Quickshell.env("USER")}@${SysInfo.host}   ·   up ${SysInfo.uptime}   ·   kernel ${SysInfo.kernel}`
        font.pixelSize: 10
        color: Theme.textDim
    }
}
