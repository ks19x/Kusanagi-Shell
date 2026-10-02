// SpStorage.qml — disks, and xbps housekeeping (cache, orphans, old kernels) run in a terminal with sudo
import Quickshell
import Quickshell.Io
import QtQuick

Column {
    id: page
    spacing: 22

    property var disks: []             // [{ src, fs, size, used, mount }]
    property string cache: "…"
    property int orphans: -1
    property string kernels: "…"

    function gib(b) { return b >= 1099511627776 ? (b / 1099511627776).toFixed(2) + " TiB" : (b / 1073741824).toFixed(1) + " GiB" }
    function term(cmd) {
        Quickshell.execDetached([Config.launcher.terminal, "-e", "sh", "-c", cmd + "; echo; read -p 'done — Enter to close ' _"])
        refresh.restart()
    }
    Timer { id: refresh; interval: 6000; onTriggered: { dfProc.running = true; cacheProc.running = true; orphanProc.running = true; kernelProc.running = true } }

    Process {
        id: dfProc
        running: true
        command: ["df", "-B1", "--output=source,fstype,size,used,target", "-x", "tmpfs", "-x", "devtmpfs", "-x", "efivarfs", "-x", "overlay"]
        stdout: StdioCollector {
            onStreamFinished: {
                const seen = {}
                page.disks = text.trim().split("\n").slice(1).map(l => l.trim().split(/\s+/))
                    .filter(f => f.length >= 5 && !seen[f[0]] && (seen[f[0]] = true))
                    .map(f => ({ src: f[0], fs: f[1], size: +f[2], used: +f[3], mount: f.slice(4).join(" ") }))
            }
        }
    }
    Process { id: cacheProc; running: true; command: ["du", "-sh", "/var/cache/xbps"]; stdout: StdioCollector { onStreamFinished: page.cache = text.split(/\s+/)[0] || "0" } }
    Process { id: orphanProc; running: true; command: ["xbps-query", "-O"]; stdout: StdioCollector { onStreamFinished: page.orphans = text.trim() ? text.trim().split("\n").length : 0 } }
    Process { id: kernelProc; running: true; command: ["sh", "-c", "vkpurge list 2>/dev/null | wc -l"]; stdout: StdioCollector { onStreamFinished: page.kernels = text.trim() } }

    SpGroup {
        title: "Disks"
        Repeater {
            model: page.disks
            Column {
                required property var modelData
                width: parent.width
                spacing: 6
                Item {
                    width: parent.width; height: 18
                    CpText { text: modelData.mount; font.pixelSize: 12; font.bold: true }
                    CpText { x: 120; text: `${modelData.src}  ·  ${modelData.fs}`; font.pixelSize: 10; color: Theme.textDim }
                    CpText { anchors.right: parent.right; text: `${page.gib(modelData.used)} / ${page.gib(modelData.size)}`; font.pixelSize: 11 }
                }
                Rectangle {
                    width: parent.width; height: 6; radius: 3
                    color: Theme.alpha(Theme.text, 0.1)
                    Rectangle {
                        readonly property real f: modelData.size ? modelData.used / modelData.size : 0
                        width: parent.width * f; height: parent.height; radius: 3
                        color: f > 0.9 ? Theme.danger : f > 0.75 ? "#e8be62" : Theme.accent
                    }
                }
            }
        }
    }

    SpGroup {
        title: "Clean up"
        hint: "Each runs in a terminal and asks for your sudo password."
        CpRow {
            width: parent.width; label: "Package cache"; hint: page.cache + " in /var/cache/xbps — keeps only the installed versions"
            CpChip { label: "Clean"; icon: 0xf00e3; onClicked: page.term("sudo xbps-remove -O") }
        }
        CpRow {
            width: parent.width; label: "Orphaned packages"
            hint: page.orphans < 0 ? "…" : page.orphans === 0 ? "none" : page.orphans + " installed as dependencies that nothing needs"
            CpChip { label: "Remove"; icon: 0xf0a7a; enabled: page.orphans > 0; opacity: enabled ? 1 : 0.4; onClicked: page.term("sudo xbps-remove -o") }
        }
        CpRow {
            width: parent.width; label: "Old kernels"; hint: page.kernels === "0" ? "none to remove" : page.kernels + " removable (vkpurge)"
            CpChip { label: "Remove"; icon: 0xf0a7a; enabled: page.kernels !== "0" && page.kernels !== "…"; opacity: enabled ? 1 : 0.4; onClicked: page.term("sudo vkpurge rm all") }
        }
    }
}
