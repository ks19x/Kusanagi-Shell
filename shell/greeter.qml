//@ pragma Env QT_WAYLAND_DISABLE_WINDOWDECORATION=1
// greeter.qml — Kusanagi's login screen: a greetd greeter that IS your lock screen design (LockScreen.qml)
// with a user and session picker and reboot / power off. Started by `kusanagi-greeter` (cage + qs) as
// greetd's default session, with HOME=/var/lib/kusanagi-greeter: its settings, colours, wallpaper
// and pictures are copies `kusanagi greeter sync` puts there (Settings → Login screen does it for you).
// Without greetd (Settings → Login screen → Preview) it runs in a window and logs nobody in.
import Quickshell
import Quickshell.Io
import Quickshell.Services.Greetd
import QtQuick

ShellRoot {
    id: g

    readonly property bool preview: !Greetd.available
    readonly property string home: Quickshell.env("HOME")

    // ---------------- who can log in, and into what ----------------
    property var users: []        // [{ name, display, shell, face }]
    property var sessions: []     // [{ id, name, exec, desktop }]
    property int ui: 0
    property int si: 0
    readonly property var user: users[ui] ?? { name: Quickshell.env("USER"), display: Quickshell.env("USER"), shell: "/bin/sh", face: "" }
    readonly property var session: sessions[si] ?? null

    Process {
        running: true
        command: ["getent", "passwd"]
        stdout: StdioCollector {
            onStreamFinished: {
                const list = []
                for (const l of text.split("\n")) {
                    const f = l.split(":")
                    if (f.length < 7 || +f[2] < 1000 || +f[2] >= 60000 || /nologin|false$/.test(f[6])) continue
                    const gecos = (f[4] || "").split(",")[0]
                    list.push({ name: f[0], display: gecos || f[0], shell: f[6] || "/bin/sh", face: g.home + "/faces/" + f[0] })
                }
                g.users = list
                const want = Config.greeter.user
                const i = list.findIndex(u => u.name === want)
                g.ui = i >= 0 ? i : 0
            }
        }
    }
    Process {
        running: true
        command: ["sh", "-c", "for f in /usr/share/wayland-sessions/*.desktop; do [ -f \"$f\" ] || continue; " +
                              "printf '%s\\t%s\\t%s\\t%s\\n' \"$(basename \"$f\" .desktop)\" \"$(grep -m1 '^Name=' \"$f\" | cut -d= -f2-)\" " +
                              "\"$(grep -m1 '^Exec=' \"$f\" | cut -d= -f2-)\" \"$(grep -m1 '^DesktopNames=' \"$f\" | cut -d= -f2-)\"; done"]
        stdout: StdioCollector {
            onStreamFinished: {
                g.sessions = text.split("\n").filter(l => l.trim()).map(l => {
                    const f = l.split("\t")
                    return { id: f[0], name: f[1] || f[0], exec: f[2] || f[0], desktop: (f[3] || f[1] || f[0]).split(";")[0] }
                })
                const i = g.sessions.findIndex(s => s.id === Config.greeter.session)
                g.si = i >= 0 ? i : 0
            }
        }
    }

    // ---------------- what LockScreen talks to (the same shape as Lock.qml) ----------------
    QtObject {
        id: auth
        property string password: ""
        property bool busy: false
        property string error: ""
        property bool unlocking: false
        readonly property bool testMode: false
        readonly property var player: null
        readonly property string wallpaper: wallFile.text().trim()
        readonly property string userName: g.user.display
        readonly property string face: g.user.face
        signal failed()
        function release() {}
        function submit() {
            if (busy || password === "") return
            error = ""
            if (g.preview) {
                error = "Preview — would log " + g.user.name + " into " + (g.session ? g.session.name : "?")
                password = ""
                return
            }
            busy = true
            Greetd.createSession(g.user.name)
        }
        function fail(msg) {
            busy = false
            error = msg || "Wrong password"
            password = ""
            failed()
        }
    }
    FileView { id: wallFile; path: g.home + "/.config/kusanagi/wallpaper"; blockLoading: true; printErrors: false }

    // greetd: ask for the password once, then start the chosen session through the user's login
    // shell (so their profile runs — PATH and friends), on its own D-Bus session like the TTY did
    Connections {
        target: Greetd
        function onAuthMessage(message, error, responseRequired, echoResponse) {
            if (responseRequired) Greetd.respond(auth.password)
            else if (error) auth.fail(message)
            else Greetd.respond("")
        }
        function onAuthFailure(message) { auth.fail(message) }
        function onError(message) { auth.fail(message) }
        function onReadyToLaunch() {
            const s = g.session
            if (!s) { auth.fail("No session to start"); Greetd.cancelSession(); return }
            auth.unlocking = true                    // the lock's dissolve-out
            // tell the launcher a session was handed over (so it never falls back after a login)
            const mark = Quickshell.env("KUSANAGI_GREETER_MARK")
            if (mark) Quickshell.execDetached(["touch", mark])
            Greetd.launch([g.user.shell, "-l", "-c", "exec dbus-run-session " + s.exec],
                          ["XDG_SESSION_TYPE=wayland", "XDG_CURRENT_DESKTOP=" + s.desktop, "XDG_SESSION_DESKTOP=" + s.id], true)
        }
    }

    function power(what) { if (!preview) Quickshell.execDetached(["sh", "-c", "loginctl " + what + " || systemctl " + what]) }

    // ---------------- the screen ----------------
    FloatingWindow {
        id: win
        visible: true
        title: g.preview ? "Kusanagi login screen — preview" : "Kusanagi login"
        // the whole screen (cage fullscreens it anyway); no frame — cage draws none and Qt's own is off above
        readonly property var scr: Quickshell.screens[0] ?? null
        implicitWidth: g.preview ? 1280 : (scr ? scr.width : 1920)
        implicitHeight: g.preview ? 800 : (scr ? scr.height : 1080)
        color: Theme.bgPanel

        LockScreen {
            anchors.fill: parent
            lock: auth
        }

        // user · session on the left, reboot · power off on the right
        component Pill: Rectangle {
            id: pill
            property int icon: 0
            property string label: ""
            property bool danger: false
            signal clicked()
            height: 40
            width: row.implicitWidth + 28
            radius: 20
            color: area.containsMouse ? Theme.alpha(pill.danger ? Theme.danger : Theme.accent, 0.3) : Theme.alpha(Theme.bgPanel, 0.55)
            border.width: 1
            border.color: Theme.alpha(Theme.text, 0.1)
            Behavior on color { ColorAnimation { duration: 140 } }
            Row {
                id: row
                anchors.centerIn: parent
                spacing: 10
                CpIcon { cp: pill.icon; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
                CpText { visible: pill.label !== ""; text: pill.label; font.pixelSize: 12; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
            }
            MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: pill.clicked() }
        }
        Row {
            anchors { left: parent.left; bottom: parent.bottom; margins: 28 }
            spacing: 10
            Pill {
                visible: g.users.length > 1
                icon: 0xf0004; label: g.user.display
                onClicked: { g.ui = (g.ui + 1) % g.users.length; auth.password = ""; auth.error = "" }
            }
            Pill {
                icon: 0xf0379; label: g.session ? g.session.name : "No sessions"
                onClicked: if (g.sessions.length) g.si = (g.si + 1) % g.sessions.length
            }
        }
        Row {
            anchors { right: parent.right; bottom: parent.bottom; margins: 28 }
            spacing: 10
            Pill { icon: 0xf0709; onClicked: g.power("reboot") }
            Pill { icon: 0xf0425; danger: true; onClicked: g.power("poweroff") }
        }
        Pill {
            visible: g.preview
            anchors { right: parent.right; top: parent.top; margins: 20 }
            icon: 0xf0156; label: "Close preview"
            onClicked: Qt.quit()
        }
    }
}
