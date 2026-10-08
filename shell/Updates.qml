pragma Singleton
// Updates.qml — how many package updates are waiting (`kusanagi updates`: xbps, pacman + AUR, apt, dnf,
// zypper, apk, … plus Flatpak — no root, nothing installed). Checks every Config.updates.interval hours,
// only while a bar shows the updates module or update notifications are on. upgrade() opens a terminal.
import Quickshell
import Quickshell.Io
import QtQuick

Singleton {
    id: root

    property int count: 0
    property bool known: false             // false until a check worked (no checker = stays false)
    property var list: []                  // [{ name, source }]
    property bool checking: false
    property real checkedAt: 0

    readonly property bool wanted: Config.updates.interval > 0 && (!!BarSpec.uses.updates || Config.updates.notify)

    Process {
        id: proc
        command: ["kusanagi", "updates", "raw"]
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = text.split("\n").filter(l => l.trim() !== "")
                const n = parseInt(lines[0])
                root.checking = false
                if (isNaN(n)) { root.known = false; return }
                const before = root.count
                root.known = true
                root.count = n
                root.list = lines.slice(1).map(l => { const p = l.split("\t"); return { name: p[0], source: p[1] || "" } })
                root.checkedAt = Date.now()
                if (Config.updates.notify && n > 0 && n !== before)
                    Quickshell.execDetached(["notify-send", "-a", "Updates", "-i", "system-software-update",
                                             n + (n === 1 ? " update" : " updates") + " waiting", "Click the updates module (or run: kusanagi updates upgrade)"])
            }
        }
        onExited: root.checking = false
    }
    function check() { if (!proc.running) { checking = true; proc.running = true } }
    function upgrade() { Quickshell.execDetached(["kusanagi", "updates", "upgrade"]); after.restart() }
    // re-count once the terminal has had time to finish
    Timer { id: after; interval: 120000; onTriggered: root.check() }

    // first check a minute after login (don't compete with startup), then every interval
    Timer { interval: 60000; running: root.wanted && root.checkedAt === 0; onTriggered: root.check() }
    Timer { interval: Math.max(1, Config.updates.interval) * 3600000; repeat: true; running: root.wanted; onTriggered: root.check() }
}
