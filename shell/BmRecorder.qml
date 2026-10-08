// BmRecorder.qml — recording / replay / stream status (shown while one runs; always: true = also when off).
// Vars: time (elapsed), mode, backend. Status: off replay record stream. Click: replay → save a clip,
// recording → stop, off → start recording; right click: stop.
import QtQuick

Item {
    id: p
    property var m
    Component.onCompleted: Recorder.watchers++
    Component.onDestruction: Recorder.watchers--
    readonly property var vars: ({ time: Recorder.elapsed, mode: Recorder.mode, backend: Recorder.backend })
    readonly property string status: Recorder.mode
    readonly property var icons: ({ off: String.fromCodePoint(0xf044a), replay: String.fromCodePoint(0xf0450),
                                    record: '<font color="danger">' + String.fromCodePoint(0xf044a) + '</font>',
                                    stream: '<font color="danger">' + String.fromCodePoint(0xf0567) + '</font>' })
    readonly property string format: "{icon} {time}"
    readonly property bool shown: Recorder.active || (m && m.eff.always === true)
    readonly property var actions: ({ click: "record:smart", rightClick: "record:stop" })
    readonly property string tooltip: Recorder.mode === "replay" ? "Replay buffer on — click to save the last " + Config.recorder.replay + " s"
        : Recorder.mode === "record" ? "Recording " + Recorder.elapsed + " — click to stop"
        : Recorder.mode === "stream" ? "Streaming " + Recorder.elapsed + " — click to stop"
        : "Not recording — click to start"
}
