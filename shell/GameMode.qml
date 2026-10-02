// GameMode.qml — everything out of the way while you play.
// On (auto when the focused window goes fullscreen, or by hand / Super+G):
//   compositor: blur, shadows and animations off          (Config.gamemode.effects)
//   system:     feral gamemode → performance CPU governor  (Config.gamemode.feral)
//   Kusanagi:   stats stop polling, marquee + slideshow pause, popups held (critical still shows)
// Off: only the options it changed are put back to your configured values — no full config reload,
// so leaving a game doesn't jolt. A short grace period keeps quick alt-tabs / launcher pops from
// flapping it, and the on/off pill only shows for manual switches unless set otherwise.
pragma Singleton
import Quickshell
import Quickshell.Io
import Quickshell.Hyprland
import Quickshell.Wayland
import QtQuick

Singleton {
    id: root

    property bool active: false
    property bool manual: false          // switched on by hand: auto won't switch it off
    property bool auto: autoFile.text().trim() !== "off"
    readonly property bool quiet: active && Config.gamemode.quiet     // Kusanagi's own background work paused

    readonly property bool mango: !!Quickshell.env("MANGO_INSTANCE_SIGNATURE")

    // Mango: the last real window that had focus (overlays like the launcher take keyboard focus
    // without being a window — that shouldn't count as leaving the game)
    property var lastToplevel: null
    Connections {
        target: root.mango ? ToplevelManager : null
        function onActiveToplevelChanged() { if (ToplevelManager.activeToplevel) root.lastToplevel = ToplevelManager.activeToplevel }
    }
    readonly property bool fullscreen: mango
        ? (lastToplevel ? lastToplevel.fullscreen : false)
        : (Hyprland.focusedWorkspace ? Hyprland.focusedWorkspace.hasFullscreen : false)

    signal changed(bool byHand)

    // your configured Mango effects, so turning off restores them exactly (read once)
    property var mangoDefaults: ({ blur: 1, shadows: 1, animations: 1, layer_animations: 1 })
    Process {
        running: root.mango
        command: ["sh", "-c", "cat \"$HOME/.config/mango/config.conf\" \"$HOME/.config/mango/rice.conf\" 2>/dev/null | grep -E '^(blur|shadows|animations|layer_animations)='"]
        stdout: StdioCollector {
            onStreamFinished: {
                const d = Object.assign({}, root.mangoDefaults)
                for (const l of text.split("\n")) { const m = l.match(/^(\w+)=(\d)/); if (m) d[m[1]] = +m[2] }
                root.mangoDefaults = d
            }
        }
    }

    function effects(on) {
        if (!Config.gamemode.effects) return
        if (mango) {
            const d = mangoDefaults
            const v = k => on ? d[k] : 0
            Quickshell.execDetached(["sh", "-c",
                `mmsg dispatch setoption,blur,${v("blur")}; mmsg dispatch setoption,shadows,${v("shadows")};` +
                ` mmsg dispatch setoption,animations,${v("animations")}; mmsg dispatch setoption,layer_animations,${v("layer_animations")}`])
        } else {
            Quickshell.execDetached(["hyprctl", "eval",
                `hl.config({ decoration = { blur = { enabled = ${on} }, shadow = { enabled = ${on} } }, animations = { enabled = ${on} } })`])
        }
    }

    function enable(byHand) {
        offGrace.stop()
        if (byHand) manual = true
        if (active) return
        active = true
        effects(false)
        if (Config.gamemode.feral) feral.running = true
        root.changed(!!byHand)
    }

    function disable(byHand) {
        offGrace.stop()
        manual = false
        if (!active) return
        active = false
        effects(true)
        feral.running = false
        root.changed(!!byHand)
    }

    function toggle() { active ? disable(true) : enable(true) }

    function setAuto(on) {
        auto = on
        autoFile.setText(on ? "on\n" : "off\n")
        if (on && fullscreen) enable(false)
    }

    // fullscreen → on straight away; not fullscreen → off after a short grace (alt-tab and back = no flap)
    onFullscreenChanged: {
        if (!auto) return
        if (fullscreen) enable(false)
        else if (!manual) offGrace.restart()
    }
    Timer {
        id: offGrace
        interval: Config.gamemode.grace
        onTriggered: if (!root.fullscreen && !root.manual) root.disable(false)
    }

    // holds a feral gamemode request for as long as it runs
    Process {
        id: feral
        command: ["gamemoded", "-r"]
    }

    FileView {
        id: autoFile
        path: Quickshell.env("HOME") + "/.config/rices/zei/gamemode-auto"
        blockLoading: true
        printErrors: false
    }

    IpcHandler {
        target: "gamemode"
        function toggle(): void { root.toggle() }
        function on(): void { root.enable(true) }
        function off(): void { root.disable(true) }
        function auto(on: bool): void { root.setAuto(on) }
    }
}
