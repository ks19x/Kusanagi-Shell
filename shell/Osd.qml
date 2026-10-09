// Osd.qml — one on-screen pill for volume, mic, brightness and game mode (replaces VolumeOsd + GameModeOsd).
// Springs in at Config.osd.position (top | bottom | left | right), morphs between kinds while it's up,
// Config.osd.style: pill · minimal (a slim strip) · box (a square in the lower middle, macOS-like);
// and is unmapped entirely when idle. Click-through.
import Quickshell
import Quickshell.Wayland
import Quickshell.Services.Pipewire
import QtQuick
import QtQuick.Effects

PanelWindow {
    id: root

    readonly property string pos: Config.osd.position
    readonly property bool box: Config.osd.style === "box"
    readonly property bool vertical: !box && (pos === "right" || pos === "left")

    // box: the whole screen (click-through), the square sits in the lower middle
    anchors {
        top: box || pos !== "bottom"
        bottom: box || pos !== "top"
        left: box || !vertical || pos === "left"
        right: box || !vertical || pos === "right"
    }
    implicitHeight: box ? 0 : vertical ? 0 : 110
    implicitWidth: box ? 0 : vertical ? 110 : 0
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "quickshell-osd"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    mask: Region {}
    visible: showing || pill.opacity > 0.01

    // ---- what to show ----
    property bool showing: false
    property string kind: "volume"          // volume | mic | brightness | game
    property int icon: 0
    property string label: ""
    property real value: -1                 // 0..1, or -1 for no bar
    property bool dim: false                // muted / off

    function show(k) {
        if (!armed.done) return
        kind = k
        showing = true
        hide.restart()
    }
    // settings page / IPC: show it without touching anything
    function preview(k) { kind = k || "volume"; showing = true; hide.restart() }
    Timer { id: hide; interval: Config.osd.timeout; onTriggered: root.showing = false }
    // ignore the burst of property changes while PipeWire connects at startup
    Timer { id: armed; property bool done: false; interval: 2000; running: true; onTriggered: done = true }

    PwObjectTracker { objects: [Pipewire.defaultAudioSink, Pipewire.defaultAudioSource].filter(n => n) }
    readonly property var sinkAudio: Pipewire.defaultAudioSink ? Pipewire.defaultAudioSink.audio : null
    readonly property var sourceAudio: Pipewire.defaultAudioSource ? Pipewire.defaultAudioSource.audio : null

    Connections {
        target: Config.osd.volume ? root.sinkAudio : null
        function onVolumeChanged() { root.show("volume") }
        function onMutedChanged() { root.show("volume") }
    }
    Connections {
        target: Config.osd.mic ? root.sourceAudio : null
        function onMutedChanged() { root.show("mic") }
        function onVolumeChanged() { root.show("mic") }
    }
    Connections {
        target: Config.osd.brightness ? Brightness : null
        function onAdjusted() { root.show("brightness") }
    }
    Connections {
        target: Config.osd.gamemode ? GameMode : null
        function onChanged(byHand) {
            const a = Config.gamemode.announce
            if (a === "always" || (a === "manual" && byHand)) root.show("game")
        }
    }

    readonly property var view: {
        if (kind === "volume" && sinkAudio) {
            const v = sinkAudio.volume, m = sinkAudio.muted
            return { icon: m || v === 0 ? 0xf0581 : v < 0.34 ? 0xf057f : v < 0.67 ? 0xf0580 : 0xf057e,
                     label: m ? "Muted" : "Volume", value: v, dim: m }
        }
        if (kind === "mic" && sourceAudio) {
            const m = sourceAudio.muted
            return { icon: m ? 0xf036d : 0xf036c, label: m ? "Mic muted" : "Microphone", value: sourceAudio.volume, dim: m }
        }
        if (kind === "brightness") {
            const v = Brightness.available ? Brightness.level : 0.7       // (a preview with nothing to dim)
            return { icon: v < 0.34 ? 0xf00dd : v < 0.67 ? 0xf00de : 0xf00df, label: "Brightness", value: v, dim: false }
        }
        return { icon: 0xf0297, label: GameMode.active ? "Game mode on" : "Game mode off", value: -1, dim: !GameMode.active }
    }

    // ---- the pill ----
    Item {
        id: holder
        anchors.fill: parent

        RectangularShadow {

            visible: Config.look.shadows
            anchors.fill: pill
            opacity: pill.opacity
            scale: pill.scale
            blur: 24
            offset.y: 6
            radius: pill.radius
            color: Theme.alpha("#000000", 0.4)
        }

        Rectangle {
            id: pill

            readonly property bool hasBar: root.view.value >= 0
            // minimal: a slim strip — small icon, the bar, (the number)
            readonly property bool minimal: Config.osd.style === "minimal"
            width: root.box ? 196 : root.vertical ? (minimal ? 34 : 52) : (minimal ? row.implicitWidth + 28 : hasBar ? row.implicitWidth + 24 : row.implicitWidth + 40)
            height: root.box ? 196 : root.vertical ? (hasBar ? (minimal ? 200 : 240) : 52) : (minimal ? 30 : 46)
            radius: root.box ? Math.max(18, Config.look.radius + 6) : Math.min(width, height) / 2
            color: Theme.alpha(Theme.bgPanel, Math.max(0.88, Config.panel.opacity))
            border.width: Theme.surfaceBorderWidth
            border.color: Theme.surfaceBorder
            Behavior on width { NumberAnimation { duration: Config.ms(280); easing.type: Easing.OutQuint } }

            // slides in from its edge with a little spring, fades/shrinks out quicker
            property real slide: root.showing ? 0 : 18
            x: root.box || !root.vertical ? Math.round((holder.width - width) / 2)
             : root.pos === "left" ? 18 - slide : holder.width - width - 18 + slide
            y: root.box ? Math.round(holder.height * 0.68 - height / 2) + slide / 2
             : root.vertical ? Math.round((holder.height - height) / 2)
             : root.pos === "top" ? 40 - slide : holder.height - height - 30 + slide
            opacity: root.showing ? 1 : 0
            scale: root.showing ? 1 : 0.88
            Behavior on slide { NumberAnimation { duration: Config.ms(root.showing ? 420 : 200); easing.type: root.showing ? Easing.OutBack : Easing.InCubic; easing.overshoot: Config.bounce(1.4) } }
            Behavior on opacity { NumberAnimation { duration: Config.ms(root.showing ? 200 : 180) } }
            Behavior on scale { NumberAnimation { duration: Config.ms(root.showing ? 420 : 200); easing.type: root.showing ? Easing.OutBack : Easing.InCubic; easing.overshoot: Config.bounce(1.6) } }

            // ---- horizontal layout ----
            // ---- box: big icon, the name, a segmented level ----
            Column {
                visible: root.box
                anchors.centerIn: parent
                spacing: 14
                CpIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    cp: root.view.icon
                    font.pixelSize: 64
                    color: root.view.dim ? Theme.textDim : Theme.text
                }
                CpText {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.view.label
                    font.pixelSize: 12
                    font.bold: true
                    color: root.view.dim ? Theme.textDim : Theme.text
                }
                Row {
                    visible: pill.hasBar
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 2
                    Repeater {
                        model: 16
                        Rectangle {
                            required property int index
                            width: 7; height: 6; radius: 1.5
                            color: index < Math.round(Math.min(1, Math.max(0, root.view.value)) * 16)
                                ? (root.view.dim ? Theme.textDim : Theme.accent) : Theme.alpha(Theme.text, 0.14)
                            Behavior on color { ColorAnimation { duration: Config.ms(90) } }
                        }
                    }
                }
            }

            Row {
                id: row
                visible: !root.vertical && !root.box
                anchors.verticalCenter: parent.verticalCenter
                x: pill.minimal ? 14 : 8
                spacing: pill.minimal ? 10 : 12

                Rectangle {
                    visible: !pill.minimal
                    width: 30; height: 30; radius: 15
                    anchors.verticalCenter: parent.verticalCenter
                    color: root.view.dim ? Theme.alpha(Theme.text, 0.12) : Theme.accent
                    Behavior on color { ColorAnimation { duration: Config.ms(200) } }
                    CpIcon {
                        anchors.centerIn: parent
                        cp: root.view.icon
                        font.pixelSize: 16
                        color: root.view.dim ? Theme.textDim : Theme.bgPanel
                    }
                }
                CpIcon {
                    visible: pill.minimal
                    anchors.verticalCenter: parent.verticalCenter
                    cp: root.view.icon
                    font.pixelSize: 14
                    color: root.view.dim ? Theme.textDim : Theme.accent
                }
                CpText {
                    visible: !pill.minimal || !pill.hasBar
                    anchors.verticalCenter: parent.verticalCenter
                    width: pill.hasBar ? 92 : implicitWidth
                    text: root.view.label
                    font.pixelSize: 12
                    font.bold: true
                    color: root.view.dim ? Theme.textDim : Theme.text
                }
                Rectangle {
                    visible: pill.hasBar
                    anchors.verticalCenter: parent.verticalCenter
                    width: pill.minimal ? 170 : 130; height: pill.minimal ? 4 : 6; radius: height / 2
                    color: Theme.alpha(Theme.text, 0.12)
                    Rectangle {
                        width: parent.width * Math.min(1, Math.max(0, root.view.value))
                        height: parent.height; radius: 3
                        color: root.view.dim ? Theme.textDim : Theme.accent
                        Behavior on width { NumberAnimation { duration: Config.ms(160); easing.type: Easing.OutCubic } }
                    }
                }
                CpText {
                    visible: pill.hasBar && Config.osd.showValue
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30
                    horizontalAlignment: Text.AlignRight
                    text: Math.round(root.view.value * 100)
                    font.pixelSize: 12
                    color: Theme.textDim
                }
            }

            // ---- vertical layout (right edge) ----
            Column {
                visible: root.vertical && !root.box
                anchors.horizontalCenter: parent.horizontalCenter
                y: 14
                spacing: 10
                CpText {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: pill.hasBar && Config.osd.showValue ? Math.round(root.view.value * 100) : ""
                    font.pixelSize: 11
                    font.bold: true
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 6; height: 150; radius: 3
                    color: Theme.alpha(Theme.text, 0.12)
                    visible: pill.hasBar
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width; radius: 3
                        height: parent.height * Math.min(1, Math.max(0, root.view.value))
                        color: root.view.dim ? Theme.textDim : Theme.accent
                        Behavior on height { NumberAnimation { duration: Config.ms(160); easing.type: Easing.OutCubic } }
                    }
                }
                CpIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    cp: root.view.icon
                    font.pixelSize: 18
                    color: root.view.dim ? Theme.textDim : Theme.accent
                }
            }
        }
    }
}
