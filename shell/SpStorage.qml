// SpStorage.qml — disks, and package-manager housekeeping (cache, orphans, old kernels) for any distro
import Quickshell
import Quickshell.Io
import QtQuick

Column {
    id: page
    spacing: 22

    property var disks: []             // [{ src, fs, size, used, mount }]
    // housekeeping for whichever package manager this is (lib/distro.sh storage)
    property var st: ({})
    readonly property bool stReady: st.cache !== undefined

    function gib(b) { return b >= 1099511627776 ? (b / 1099511627776).toFixed(2) + " TiB" : (b / 1073741824).toFixed(1) + " GiB" }
    function term(cmd) {
        Quickshell.execDetached([Config.launcher.terminal, "-e", "sh", "-c", cmd + "; echo; printf 'done — Enter to close '; read _"])
        refresh.restart()
    }
    Timer { id: refresh; interval: 6000; onTriggered: { dfProc.running = true; stProc.running = true } }

    Process {
        id: dfProc
        running: true
        command: ["df", "-B1", "--output=source,fstype,size,used,target", "-x", "tmpfs", "-x", "devtmpfs", "-x", "efivarfs", "-x", "overlay"]
        stdout: StdioCollector {
            onStreamFinished: {
                const seen = {}
                page.disks = text.trim().split("\n").slice(1).map(l => l.trim().split(/\s+/))
                    .filter(f => f.length >= 5 && !f[1].startsWith("fuse.") && !seen[f[0]] && (seen[f[0]] = true))
                    .map(f => ({ src: f[0], fs: f[1], size: +f[2], used: +f[3], mount: f.slice(4).join(" ") }))
            }
        }
    }
    Process {
        id: stProc
        running: true
        command: ["kusanagi", "distro", "storage"]
        stdout: StdioCollector {
            onStreamFinished: {
                const o = {}
                for (const l of text.split("\n")) { const i = l.indexOf("="); if (i > 0) o[l.slice(0, i)] = l.slice(i + 1) }
                page.st = o
            }
        }
    }

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
        visible: page.stReady && (!!page.st.cacheClean || !!page.st.orphansRemove || !!page.st.kernelsRemove)
        hint: "Each runs in a terminal and asks for your password."
        CpRow {
            visible: !!page.st.cacheClean
            width: parent.width; label: "Package cache"; hint: (page.st.cacheSize || "?") + " in " + page.st.cache
            CpChip { label: "Clean"; icon: 0xf00e3; onClicked: page.term(page.st.cacheClean) }
        }
        CpRow {
            visible: !!page.st.orphansRemove
            width: parent.width; label: "Unneeded packages"
            hint: page.st.orphans === "" ? "installed as dependencies that nothing needs any more" : page.st.orphans === "0" ? "none" : page.st.orphans + " installed as dependencies that nothing needs"
            CpChip { label: "Remove"; icon: 0xf0a7a; enabled: page.st.orphans !== "0"; opacity: enabled ? 1 : 0.4; onClicked: page.term(page.st.orphansRemove) }
        }
        CpRow {
            visible: !!page.st.kernelsRemove
            width: parent.width; label: "Old kernels"; hint: page.st.kernels === "0" ? "none to remove" : page.st.kernels + " removable"
            CpChip { label: "Remove"; icon: 0xf0a7a; enabled: page.st.kernels !== "0"; opacity: enabled ? 1 : 0.4; onClicked: page.term(page.st.kernelsRemove) }
        }
    }
}
