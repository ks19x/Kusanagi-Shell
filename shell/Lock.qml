// Lock.qml — Kusanagi lock screen (ext-session-lock + PAM via /etc/pam.d/hyprlock, same rules as hyprlock).
// lock()      real lock: only the right password gets you out.
// test()      same screen, but Esc unlocks and it unlocks by itself after 30s — try it before switching
//             Config.lock.engine to "kusanagi".
// If Quickshell ever dies while locked the session STAYS locked (that's the protocol); recover from a
// TTY with `WAYLAND_DISPLAY=wayland-0 swaylock` (or hyprlock), which can take the lock over.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Services.Pam
import Quickshell.Services.Mpris
import QtQuick

Scope {
    id: root

    property bool testMode: false
    property string password: ""
    property bool busy: false
    property string error: ""
    property int fails: 0
    signal failed()


    function lock() {
        if (Config.lock.engine !== "kusanagi") {
            Quickshell.execDetached(["sh", "-c", "pidof hyprlock || { c=\"$HOME/.config/rices/zei/generated/hyprlock.conf\"; if [ -f \"$c\" ]; then hyprlock -c \"$c\"; else hyprlock; fi; } || swaylock -f"])
            return
        }
        testMode = false
        engage()
    }
    function test() { testMode = true; engage() }

    function engage() {
        if (sessionLock.locked) return
        password = ""; error = ""; busy = false; fails = 0
        wallFile.reload()
        sessionLock.locked = true
        if (testMode) failsafe.restart()
    }
    function release() {
        failsafe.stop()
        unlocking = true
        unlockAnim.restart()
    }
    property bool unlocking: false
    Timer { id: unlockAnim; interval: Config.ms(420); onTriggered: { sessionLock.locked = false; root.unlocking = false; root.password = "" } }

    function submit() {
        if (busy || !password) return
        if (testMode && password === "") return
        busy = true
        error = ""
        pam.start()
    }

    // test mode safety net
    Timer { id: failsafe; interval: 30000; onTriggered: if (root.testMode) root.release() }

    FileView { id: wallFile; path: Quickshell.env("HOME") + "/.config/kusanagi/wallpaper"; blockLoading: true; printErrors: false }
    readonly property string wallpaper: wallFile.text().trim()

    PamContext {
        id: pam
        config: "hyprlock"
        configDirectory: "/etc/pam.d"
        onPamMessage: { if (responseRequired) respond(root.password) }
        onCompleted: result => {
            root.busy = false
            if (result === PamResult.Success) root.release()
            else {
                root.fails++
                root.error = root.fails > 2 ? `Wrong password (${root.fails})` : "Wrong password"
                root.password = ""
                root.failed()
            }
        }
        onError: err => {
            root.busy = false
            root.error = "Couldn't check the password: " + PamError.toString(err)
            root.failed()
        }
    }

    readonly property MprisPlayer player: {
        const ps = Mpris.players.values
        return ps.find(p => p.isPlaying) ?? ps[0] ?? null
    }

    WlSessionLock {
        id: sessionLock

        WlSessionLockSurface {
            id: surface
            color: "black"

            LockScreen {
                anchors.fill: parent
                lock: root
            }
        }
    }
}
