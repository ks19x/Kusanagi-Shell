pragma Singleton
// GameMode.qml — everything out of the way while you play.
// On (auto when the focused window goes fullscreen, or by hand / Super+G):
//   compositor: blur, shadows and animations off          (Config.gamemode.effects)
//   system:     feral gamemode → performance CPU governor  (Config.gamemode.feral)
//   Kusanagi:   stats stop polling, marquee + slideshow pause, popups held (critical still shows)
// Off: only the options it changed are put back to your configured values — no full config reload,
// so leaving a game doesn't jolt. A short grace period keeps quick alt-tabs / launcher pops from
// flapping it, and the on/off pill only shows for manual switches unless set otherwise.
import Quickshell
import Quickshell.Io
import QtQuick

Singleton {
    id: root

    property bool active: false
    property bool manual: false          // switched on by hand: auto won't switch it off
    readonly property bool auto: Config.gamemode.auto
    readonly property bool quiet: active && Config.gamemode.quiet     // Kusanagi's own background work paused

    readonly property bool fullscreen: Wm.fullscreen

    signal changed(bool byHand)

    function effects(on) { if (Config.gamemode.effects) Wm.setEffects(on) }

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
        Config.gamemode.auto = on
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


    IpcHandler {
        target: "gamemode"
        function toggle(): void { root.toggle() }
        function on(): void { root.enable(true) }
        function off(): void { root.disable(true) }
        function auto(on: bool): void { root.setAuto(on) }
    }
}
