// SysInfo.qml — system stats for the bar and the control panel, straight from /proc and /sys.
// Nothing is read unless something on screen shows it: the bar's enabled modules keep the
// cheap counters ticking (paused in game mode); `panelOpen` turns on the detailed set (temps, clocks, disk, history).
pragma Singleton
import Quickshell
import Quickshell.Io
import QtQuick

Singleton {
    id: root

    property bool panelOpen: false
    property bool settingsOpen: false     // Network / About pages of the settings app
    readonly property bool detailed: panelOpen || settingsOpen

    readonly property bool wantCpuRam: detailed || !GameMode.quiet && (Config.bar.modules.cpu || Config.bar.modules.ram)
    readonly property bool wantGpu: detailed || !GameMode.quiet && (Config.bar.modules.gpu || Config.bar.modules.temp)
    readonly property bool wantNet: detailed || !GameMode.quiet && Config.bar.modules.network

    // ---- values ----
    property int cpu: 0                 // %
    property real cpuGhz: 0
    property int cpuThreads: 0
    property int ram: 0                 // %
    property real ramUsed: 0            // GiB
    property real ramTotal: 0
    property int gpu: 0                 // % busy
    property int gpuMhz: 0
    property real vramUsed: 0           // GiB
    property real vramTotal: 0
    property int cpuTemp: 0             // °C
    property int gpuTemp: 0
    property real diskUsed: 0           // GiB, root filesystem
    property real diskTotal: 0
    property string uptime: ""
    property string load: ""
    property string kernel: ""
    property string host: ""

    property string netIf: ""
    property bool netWifi: false
    property bool netUp: false
    property string netIp: ""
    property real netDown: 0            // bytes/s
    property real netUpRate: 0

    // last ~2 minutes for the sparklines (one sample per tick while the panel is open)
    property var cpuHistory: []
    property var netHistory: []
    readonly property int historyLength: 60

    function push(list, v) {
        const out = list.concat([v])
        return out.length > historyLength ? out.slice(out.length - historyLength) : out
    }

    function rate(bytes) {
        const units = ["B", "kB", "MB", "GB"]
        let i = 0
        while (bytes >= 1000 && i < units.length - 1) { bytes /= 1000; i++ }
        return (i ? bytes.toFixed(1) : Math.round(bytes)) + units[i] + "/s"
    }

    // ---- sensor discovery (hwmon numbering changes between boots) ----
    property string gpuDev: ""          // /sys/class/drm/cardN/device
    property string gpuHwmon: ""
    property string cpuHwmon: ""
    property string cpuTempFile: ""

    Process {
        running: true
        command: ["sh", "-c",
            "for h in /sys/class/hwmon/hwmon*; do echo \"hwmon $h $(cat $h/name)\"; done;" +
            "for c in /sys/class/drm/card[0-9]; do [ -f $c/device/gpu_busy_percent ] && echo \"gpu $c/device\"; done;" +
            "for l in /sys/class/hwmon/hwmon*/temp*_label; do echo \"label $l $(cat $l)\"; done;" +
            "nproc; cat /proc/sys/kernel/osrelease /proc/sys/kernel/hostname"]
        stdout: StdioCollector {
            onStreamFinished: {
                const lines = text.trim().split("\n")
                const tail = lines.slice(-3)
                root.cpuThreads = +tail[0]; root.kernel = tail[1]; root.host = tail[2]
                for (const l of lines) {
                    const f = l.split(" ")
                    if (f[0] === "hwmon" && f[2] === "amdgpu" && !root.gpuHwmon) root.gpuHwmon = f[1]
                    if (f[0] === "hwmon" && (f[2] === "k10temp" || f[2] === "coretemp" || f[2] === "zenpower") && !root.cpuHwmon) root.cpuHwmon = f[1]
                    if (f[0] === "gpu" && !root.gpuDev) root.gpuDev = f[1]
                }
                // Tctl / Package id 0 / Tdie, else the first input
                let best = ""
                for (const l of lines) {
                    const f = l.split(" ")
                    if (f[0] !== "label" || !root.cpuHwmon || !f[1].startsWith(root.cpuHwmon)) continue
                    const name = f.slice(2).join(" ")
                    if (/Tctl|Tdie|Package id 0/.test(name) && !best) best = f[1].replace("_label", "_input")
                }
                root.cpuTempFile = best || (root.cpuHwmon ? root.cpuHwmon + "/temp1_input" : "")
            }
        }
    }

    // ---- readers ----
    component Sys: FileView { blockLoading: true; printErrors: false }
    function num(view) { view.reload(); return +view.text().trim() }

    Sys { id: stat; path: "/proc/stat" }
    Sys { id: meminfo; path: "/proc/meminfo" }
    Sys { id: cpuinfo; path: "/proc/cpuinfo" }
    Sys { id: uptimeFile; path: "/proc/uptime" }
    Sys { id: loadFile; path: "/proc/loadavg" }
    Sys { id: gpuBusy; path: root.gpuDev ? root.gpuDev + "/gpu_busy_percent" : "" }
    Sys { id: vramUsedFile; path: root.gpuDev ? root.gpuDev + "/mem_info_vram_used" : "" }
    Sys { id: vramTotalFile; path: root.gpuDev ? root.gpuDev + "/mem_info_vram_total" : "" }
    Sys { id: sclk; path: root.gpuDev ? root.gpuDev + "/pp_dpm_sclk" : "" }
    Sys { id: gpuTempFile; path: root.gpuHwmon ? root.gpuHwmon + "/temp1_input" : "" }
    Sys { id: cpuTempView; path: root.cpuTempFile }
    Sys { id: route; path: "/proc/net/route" }
    Sys { id: rx; path: root.netIf ? `/sys/class/net/${root.netIf}/statistics/rx_bytes` : "" }
    Sys { id: tx; path: root.netIf ? `/sys/class/net/${root.netIf}/statistics/tx_bytes` : "" }
    Sys { id: netType; path: root.netIf ? `/sys/class/net/${root.netIf}/uevent` : "" }

    property var cpuLast: null
    function readCpuRam() {
        stat.reload()
        const f = stat.text().split("\n")[0].trim().split(/\s+/).slice(1).map(Number)
        const idle = f[3] + f[4], total = f.reduce((a, b) => a + b, 0)
        if (cpuLast && total > cpuLast.total)
            cpu = Math.round(100 * (1 - (idle - cpuLast.idle) / (total - cpuLast.total)))
        cpuLast = { idle: idle, total: total }

        meminfo.reload()
        const m = meminfo.text()
        const kb = k => { const r = m.match(new RegExp("^" + k + ":\\s+(\\d+)", "m")); return r ? +r[1] : 0 }
        const t = kb("MemTotal"), used = t - kb("MemAvailable")
        if (t) { ram = Math.round(100 * used / t); ramUsed = used / 1048576; ramTotal = t / 1048576 }
    }

    function readGpu() {
        if (gpuDev) {
            gpu = num(gpuBusy)
            vramUsed = num(vramUsedFile) / 1073741824
            vramTotal = num(vramTotalFile) / 1073741824
        }
        if (gpuHwmon) gpuTemp = Math.round(num(gpuTempFile) / 1000)
        if (cpuTempFile) cpuTemp = Math.round(num(cpuTempView) / 1000)
    }

    function readDetail() {
        cpuinfo.reload()
        const mhz = cpuinfo.text().match(/^cpu MHz\s*:\s*([\d.]+)/gm) || []
        if (mhz.length) cpuGhz = mhz.reduce((a, l) => a + parseFloat(l.split(":")[1]), 0) / mhz.length / 1000
        if (gpuDev) {
            sclk.reload()
            const cur = sclk.text().split("\n").find(l => l.includes("*"))
            gpuMhz = cur ? parseInt(cur.split(":")[1]) : 0
        }
        uptimeFile.reload()
        const s = Math.floor(parseFloat(uptimeFile.text()))
        const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), mi = Math.floor(s % 3600 / 60)
        uptime = (d ? d + "d " : "") + (d || h ? h + "h " : "") + mi + "m"
        loadFile.reload()
        load = loadFile.text().split(" ").slice(0, 3).join("  ")
    }

    property var netLast: null
    function readNet() {
        route.reload()
        const r = route.text().split("\n").slice(1).map(l => l.trim().split(/\s+/))
            .find(f => f[1] === "00000000" && (parseInt(f[3], 16) & 1))
        const iface = r ? r[0] : ""
        if (iface !== netIf) { netIf = iface; netLast = null; netIp = "" }
        netUp = iface !== ""
        if (!netUp) { netDown = 0; netUpRate = 0; return }
        netWifi = (netType.reload(), netType.text().includes("DEVTYPE=wlan"))
        const now = Date.now(), down = num(rx), up = num(tx)
        if (netLast) {
            const sec = (now - netLast.t) / 1000
            netDown = Math.max(0, (down - netLast.rx) / sec)
            netUpRate = Math.max(0, (up - netLast.tx) / sec)
        }
        netLast = { rx: down, tx: up, t: now }
        if (!netIp) ipProc.running = true
    }

    Process {
        id: ipProc
        command: ["ip", "-4", "-o", "addr", "show", "dev", root.netIf]
        stdout: StdioCollector {
            onStreamFinished: { const r = text.match(/inet ([0-9.]+)/); root.netIp = r ? r[1] : "" }
        }
    }

    Process {
        id: df
        command: ["df", "-B1", "--output=used,size", "/"]
        stdout: StdioCollector {
            onStreamFinished: {
                const f = text.trim().split("\n").pop().trim().split(/\s+/).map(Number)
                root.diskUsed = f[0] / 1073741824; root.diskTotal = f[1] / 1073741824
            }
        }
    }

    // ---- clocks ----
    // bar only: cpu/ram every 2s, gpu/temps every 3s, network every 5s.
    // panel open: everything every 1.5s so the gauges feel live.
    Timer {
        interval: root.detailed ? 1500 : 2000
        repeat: true; triggeredOnStart: true
        running: root.wantCpuRam
        onTriggered: {
            root.readCpuRam()
            if (root.detailed) root.cpuHistory = root.push(root.cpuHistory, root.cpu)
        }
    }
    Timer {
        interval: root.detailed ? 1500 : 3000
        repeat: true; triggeredOnStart: true
        running: root.wantGpu && (root.gpuDev !== "" || root.cpuTempFile !== "")
        onTriggered: root.readGpu()
    }
    Timer {
        interval: root.detailed ? 1500 : 5000
        repeat: true; triggeredOnStart: true
        running: root.wantNet
        onTriggered: {
            root.readNet()
            if (root.detailed) root.netHistory = root.push(root.netHistory, root.netDown)
        }
    }
    Timer {
        interval: 1500
        repeat: true; triggeredOnStart: true
        running: root.detailed
        onTriggered: root.readDetail()
    }
    Timer {
        interval: 30000
        repeat: true; triggeredOnStart: true
        running: root.detailed
        onTriggered: df.running = true
    }
}
