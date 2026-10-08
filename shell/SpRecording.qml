// SpRecording.qml — screen recording, the replay buffer and streaming (Recorder.qml → `kusanagi record`)
import Quickshell
import QtQuick

Column {
    id: page
    spacing: 22
    Component.onCompleted: Recorder.refresh()
    readonly property bool can: Recorder.available.length > 0
    function has(what) { return Recorder.available.indexOf(what) >= 0 }

    SpGroup {
        title: "Now"
        hint: !page.can ? "No recorder found. Install gpu-screen-recorder (recording, replays, streaming) or wf-recorder (recording) — `kusanagi doctor` prints the command for your distro."
            : "Using " + (({ "gsr-ui": "GPU Screen Recorder (gsr-ui)", gsr: "gpu-screen-recorder", "wf-recorder": "wf-recorder" })[Recorder.backend] || "gpu-screen-recorder / wf-recorder, whichever is installed") + ". Add the Recorder module to the bar or the Record / Replay / Save clip tiles to the control panel."
        Row {
            spacing: 14
            Rectangle {
                width: 46; height: 46; radius: 23
                color: Recorder.active ? (Recorder.mode === "replay" ? Theme.accent : Theme.danger) : Theme.alpha(Theme.text, 0.08)
                Behavior on color { ColorAnimation { duration: Config.ms(200) } }
                CpIcon { anchors.centerIn: parent; cp: Recorder.mode === "replay" ? 0xf0450 : Recorder.mode === "stream" ? 0xf0567 : 0xf044a; font.pixelSize: 22; color: Recorder.active ? Theme.bgPanel : Theme.textDim }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                CpText { text: ({ off: "Not recording", replay: "Replay buffer on", record: "Recording", stream: "Streaming" })[Recorder.mode] || Recorder.mode; font.pixelSize: 14; font.bold: true }
                CpText { text: Recorder.active && Recorder.mode !== "replay" ? Recorder.elapsed : Recorder.mode === "replay" ? "keeps the last " + Config.recorder.replay + " s" : ""; font.pixelSize: 11; color: Theme.textDim }
            }
        }
        Flow {
            width: parent.width
            spacing: 8
            CpChip { visible: page.has("record"); label: Recorder.mode === "record" ? "Stop recording" : "Record"; icon: 0xf044a; on: Recorder.mode === "record"; onClicked: Recorder.record() }
            CpChip { visible: page.has("replay"); label: Recorder.mode === "replay" ? "Stop replay" : "Start replay"; icon: 0xf0450; on: Recorder.mode === "replay"; onClicked: Recorder.replay() }
            CpChip { visible: Recorder.mode === "replay"; label: "Save clip"; icon: 0xf0fd8; onClicked: Recorder.save() }
            CpChip { visible: page.has("stream"); label: Recorder.mode === "stream" ? "Stop stream" : "Stream"; icon: 0xf0567; on: Recorder.mode === "stream"; onClicked: Recorder.stream() }
            CpChip { label: "Open folder"; icon: 0xf024b; onClicked: Quickshell.execDetached(["xdg-open", Config.recorder.folder.replace(/^~/, Quickshell.env("HOME"))]) }
        }
    }

    SpGroup {
        title: "Video"
        hint: "Used when Kusanagi runs the recorder itself. With gsr-ui running, its own settings apply."
        CpRow {
            width: parent.width; label: "Frame rate"
            CpSegmented { width: 300; current: Config.recorder.fps; options: [{ label: "30", value: 30 }, { label: "60", value: 60 }, { label: "120", value: 120 }, { label: "144", value: 144 }]; onPicked: v => Config.recorder.fps = v }
        }
        CpRow {
            width: parent.width; label: "Quality"
            CpSegmented { width: 300; current: Config.recorder.quality; options: [{ label: "Medium", value: "medium" }, { label: "High", value: "high" }, { label: "Very high", value: "very_high" }, { label: "Ultra", value: "ultra" }]; onPicked: v => Config.recorder.quality = v }
        }
        CpRow {
            width: parent.width; label: "Codec"; hint: "older AMD cards (RX 400/500): HEVC"
            CpSegmented { width: 300; current: Config.recorder.codec; options: [{ label: "Auto", value: "auto" }, { label: "H.264", value: "h264" }, { label: "HEVC", value: "hevc" }, { label: "AV1", value: "av1" }]; onPicked: v => Config.recorder.codec = v }
        }
        CpRow {
            width: parent.width; label: "Capture"; hint: "screen = no prompts (needs gsr's KMS helper); portal asks once"
            CpSegmented { width: 300; current: Config.recorder.capture === "portal" ? "portal" : "screen"; options: [{ label: "Screen", value: "screen" }, { label: "Portal", value: "portal" }]; onPicked: v => Config.recorder.capture = v }
        }
        CpRow {
            width: parent.width; label: "Sound"
            CpSegmented { width: 300; current: Config.recorder.audio; options: [{ label: "Desktop", value: "desktop" }, { label: "Mic", value: "mic" }, { label: "Both", value: "both" }, { label: "None", value: "none" }]; onPicked: v => Config.recorder.audio = v }
        }
    }

    SpGroup {
        title: "Replay and files"
        CpRow {
            width: parent.width; label: "Replay keeps the last"
            CpSegmented { width: 300; current: Config.recorder.replay; options: [{ label: "15 s", value: 15 }, { label: "30 s", value: 30 }, { label: "60 s", value: 60 }, { label: "2 min", value: 120 }]; onPicked: v => Config.recorder.replay = v }
        }
        CpRow {
            width: parent.width; label: "Folder"; hint: "replays go in Replays/ inside it"
            CpField { width: 260; text: Config.recorder.folder; onEdited: t => Config.recorder.folder = t }
        }
        CpRow {
            width: parent.width; label: "Stream to"; hint: "rtmp://… with your key (Twitch, YouTube…)"
            CpField { width: 260; text: Config.recorder.streamUrl; placeholder: "rtmp://"; onEdited: t => Config.recorder.streamUrl = t }
        }
    }
}
