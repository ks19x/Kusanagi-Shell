pragma Singleton
// Recorder.qml — screen recording, replay buffer and streaming through `kusanagi record` (scripts/record):
// gsr-ui when it runs, else gpu-screen-recorder directly, else wf-recorder (recording only). Any distro.
// State comes from $XDG_RUNTIME_DIR/kusanagi/record.json, which the script rewrites after every action
// (watched — no polling); recorders started elsewhere (gsr-ui hotkeys) are picked up by refresh(),
// run when the panel opens and every 10 s only while a bar shows the recorder module.
// Settings: Config.recorder (Settings → Game mode → Recording).
import Quickshell
import Quickshell.Io
import QtQuick

Singleton {
    id: root

    property string mode: "off"           // off | replay | record | stream
    property string backend: "none"       // gsr-ui | gsr | wf-recorder | none
    property real since: 0                // epoch seconds the current one started
    property var available: []            // what this system can do: replay record stream
    readonly property bool active: mode !== "off"

    // elapsed "12:34" (only ticks while something records and someone shows it)
    property real now: Date.now() / 1000
    readonly property string elapsed: {
        if (!active || since <= 0) return ""
        const s = Math.max(0, Math.floor(now - since)), h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60
        const pad = n => (n < 10 ? "0" : "") + n
        return (h ? h + ":" + pad(m) : m) + ":" + pad(s % 60)
    }
    property int watchers: 0               // bar modules showing the time
    Timer { interval: 1000; repeat: true; running: root.active && root.watchers > 0; onTriggered: root.now = Date.now() / 1000 }
    Timer { interval: 10000; repeat: true; running: root.watchers > 0 && !GameMode.quiet; onTriggered: root.refresh() }

    readonly property string stateFile: (Quickshell.env("XDG_RUNTIME_DIR") || "/tmp") + "/kusanagi/record.json"
    FileView {
        id: file
        path: root.stateFile
        watchChanges: true
        printErrors: false
        onFileChanged: reload()
        onLoaded: root.parse(text())
    }
    function parse(t) {
        try {
            const j = JSON.parse(t)
            mode = j.mode || "off"; backend = j.backend || "none"; since = j.since || 0
            available = j.available || []
            now = Date.now() / 1000
        } catch (e) {}
    }

    Process {
        id: statusProc
        command: ["kusanagi", "record", "status"]
        stdout: StdioCollector { onStreamFinished: { root.parse(text); file.reload() } }
    }
    function refresh() { if (!statusProc.running) statusProc.running = true }
    Component.onCompleted: refresh()

    function env() {
        const r = Config.recorder
        return ["env", "KR_FOLDER=" + r.folder.replace(/^~/, Quickshell.env("HOME")), "KR_FPS=" + r.fps, "KR_QUALITY=" + r.quality,
                "KR_REPLAY=" + r.replay, "KR_AUDIO=" + r.audio, "KR_CAPTURE=" + r.capture, "KR_CODEC=" + r.codec,
                "KR_STREAM_URL=" + r.streamUrl]
    }
    function run(what) {
        Quickshell.execDetached(env().concat(["kusanagi", "record", what]))
        settle.restart()
    }
    // the script writes record.json itself; this catches backends that take a moment (gsr-ui)
    Timer { id: settle; interval: 1500; onTriggered: root.refresh() }

    function replay() { run("replay") }        // toggles
    function save() { run("save") }            // the last Config.recorder.replay seconds → a clip
    function record() { run("record") }        // toggles
    function stream() { run("stream") }        // toggles
    function stop() { run("stop") }
    // one button: replay → save a clip, recording/stream → stop, off → start recording
    function smart() { if (mode === "replay") save(); else if (active) stop(); else record() }
}
