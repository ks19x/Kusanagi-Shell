// GreeterSync.qml — keeps the login screen (`kusanagi greeter`) showing what you have: when it's
// installed, a change to the lock / login design, colours or wallpaper is copied over a few seconds
// later. Nothing runs when it isn't installed.
import Quickshell
import Quickshell.Io
import QtQuick

Scope {
    FileView { id: wall; path: Quickshell.env("HOME") + "/.config/kusanagi/wallpaper"; blockLoading: true; printErrors: false; watchChanges: true; onFileChanged: reload() }
    FileView { id: conf; path: "/etc/greetd/config.toml"; blockLoading: true; printErrors: false; watchChanges: true; onFileChanged: reload() }
    readonly property bool installed: /command\s*=\s*"kusanagi-greeter"/.test(conf.text())
    // what the login screen shows
    readonly property string watched: !installed ? "" : JSON.stringify([Config.greeter.style, Config.greeter.user, Config.greeter.session,
        Config.lock.style, Config.lock.blur, Config.lock.dim, Config.lock.clock, Config.lock.avatar, Config.lock.greeting,
        Config.look.font, Config.look.palette, Config.look.accent, String(Theme.accent), String(Theme.bgPanel), wall.text().trim()])
    onWatchedChanged: if (installed) later.restart()
    // and once per start: it picks up the session you just logged in to (kusanagi session noted it)
    onInstalledChanged: if (installed) later.restart()
    Component.onCompleted: if (installed) later.restart()
    Timer { id: later; interval: 4000; onTriggered: Quickshell.execDetached(["kusanagi", "greeter", "sync", "-q"]) }
}
