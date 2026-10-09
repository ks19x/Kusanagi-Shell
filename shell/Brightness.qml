pragma Singleton
// Brightness.qml — screen brightness: laptop panels (/sys/class/backlight, via brightnessctl or the file) and
// external monitors over DDC/CI (ddcutil, Config.brightness.ddc). Nothing keeps running: one detect a few
// seconds after start (and when monitors change), values re-read when a panel / page asks (refresh()), and
// writes go out one at a time — a slider drag only ever has one ddcutil in flight, always with the newest value.
// `kusanagi brightness setup` installs ddcutil and the i2c access it needs. IPC: brightness up|down|set|get.
import Quickshell
import Quickshell.Io
import QtQuick

Singleton {
    id: root

    // [{ key, kind: "backlight"|"ddc", dev, bus, max, name, screen }] — replaced only when re-detected, so
    // Repeaters over it keep their delegates (a slider mid-drag survives); the levels live in `values`
    property var displays: []
    property var values: ({})               // key → 0..1
    readonly property bool available: displays.length > 0
    // one number for the bar / OSD: the average
    readonly property real level: available ? displays.reduce((a, d) => a + (values[d.key] ?? 0), 0) / displays.length : 0
    function value(key) { return values[key] ?? 0 }
    // why there are no DDC monitors: "" fine · off · missing (no ddcutil) · noi2c (i2c-dev not loaded) · noaccess · none
    property string ddcState: ""
    property bool detecting: false
    property bool detected: false
    signal adjusted()                       // the user changed it (keys, scroll, sliders): the OSD shows

    function find(key) { return displays.find(d => d.key === key) ?? null }
    function update(key, v) {
        const o = Object.assign({}, values)
        o[key] = v
        values = o
    }

    // ---- changing it ----
    function set(key, v) {
        const d = find(key)
        if (!d) return
        v = Math.max(d.kind === "backlight" ? 0.01 : 0, Math.min(1, v))      // a black laptop panel is hard to undo
        update(key, v)
        touched[key] = Date.now()
        pending[key] = Math.round(v * d.max)
        flush()
        adjusted()
    }
    function setAll(v) { for (const d of displays) set(d.key, v) }
    // dir +1 / -1, by step % (default Config.brightness.step), every display
    function change(dir, step) {
        const s = (step || Config.brightness.step) / 100
        for (const d of displays) set(d.key, Math.round((value(d.key) + dir * s) * 100) / 100)
        if (!available) ensure()
    }
    function ensure() { if (!detected && !detecting) detect() }

    property var pending: ({})              // key → raw value waiting to be written
    property var touched: ({})              // key → when the user last set it (reads don't fight a drag)
    function flush() {
        if (writer.running) return
        const key = Object.keys(pending)[0]
        if (key === undefined) return
        const d = find(key), n = pending[key]
        delete pending[key]
        if (!d) { flush(); return }
        writer.command = d.kind === "ddc"
            ? ["ddcutil", "--bus", String(d.bus), "--noverify", "setvcp", "10", String(n)]
            : ["sh", "-c", 'brightnessctl -q -d "$1" set "$2" >/dev/null 2>&1 || echo "$2" > "/sys/class/backlight/$1/brightness"', "sh", d.dev, String(n)]
        writer.running = true
    }
    Process { id: writer; onExited: root.flush() }

    // ---- finding them (scripts/brightness.sh) ----
    readonly property string helper: Quickshell.shellDir + "/scripts/brightness.sh"

    function parse(text) {
        const list = [], vals = {}
        let ddc = ""
        for (const l of text.split("\n")) {
            const f = l.trim().split("|")
            if (f[0] === "bl" && +f[3] > 0) {
                const internal = Quickshell.screens.find(s => /^(eDP|LVDS|DSI)/.test(s.name))
                list.push({ key: "bl:" + f[1], kind: "backlight", dev: f[1], bus: -1, max: +f[3], value: +f[2] / +f[3],
                            name: "Built-in display", screen: internal ? internal.name : "" })
            } else if (f[0] === "mon") {
                list.push({ key: "ddc:" + f[1], kind: "ddc", dev: "", bus: +f[1], max: 100, value: -1,
                            name: f[3] || f[2] || "Monitor " + f[1], screen: f[2] || "" })
            } else if (f[0] === "val") vals["ddc:" + f[1]] = { cur: +f[2], max: +f[3] }
            else if (f[0] === "ddc") ddc = f[1]
        }
        for (const d of list) if (d.kind === "ddc") {
            const v = vals[d.key]
            if (v && v.max > 0) { d.max = v.max; d.value = v.cur / v.max }
        }
        // monitors that answered detect but not getvcp can't be dimmed: leave them out
        const usable = list.filter(d => d.value >= 0)
        // a value the user set a moment ago wins over what the hardware said before the write landed
        const vals2 = {}
        for (const d of usable) {
            vals2[d.key] = values[d.key] !== undefined && Date.now() - (touched[d.key] || 0) < 2500 ? values[d.key] : d.value
            delete d.value
        }
        values = vals2
        // same monitors as before: keep the array (and every delegate built from it)
        if (JSON.stringify(usable) !== JSON.stringify(displays)) displays = usable
        ddcState = ddc === "ok" ? (usable.some(d => d.kind === "ddc") ? "" : "none") : ddc
        detected = true
    }

    Process {
        id: finder
        command: ["sh", root.helper, "detect", Config.brightness.ddc ? "1" : "0"]
        stdout: StdioCollector { onStreamFinished: root.parse(text) }
        onExited: root.detecting = false
    }
    function detect() {
        if (finder.running) return
        detecting = true
        finder.running = true
    }
    // re-read the levels (monitor buttons, other tools): cheap for backlights, ~50 ms per DDC monitor
    function refresh() {
        if (!detected) { ensure(); return }
        if (writer.running || reader.running || !available) return
        reader.command = ["sh", root.helper, "read"].concat(displays.map(d => d.kind === "ddc" ? String(d.bus) : "bl:" + d.dev))
        reader.running = true
    }
    Process {
        id: reader
        stdout: StdioCollector {
            onStreamFinished: {
                for (const l of text.split("\n")) {
                    const f = l.trim().split("|")
                    if (f.length < 4 || !(+f[3] > 0)) continue
                    const key = (f[0] === "bl" ? "bl:" : "ddc:") + f[1]
                    if (Date.now() - (root.touched[key] || 0) < 2500 || root.pending[key] !== undefined) continue
                    root.update(key, +f[2] / +f[3])
                }
            }
        }
    }

    // first look a few seconds after login (ddcutil takes a moment; don't compete with startup)
    Timer { interval: 4000; running: true; onTriggered: root.detect() }
    // monitors plugged / unplugged, or DDC switched on/off
    Timer { id: redetect; interval: 2500; onTriggered: root.detect() }
    Connections { target: Quickshell; function onScreensChanged() { if (root.detected) redetect.restart() } }
    Connections { target: Config.brightness; function onDdcChanged() { redetect.restart() } }
}
