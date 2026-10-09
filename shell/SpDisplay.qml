// SpDisplay.qml — monitors (from the compositor), brightness, window gaps / borders and night light
import Quickshell
import Quickshell.Io
import QtQuick

Column {
    id: page
    spacing: 22

    property var monitors: []          // [{ name, w, h, hz, scale, x, y }]
    Process {
        running: Wm.monitorsCommand.length > 0
        command: Wm.monitorsCommand
        stdout: StdioCollector { onStreamFinished: page.monitors = Wm.parseMonitors(text) }
    }

    // gammastep running?
    property bool night: false
    Process { id: nightCheck; running: true; command: ["pgrep", "-x", "gammastep"]; onExited: code => page.night = code === 0 }
    function setNight(on) {
        Quickshell.execDetached(["sh", "-c", on
            ? `pkill -x gammastep; sleep 0.2; setsid -f gammastep -O ${Config.display.nightTemp} >/dev/null 2>&1`
            : "pkill -x gammastep"])
        night = on
    }
    Timer { id: retemp; interval: 400; onTriggered: if (page.night) page.setNight(true) }

    SpGroup {
        title: "Monitors"
        Repeater {
            model: page.monitors.length ? page.monitors : Quickshell.screens.map(s => ({ name: s.name, w: s.width, h: s.height, hz: 0, scale: s.devicePixelRatio, x: s.x, y: s.y }))
            Item {
                required property var modelData
                width: parent.width; height: 52
                CpIcon { id: mi; anchors.verticalCenter: parent.verticalCenter; cp: 0xf0379; font.pixelSize: 26; color: Theme.accent }
                Column {
                    anchors { left: mi.right; leftMargin: 14; verticalCenter: parent.verticalCenter }
                    CpText { text: modelData.name; font.pixelSize: 13; font.bold: true }
                    CpText {
                        text: `${modelData.w} × ${modelData.h}${modelData.hz ? "  @ " + Number(modelData.hz).toFixed(2) + " Hz" : ""}   ·   scale ${modelData.scale}   ·   at ${modelData.x}, ${modelData.y}`
                        font.pixelSize: 11; color: Theme.textDim
                    }
                }
            }
        }
        CpChip {
            visible: Wm.configFile !== ""
            label: "Edit " + Wm.name + " config"
            icon: 0xf107b
            onClicked: Quickshell.execDetached(["xdg-open", Wm.configFile])
        }
    }

    SpGroup {
        title: "Brightness"
        hint: "The screens' own backlight — a laptop panel, and monitors over their cable (DDC/CI)."
        Component.onCompleted: Brightness.refresh()
        Repeater {
            model: Brightness.displays
            CpSlider {
                required property var modelData
                width: parent.width; height: 30
                readonly property real level: Brightness.value(modelData.key)
                icon: level < 0.34 ? 0xf00dd : level < 0.67 ? 0xf00de : 0xf00df
                label: modelData.name + (modelData.screen ? "  ·  " + modelData.screen : "")
                value: level
                onMoved: v => Brightness.set(modelData.key, v)
            }
        }
        CpText {
            readonly property string why: ({
                missing: "Monitors: ddcutil isn't installed — it's what talks to them over the cable.",
                noi2c: "Monitors: the i2c-dev kernel module isn't loaded, so ddcutil can't reach them.",
                noaccess: "Monitors: no permission for /dev/i2c-* yet — after the setup, log out and back in.",
                none: "No monitor answered over DDC/CI — switch DDC/CI on in the monitor's own menu, then look again."
            })[Brightness.ddcState] ?? ""
            visible: text !== ""
            width: parent.width
            wrapMode: Text.Wrap
            text: Brightness.detecting && !Brightness.available ? "Looking for screens…" : why
            font.pixelSize: 12
            color: Theme.textDim
        }
        CpRow {
            width: parent.width; label: "Monitors over DDC/CI"
            hint: "External monitors via ddcutil. A change takes a moment to reach them."
            CpSwitch { on: Config.brightness.ddc; onToggled: v => Config.brightness.ddc = v }
        }
        CpRow {
            width: parent.width; label: "Step"
            hint: "Per brightness key press or bar scroll."
            CpStepper { value: Config.brightness.step; from: 1; to: 25; suffix: "%"; onChanged: v => Config.brightness.step = v }
        }
        Row {
            spacing: 8
            CpChip {
                visible: Config.brightness.ddc && ["missing", "noi2c", "noaccess"].includes(Brightness.ddcState)
                on: true
                label: "Set up monitor brightness"
                icon: 0xf0493
                onClicked: Quickshell.execDetached(["kusanagi", "brightness", "setup"])
            }
            CpChip { label: Brightness.detecting ? "Looking…" : "Look again"; icon: 0xf0450; onClicked: Brightness.detect() }
        }
    }

    SpGroup {
        visible: Wm.canSetLayout
        title: "Windows"
        hint: "Gaps and borders, applied to " + Wm.name + " as you drag."
        CpRow {
            width: parent.width; label: "Set from Kusanagi"
            hint: Config.windows.override ? "Overrides your " + Wm.name + " config." : "Off: your " + Wm.name + " config decides."
            CpSwitch {
                on: Config.windows.override
                onToggled: v => {
                    // start the sliders from what the compositor uses now
                    if (v) { Config.windows.gapsIn = Wm.layout.gapsIn; Config.windows.gapsOut = Wm.layout.gapsOut; Config.windows.border = Wm.layout.border }
                    Config.windows.override = v
                }
            }
        }
        Column {
            width: parent.width; spacing: 8
            enabled: Config.windows.override
            opacity: enabled ? 1 : 0.4
            CpSlider {
                width: parent.width; height: 30
                icon: 0xf084e; label: "Between windows"
                value: Config.windows.gapsIn / 40; step: 1 / 40
                valueText: Config.windows.gapsIn + "px"
                onMoved: v => Config.windows.gapsIn = Math.round(v * 40)
            }
            CpSlider {
                width: parent.width; height: 30
                icon: 0xf0293; label: "Screen edges"
                value: Config.windows.gapsOut / 60; step: 1 / 60
                valueText: Config.windows.gapsOut + "px"
                onMoved: v => Config.windows.gapsOut = Math.round(v * 60)
            }
            CpSlider {
                width: parent.width; height: 30
                icon: 0xf01fd; label: "Border"
                value: Config.windows.border / 10; step: 1 / 10
                valueText: Config.windows.border + "px"
                onMoved: v => Config.windows.border = Math.round(v * 10)
            }
        }
    }

    SpGroup {
        title: "Night light"
        hint: "Warmer colours for the evening (gammastep)."
        CpRow { width: parent.width; label: "On"; CpSwitch { on: page.night; onToggled: v => page.setNight(v) } }
        CpSlider {
            width: parent.width; height: 30
            icon: 0xf0594; label: "Warmth"
            value: (6500 - Config.display.nightTemp) / 4000
            valueText: Config.display.nightTemp + "K"
            onMoved: v => { Config.display.nightTemp = Math.round((6500 - v * 4000) / 100) * 100; retemp.restart() }
        }
    }
}
