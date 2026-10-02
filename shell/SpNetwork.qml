// SpNetwork.qml — connection, live throughput, NetworkManager devices
import Quickshell
import Quickshell.Io
import QtQuick

Column {
    id: page
    spacing: 22

    Component.onCompleted: SysInfo.settingsOpen = true
    Component.onDestruction: SysInfo.settingsOpen = false

    property var devices: []           // [{ dev, type, state, conn }]
    Process {
        id: nm
        running: true
        command: ["nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device"]
        stdout: StdioCollector {
            onStreamFinished: page.devices = text.trim().split("\n").filter(l => l)
                .map(l => l.split(":")).filter(f => f[1] !== "loopback")
                .map(f => ({ dev: f[0], type: f[1], state: f[2], conn: f.slice(3).join(":") }))
        }
    }
    Timer { interval: 5000; running: true; repeat: true; onTriggered: nm.running = true }

    SpGroup {
        title: "Connection"
        Item {
            width: parent.width; height: 64
            CpIcon { id: ni; anchors.verticalCenter: parent.verticalCenter; cp: SysInfo.netWifi ? 0xf05a9 : 0xf0200; font.pixelSize: 30; color: SysInfo.netUp ? Theme.accent : Theme.textDim }
            Column {
                anchors { left: ni.right; leftMargin: 16; verticalCenter: parent.verticalCenter }
                spacing: 2
                CpText { text: SysInfo.netUp ? (SysInfo.netWifi ? "Wi-Fi" : "Ethernet") + " · connected" : "Offline"; font.pixelSize: 14; font.bold: true }
                CpText { text: SysInfo.netUp ? `${SysInfo.netIf}   ·   ${SysInfo.netIp || "…"}` : "no default route"; font.pixelSize: 11; color: Theme.textDim }
            }
            Column {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                spacing: 2
                CpText { anchors.right: parent.right; text: "↓ " + SysInfo.rate(SysInfo.netDown); font.pixelSize: 13; font.bold: true }
                CpText { anchors.right: parent.right; text: "↑ " + SysInfo.rate(SysInfo.netUpRate); font.pixelSize: 11; color: Theme.textDim }
            }
        }
        CpSpark {
            width: parent.width; height: 60
            values: SysInfo.netHistory
        }
    }

    SpGroup {
        title: "Devices"
        Repeater {
            model: page.devices
            Item {
                required property var modelData
                width: parent.width; height: 36
                Rectangle {
                    id: dot
                    width: 8; height: 8; radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: modelData.state.startsWith("connected") ? "#85cc87" : modelData.state === "unavailable" ? Theme.danger : Theme.textDim
                }
                CpText {
                    anchors { left: dot.right; leftMargin: 12; verticalCenter: parent.verticalCenter }
                    text: modelData.dev
                    font.pixelSize: 12
                    font.bold: true
                }
                CpText { x: 180; anchors.verticalCenter: parent.verticalCenter; text: modelData.type; font.pixelSize: 11; color: Theme.textDim }
                CpText { x: 300; anchors.verticalCenter: parent.verticalCenter; text: modelData.conn || modelData.state; font.pixelSize: 11 }
            }
        }
        CpChip { label: "Connection editor"; icon: 0xf06f3; onClicked: Quickshell.execDetached(["nm-connection-editor"]) }
    }
}
