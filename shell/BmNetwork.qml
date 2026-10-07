// BmNetwork.qml — connection. Vars: ifname, ip, down, up, icon. Status: wifi | ethernet | disconnected.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property string status: !SysInfo.netUp ? "disconnected" : SysInfo.netWifi ? "wifi" : "ethernet"
    readonly property var icons: ({ disconnected: String.fromCodePoint(0xf05aa), wifi: "\uf1eb", ethernet: String.fromCodePoint(0xf0200) })
    readonly property var vars: ({ ifname: SysInfo.netIf, ip: SysInfo.netIp, down: SysInfo.rate(SysInfo.netDown), up: SysInfo.rate(SysInfo.netUpRate) })
    readonly property string format: "{icon}"
    readonly property var actions: ({ click: "panel:network", middleClick: "settings:network" })
    readonly property string tooltip: SysInfo.netUp ? `${SysInfo.netIf}  ${SysInfo.netIp}\n⇣ ${vars.down}  ⇡ ${vars.up}` : ""
}
