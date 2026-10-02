// SpAbout.qml — this machine and Kusanagi itself (including how much memory the shell uses)
import Quickshell
import Quickshell.Io
import QtQuick

Column {
    id: page
    spacing: 22

    Component.onCompleted: SysInfo.settingsOpen = true
    Component.onDestruction: SysInfo.settingsOpen = false

    readonly property bool mango: !!Quickshell.env("MANGO_INSTANCE_SIGNATURE")
    property string distro: "…"
    property string cpuModel: "…"
    property string gpuModel: "…"
    property string wm: "…"
    property string qsVersion: "…"
    property int shellMb: 0

    Process {
        running: true
        command: ["sh", "-c",
            ". /etc/os-release; echo \"$PRETTY_NAME\";" +
            "grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//';" +
            "lspci -mm 2>/dev/null | grep -iE 'vga|3d' | head -1 | cut -d'\"' -f6 | sed 's/.*\\[\\(.*\\)\\].*/\\1/';" +
            (page.mango ? "mango -v 2>&1 | head -1;" : "hyprctl version -j 2>/dev/null | grep -m1 '\"tag\"' | cut -d'\"' -f4 | sed 's/^/Hyprland /';") +
            "qs --version | head -1"]
        stdout: StdioCollector {
            onStreamFinished: {
                const l = text.split("\n")
                page.distro = l[0] || "?"; page.cpuModel = l[1] || "?"; page.gpuModel = l[2] || "?"
                page.wm = l[3] || "?"; page.qsVersion = (l[4] || "?").replace(/ \(.*/, "")
            }
        }
    }
    // the shell's own proportional memory (PSS)
    FileView { id: smaps; path: "/proc/self/smaps_rollup"; blockLoading: true; printErrors: false }
    Timer {
        interval: 3000; running: true; repeat: true; triggeredOnStart: true
        onTriggered: { smaps.reload(); const m = smaps.text().match(/^Pss:\s+(\d+)/m); page.shellMb = m ? Math.round(+m[1] / 1024) : 0 }
    }

    // ---- hero ----
    Rectangle {
        width: parent.width
        height: 150
        radius: Config.look.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Theme.alpha(Theme.accent, 0.28) }
            GradientStop { position: 1; color: Theme.alpha(Theme.accent2, 0.08) }
        }
        border.width: 1
        border.color: Theme.alpha(Theme.text, 0.08)

        CpText {
            anchors { right: parent.right; rightMargin: 28; verticalCenter: parent.verticalCenter }
            text: "草薙"
            font.pixelSize: 72
            font.bold: true
            color: Theme.alpha(Theme.text, 0.12)
        }
        Column {
            x: 28; anchors.verticalCenter: parent.verticalCenter
            spacing: 4
            CpText { text: "KUSANAGI"; font.pixelSize: 30; font.bold: true; font.letterSpacing: 8 }
            CpText { text: `${Quickshell.env("USER")}'s Quickshell  ·  ${page.qsVersion}`; font.pixelSize: 12; color: Theme.alpha(Theme.text, 0.75) }
            CpText { text: `using ${page.shellMb} MB right now`; font.pixelSize: 11; color: Theme.accent }
        }
    }

    SpGroup {
        title: "This machine"
        Repeater {
            model: [
                { k: "Host", v: `${Quickshell.env("USER")}@${SysInfo.host}` },
                { k: "System", v: page.distro },
                { k: "Kernel", v: SysInfo.kernel },
                { k: "Compositor", v: page.wm },
                { k: "Processor", v: `${page.cpuModel}  (${SysInfo.cpuThreads} threads)` },
                { k: "Graphics", v: page.gpuModel },
                { k: "Memory", v: `${SysInfo.ramUsed.toFixed(1)} of ${SysInfo.ramTotal.toFixed(1)} GiB in use` },
                { k: "Uptime", v: SysInfo.uptime }
            ]
            Item {
                required property var modelData
                width: parent.width; height: 22
                CpText { text: modelData.k; font.pixelSize: 11; color: Theme.textDim }
                CpText { x: 130; width: parent.width - 130; text: modelData.v; font.pixelSize: 12; elide: Text.ElideRight }
            }
        }
    }

    SpGroup {
        title: "Kusanagi"
        Flow {
            width: parent.width
            spacing: 8
            CpChip { label: "Open config folder"; icon: 0xf024b; onClicked: Quickshell.execDetached(["thunar", Quickshell.shellDir]) }
            CpChip { label: "Edit settings.json"; icon: 0xf107b; onClicked: Quickshell.execDetached(["mousepad", Quickshell.env("HOME") + "/.config/kusanagi/settings.json"]) }
            CpChip { label: "Reset everything"; icon: 0xf0709; onClicked: Config.reset() }
            CpChip { label: "Restart Kusanagi"; icon: 0xf0450; onClicked: Quickshell.execDetached(["kusanagi", "restart"]) }
        }
    }
}
