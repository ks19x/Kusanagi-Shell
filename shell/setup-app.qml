// setup-app.qml — Kusanagi's first-run setup on its own, for the native shell: `kusanagi setup` runs
// `qs -p shell/setup-app.qml`; it writes settings.json (reloaded live by the native shell) and quits when closed.
import Quickshell
import QtQuick

ShellRoot {
    Setup {
        id: win
        Component.onCompleted: show()
        onShowingChanged: if (!showing) quitLater.start()
    }
    Timer { id: quitLater; interval: 150; onTriggered: Qt.quit() }
}
