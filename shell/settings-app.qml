// settings-app.qml — Kusanagi's Settings window on its own, for the native shell (kusanagi engine native):
// `kusanagi settings [page]` runs `qs -p shell/settings-app.qml`. It edits ~/.config/kusanagi/settings.json,
// which the native shell reloads live, and quits when the window closes — nothing stays in memory.
// IPC (qs -p …/settings-app.qml ipc call settings <fn>): toggle | page <name> | hide.
import Quickshell
import Quickshell.Io
import QtQuick

ShellRoot {
    Settings {
        id: win
        Component.onCompleted: show(Quickshell.env("KUSANAGI_SETTINGS_PAGE") || "presets")
        onShowingChanged: if (!showing) quitLater.start()
    }
    // let the window unmap before the process goes
    Timer { id: quitLater; interval: 150; onTriggered: Qt.quit() }

    IpcHandler {
        target: "settings"
        function toggle(): void { if (win.showing) win.hide(); else win.show() }
        function page(name: string): void { win.show(name) }
        function hide(): void { win.hide() }
    }
}
