pragma Singleton
// Idle.qml — lock, screens off and suspend after you've been away (Settings → Lock & power → When idle).
// Replaces hypridle / swayidle: the compositor tells us when input stops (ext-idle-notify), apps that
// inhibit idle (video players, Caffeine) are respected, and nothing runs while you're at the keyboard.
// Stays awake while media plays (idle.media) and in game mode. IPC: idle status | inhibit
import Quickshell
import Quickshell.Wayland
import Quickshell.Services.Mpris
import QtQuick

Singleton {
    id: root
    property var lock: null                // Lock.qml, handed over by shell.qml

    readonly property bool playing: Config.idle.media && Mpris.players.values.some(p => p.isPlaying)
    // off entirely: not enabled, or something wants the screen awake
    readonly property bool armed: Config.idle.enabled && !Caffeine.active && !GameMode.active && !playing

    // a heads-up shortly before locking; any input cancels it (the monitor stops being idle)
    readonly property int warnSecs: 10
    property bool warning: false

    function minutes(m) { return Math.max(1, m) * 60 }

    IdleMonitor {
        id: warnMon
        enabled: root.armed && Config.idle.notify && Config.idle.lock > 0
        respectInhibitors: true
        timeout: Math.max(5, root.minutes(Config.idle.lock) - root.warnSecs)
        onIsIdleChanged: root.warning = isIdle
    }
    IdleMonitor {
        id: lockMon
        enabled: root.armed && Config.idle.lock > 0
        respectInhibitors: true
        timeout: root.minutes(Config.idle.lock)
        onIsIdleChanged: if (isIdle && root.lock) { root.warning = false; root.lock.lock() }
    }
    IdleMonitor {
        id: offMon
        enabled: root.armed && Config.idle.screenOff > 0
        respectInhibitors: true
        timeout: root.minutes(Config.idle.screenOff)
        property bool off: false
        onIsIdleChanged: {
            if (isIdle) { off = true; Wm.screens(false) }
            else if (off) { off = false; Wm.screens(true) }
        }
    }
    IdleMonitor {
        id: sleepMon
        enabled: root.armed && Config.idle.suspend > 0
        respectInhibitors: true
        timeout: root.minutes(Config.idle.suspend)
        onIsIdleChanged: if (isIdle) root.suspend()
    }

    // lock first (when idle locking is on), then sleep — whichever init / seat manager this system has
    function suspend() {
        if (Config.idle.lock > 0 && lock) lock.lock()
        Quickshell.execDetached(["sh", "-c", "sleep 1; loginctl suspend 2>/dev/null || systemctl suspend 2>/dev/null || zzz 2>/dev/null"])
    }

    readonly property string status: !Config.idle.enabled ? "off"
        : Caffeine.active ? "awake: caffeine" : GameMode.active ? "awake: game mode" : playing ? "awake: media playing" : "armed"

    // the heads-up pill: only exists while warning
    LazyLoader {
        active: root.warning
        PanelWindow {
            anchors.top: true
            margins.top: 60
            implicitWidth: pill.width + 4
            implicitHeight: pill.height + 4
            color: "transparent"
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.namespace: "kusanagi-idle"
            mask: Region {}
            Rectangle {
                id: pill
                anchors.centerIn: parent
                width: row.width + 36; height: 44; radius: 22
                color: Theme.alpha(Theme.bgPanel, 0.94)
                border.width: Theme.surfaceBorderWidth
                border.color: Theme.surfaceBorder
                Row {
                    id: row
                    anchors.centerIn: parent
                    spacing: 10
                    CpIcon { cp: 0xf033e; font.pixelSize: 17; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
                    CpText { text: "Locking soon — move the mouse to stay"; font.pixelSize: 13; anchors.verticalCenter: parent.verticalCenter }
                }
            }
        }
    }
}
