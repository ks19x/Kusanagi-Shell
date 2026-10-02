// SpDisplay.qml — monitors (from the compositor) and night light
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
