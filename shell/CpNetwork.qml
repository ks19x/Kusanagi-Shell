// CpNetwork.qml — control panel Network tab: the connection with live speeds, Wi-Fi (only when
// there is a Wi-Fi card), Mullvad on/off, and which DNS resolver is answering.
// Everything is polled only while this tab is open.
import Quickshell
import Quickshell.Io
import QtQuick

Column {
    id: root
    required property var panel
    spacing: 12

    // ---------------------------------------------------------------- state
    property bool hasWifi: false
    property bool wifiOn: false
    property var networks: []           // [{ ssid, signal, secure, active }]
    property var known: []              // saved connection names
    property string askFor: ""          // SSID waiting for a password
    property string busy: ""            // SSID being connected to

    property bool hasMullvad: false
    property string vpnState: ""        // connected | connecting | disconnected | disconnecting | error
    property string vpnWhere: ""

    property string dnsName: ""
    property string dnsUpstream: ""
    property bool dnsRunning: false

    function run(cmd, then) {
        const p = runner.createObject(root, { command: cmd })
        p.done.connect(out => { if (then) then(out); p.destroy() })
        p.running = true
    }
    Component {
        id: runner
        Process {
            signal done(string out)
            stdout: StdioCollector { id: col }
            onExited: done(col.text)
        }
    }

    function refreshWifi() {
        run(["nmcli", "-t", "-f", "TYPE", "device"], out => {
            root.hasWifi = out.split("\n").includes("wifi")
            if (!root.hasWifi) return
            run(["nmcli", "radio", "wifi"], o => root.wifiOn = o.trim() === "enabled")
            run(["nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"], o =>
                root.known = o.trim().split("\n").filter(l => l.endsWith(":802-11-wireless")).map(l => l.slice(0, l.lastIndexOf(":")).replace(/\\:/g, ":")))
            run(["nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list"], o => {
                const seen = {}
                root.networks = o.trim().split("\n").filter(l => l).map(l => {
                    const f = l.replace(/\\:/g, "\u0001").split(":").map(s => s.replace(/\u0001/g, ":"))
                    return { active: f[0] === "*", ssid: f[1], signal: +f[2], secure: !!f[3] && f[3] !== "--" }
                }).filter(n => n.ssid && !seen[n.ssid] && (seen[n.ssid] = true))
                  .sort((a, b) => b.active - a.active || b.signal - a.signal).slice(0, 8)
            })
        })
    }
    function refreshVpn() {
        run(["sh", "-c", "command -v mullvad >/dev/null && mullvad status --json"], out => {
            root.hasMullvad = out.trim() !== ""
            if (!root.hasMullvad) return
            try {
                const s = JSON.parse(out)
                root.vpnState = s.state
                const l = s.details && s.details.location
                root.vpnWhere = l ? [l.city, l.country].filter(x => x).join(", ") + (l.ipv4 ? "  ·  " + l.ipv4 : "") : ""
            } catch (e) { root.vpnState = "error" }
        })
    }
    function refreshDns() {
        // read-only: the resolver is a system service, switching it off would leave you without DNS
        run(["sh", "-c", "grep -m1 '^nameserver' /etc/resolv.conf | cut -d' ' -f2; pgrep -x nextdns >/dev/null && echo nextdns; " +
                         "grep -m1 '^forwarder' /etc/nextdns.conf 2>/dev/null | cut -d' ' -f2"], out => {
            const l = out.trim().split("\n")
            const local = l[0] === "127.0.0.1" || l[0] === "::1"
            root.dnsRunning = l.includes("nextdns")
            root.dnsName = root.dnsRunning ? "NextDNS (encrypted)" : local ? "Local resolver (not running!)" : (l[0] || "unknown")
            const fwd = l.find(x => x.startsWith("https://")) || ""
            root.dnsUpstream = fwd.includes("1.1.1.1") ? "→ Cloudflare over HTTPS" : fwd ? "→ " + fwd.split(",")[0] : ""
        })
    }
    function connect(n) {
        if (n.active) return
        if (root.known.includes(n.ssid)) { busy = n.ssid; run(["nmcli", "connection", "up", "id", n.ssid], () => { busy = ""; refreshWifi() }) }
        else if (n.secure) askFor = n.ssid
        else { busy = n.ssid; run(["nmcli", "device", "wifi", "connect", n.ssid], () => { busy = ""; refreshWifi() }) }
    }

    Component.onCompleted: { refreshWifi(); refreshVpn(); refreshDns() }
    Timer { interval: 3000; repeat: true; running: true; onTriggered: root.refreshVpn() }
    Timer { interval: 15000; repeat: true; running: root.hasWifi; onTriggered: root.refreshWifi() }

    // ---------------------------------------------------------------- connection
    CpCard {
        width: parent.width
        height: 136
        CpIcon {
            id: ni
            x: 14; y: 16
            cp: !SysInfo.netUp ? 0xf05aa : SysInfo.netWifi ? 0xf05a9 : 0xf0200
            font.pixelSize: 26
            color: SysInfo.netUp ? Theme.accent : Theme.textDim
        }
        Column {
            anchors { left: ni.right; leftMargin: 12; top: parent.top; topMargin: 14 }
            spacing: 2
            CpText { text: SysInfo.netUp ? (SysInfo.netWifi ? "Wi-Fi" : "Ethernet") + " · connected" : "Offline"; font.pixelSize: 13; font.bold: true }
            CpText { text: SysInfo.netUp ? `${SysInfo.netIf}   ·   ${SysInfo.netIp || "…"}` : "no default route"; font.pixelSize: 11; color: Theme.textDim }
        }
        Column {
            anchors { right: parent.right; rightMargin: 14; top: parent.top; topMargin: 14 }
            spacing: 2
            CpText { anchors.right: parent.right; text: "↓ " + SysInfo.rate(SysInfo.netDown); font.pixelSize: 13; font.bold: true }
            CpText { anchors.right: parent.right; text: "↑ " + SysInfo.rate(SysInfo.netUpRate); font.pixelSize: 11; color: Theme.textDim }
        }
        CpSpark {
            x: 12; y: 62
            width: parent.width - 24; height: 62
            values: SysInfo.netHistory
        }
    }

    // ---------------------------------------------------------------- Wi-Fi
    CpCard {
        visible: root.hasWifi
        width: parent.width
        height: wifiCol.implicitHeight + 24
        Column {
            id: wifiCol
            x: 12; y: 12
            width: parent.width - 24
            spacing: 4
            Item {
                width: parent.width; height: 24
                CpText { anchors.verticalCenter: parent.verticalCenter; text: "WI-FI"; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1; color: Theme.textDim }
                Row {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: 6
                    CpIconButton {
                        visible: root.wifiOn
                        icon: 0xf0450
                        onClicked: root.run(["nmcli", "device", "wifi", "rescan"], () => root.refreshWifi())
                    }
                    CpSwitch {
                        anchors.verticalCenter: parent.verticalCenter
                        on: root.wifiOn
                        onToggled: v => { root.wifiOn = v; root.run(["nmcli", "radio", "wifi", v ? "on" : "off"], () => root.refreshWifi()) }
                    }
                }
            }
            Repeater {
                model: root.wifiOn ? root.networks : []
                Rectangle {
                    id: net
                    required property var modelData
                    width: wifiCol.width
                    height: 32
                    radius: Math.max(6, Config.look.radius - 8)
                    color: modelData.active ? Theme.alpha(Theme.accent, 0.16) : Theme.alpha(Theme.text, netArea.containsMouse ? 0.06 : 0)
                    Behavior on color { ColorAnimation { duration: Config.ms(160) } }
                    CpIcon {
                        id: sig
                        x: 10; anchors.verticalCenter: parent.verticalCenter
                        cp: [0xf092f, 0xf091f, 0xf0922, 0xf0925, 0xf0928][Math.min(4, Math.floor(net.modelData.signal / 21))]
                        font.pixelSize: 15
                        color: net.modelData.active ? Theme.accent : Theme.text
                    }
                    CpText {
                        anchors { left: sig.right; leftMargin: 10; right: lock.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        text: net.modelData.ssid + (root.busy === net.modelData.ssid ? "  · connecting…" : "")
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        font.bold: net.modelData.active
                    }
                    CpIcon {
                        id: lock
                        anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                        cp: 0xf033e
                        font.pixelSize: 12
                        color: Theme.textDim
                        visible: net.modelData.secure
                    }
                    MouseArea {
                        id: netArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.connect(net.modelData)
                    }
                }
            }
            CpField {
                visible: root.askFor !== ""
                width: wifiCol.width
                icon: 0xf033e
                placeholder: "Password for " + root.askFor
                onAccepted: t => {
                    const ssid = root.askFor
                    root.askFor = ""; root.busy = ssid
                    root.run(["nmcli", "device", "wifi", "connect", ssid, "password", t], () => { root.busy = ""; root.refreshWifi() })
                }
            }
            CpText { visible: !root.wifiOn; text: "Wi-Fi is off"; color: Theme.textDim; font.pixelSize: 12 }
        }
    }

    // ---------------------------------------------------------------- VPN + DNS
    Row {
        width: parent.width
        spacing: 12
        readonly property real half: (width - spacing) / 2

        CpCard {
            visible: root.hasMullvad
            width: root.hasMullvad ? parent.half : 0
            height: 92
            readonly property bool on: root.vpnState === "connected"
            readonly property bool moving: root.vpnState === "connecting" || root.vpnState === "disconnecting"
            CpIcon {
                id: vi
                x: 14; y: 14
                cp: parent.on ? 0xf0582 : 0xf099d
                font.pixelSize: 20
                color: parent.on ? Theme.ok : Theme.textDim
            }
            CpText { anchors { left: vi.right; leftMargin: 8; verticalCenter: vi.verticalCenter }
            text: "Mullvad"; font.pixelSize: 13; font.bold: true }
            CpSwitch {
                anchors { right: parent.right; rightMargin: 12; verticalCenter: vi.verticalCenter }
                on: parent.on || root.vpnState === "connecting"
                onToggled: v => { root.vpnState = v ? "connecting" : "disconnecting"; root.run(["mullvad", v ? "connect" : "disconnect"], () => root.refreshVpn()) }
            }
            CpText {
                x: 14; y: 46
                width: parent.width - 28
                text: parent.moving ? root.vpnState + "…" : parent.on ? "Protected" : "Not protected"
                font.pixelSize: 11
                font.bold: true
                color: parent.on ? Theme.ok : Theme.textDim
            }
            CpText {
                x: 14; y: 64
                width: parent.width - 28
                text: root.vpnWhere
                elide: Text.ElideRight
                font.pixelSize: 10
                color: Theme.textDim
            }
        }

        CpCard {
            width: root.hasMullvad ? parent.half : parent.width
            height: 92
            CpIcon {
                id: di
                x: 14; y: 14
                cp: 0xf0483
                font.pixelSize: 20
                color: root.dnsRunning ? Theme.accent : Theme.danger
            }
            CpText { anchors { left: di.right; leftMargin: 8; verticalCenter: di.verticalCenter }
            text: "DNS"; font.pixelSize: 13; font.bold: true }
            CpText { x: 14; y: 46; width: parent.width - 28; text: root.dnsName; elide: Text.ElideRight; font.pixelSize: 11; font.bold: true }
            CpText { x: 14; y: 64; width: parent.width - 28; text: root.dnsUpstream; elide: Text.ElideRight; font.pixelSize: 10; color: Theme.textDim }
        }
    }

    Row {
        spacing: 8
        CpChip { label: "Network settings"; icon: 0xf0493; onClicked: { root.panel.close(); root.panel.shellRef.openSettings("network") } }
        CpChip { label: "Connection editor"; icon: 0xf06f3; onClicked: root.panel.runClosed(["nm-connection-editor"]) }
    }
}
