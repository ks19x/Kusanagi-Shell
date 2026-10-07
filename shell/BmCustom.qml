// BmCustom.qml — your own module from a command (like waybar's custom/…):
//   exec "cmd"     run with sh -c; output = the text, or JSON { text, tooltip, class, percentage, alt }
//   interval s     re-run every s seconds (0 = once); "stream": true = keep it running, every line updates
//   refresh true   re-run right after a click / scroll action
//   game false     keep running in game mode (default: paused while Kusanagi is quiet)
// Vars: text, alt, percentage + every JSON field. Status: the JSON class. Level: percentage.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import Quickshell
import Quickshell.Io
import QtQuick

Item {
    id: p
    property var m
    readonly property var o: m ? m.eff : ({})
    readonly property string cmd: o.exec || ""
    readonly property bool stream: o.stream === true
    readonly property real every: o.interval !== undefined ? +o.interval : 5
    readonly property bool paused: GameMode.quiet && o.game !== true

    property var data: ({ text: "" })
    function take(line) {
        const t = line.trim()
        if (t.startsWith("{")) {
            try { const j = JSON.parse(t); data = Object.assign({ text: "" }, j); return } catch (e) {}
        }
        data = { text: t }
    }
    readonly property var vars: data
    readonly property real level: data.percentage !== undefined ? +data.percentage : -1
    readonly property string status: Array.isArray(data["class"]) ? data["class"].join(" ") : (data["class"] || "")
    readonly property string format: "{text}"
    readonly property string tooltip: data.tooltip !== undefined ? String(data.tooltip) : ""
    readonly property bool shown: cmd !== ""
    function acted() { if (o.refresh && !stream) kick() }
    function kick() { if (cmd !== "" && !paused && m) proc.running = true }
    onCmdChanged: { proc.running = false; Qt.callLater(kick) }
    onPausedChanged: if (paused) { if (stream) proc.running = false } else kick()

    Process {
        id: proc
        command: ["sh", "-c", p.cmd]
        // stream: every line updates; one-shot: the last non-empty line wins
        stdout: p.stream ? lines : collect
        onExited: if (p.stream && !p.paused) restart.start()
    }
    SplitParser { id: lines; onRead: line => p.take(line) }
    StdioCollector {
        id: collect
        onStreamFinished: { const l = text.trim().split("\n").filter(s => s.trim()); p.take(l.length ? l[l.length - 1] : "") }
    }
    Timer {
        interval: Math.max(250, p.every * 1000)
        repeat: true
        running: !p.stream && p.every > 0 && p.cmd !== "" && !p.paused
        onTriggered: if (!proc.running) p.kick()
    }
    Timer { id: restart; interval: 2000; onTriggered: p.kick() }
}
