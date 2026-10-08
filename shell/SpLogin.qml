// SpLogin.qml — Settings → Login screen: Kusanagi as your greetd greeter (the lock screen design, with
// a user + session picker), its design, defaults, preview, and install / uninstall (`kusanagi greeter …`).
import Quickshell
import Quickshell.Io
import QtQuick

Column {
    id: page
    spacing: 22

    // is greetd pointed at us? (its config is world-readable)
    FileView { id: conf; path: "/etc/greetd/config.toml"; blockLoading: true; printErrors: false; watchChanges: true; onFileChanged: reload() }
    readonly property bool hasGreetd: conf.text() !== ""
    readonly property bool installed: /command\s*=\s*"kusanagi-greeter"/.test(conf.text())
    readonly property string current: { const m = conf.text().match(/\[default_session\][^\[]*?command\s*=\s*"([^"]*)"/); return m ? m[1] : "" }

    property var sessions: []
    Process {
        running: true
        command: ["sh", "-c", "for f in /usr/share/wayland-sessions/*.desktop; do [ -f \"$f\" ] && printf '%s\\t%s\\n' \"$(basename \"$f\" .desktop)\" \"$(grep -m1 '^Name=' \"$f\" | cut -d= -f2-)\"; done"]
        stdout: StdioCollector { onStreamFinished: page.sessions = text.split("\n").filter(l => l.trim()).map(l => ({ value: l.split("\t")[0], label: l.split("\t")[1] || l.split("\t")[0] })) }
    }

    // admin steps need your password once: in a terminal
    function inTerminal(cmd) {
        Quickshell.execDetached([Config.launcher.terminal || "foot", "-e", "sh", "-c", cmd + "; printf '\\nPress Enter to close '; read x"])
    }
    property string note: ""
    function run(args, msg) { Quickshell.execDetached(["kusanagi", "greeter"].concat(args)); note = msg }

    SpGroup {
        title: "Login screen"
        icon: 0xf0004
        hint: page.installed ? "Kusanagi greets you when you log in — your lock screen design, with you and your session to pick."
            : page.hasGreetd ? "greetd (your login daemon) uses \"" + page.current + "\" now. Kusanagi can be the login screen instead."
            : "Needs greetd, the login daemon (install it from your distro)."
        Flow {
            width: parent.width
            spacing: 6
            CpChip {
                visible: page.hasGreetd && !page.installed
                label: "Use Kusanagi to log in…"; icon: 0xf0415; on: true
                onClicked: page.inTerminal("kusanagi greeter install")
            }
            CpChip { label: "Preview"; icon: 0xf0208; onClicked: page.run(["preview"], "") }
            CpChip { visible: page.installed; label: "Sync now"; icon: 0xf0450; onClicked: page.run(["sync"], "Synced — it shows your current design, colours and wallpaper.") }
            CpChip {
                visible: page.installed
                label: "Go back to " + (page.current === "kusanagi-greeter" ? "the old login" : "it") + "…"; icon: 0xf0a7a
                onClicked: page.inTerminal("kusanagi greeter uninstall")
            }
        }
        CpText { visible: page.note !== ""; text: page.note; font.pixelSize: 11; color: Theme.ok }
        CpText {
            width: parent.width; wrapMode: Text.WordWrap
            font.pixelSize: 10; color: Theme.textDim
            text: "Install asks for your password once (sudo): it adds cage (a tiny compositor for the login screen), "
                + "/var/lib/kusanagi-greeter and /usr/local/bin/kusanagi-greeter, and points /etc/greetd/config.toml at it — "
                + "the old one is kept, and if the Kusanagi login ever fails to start the old one takes over. "
                + "TTY logins (Ctrl+Alt+F1 / F2) stay as they are. It shows up after a reboot (greetd only reads its config when it starts)."
        }
    }

    SpGroup {
        title: "Design"
        hint: "Your lock screen's design, or a different one just for logging in."
        SpStylePicker {
            kind: "lock"
            cardW: 112
            current: Config.greeter.style
            options: [{ value: "", label: "Same as lock" }, { value: "center", label: "Centered" }, { value: "card", label: "Card" },
                      { value: "split", label: "Split" }, { value: "minimal", label: "Minimal" }, { value: "stacked", label: "Stacked" },
                      { value: "terminal", label: "Terminal" }]
            onPicked: v => Config.greeter.style = v
        }
    }

    SpGroup {
        title: "Defaults"
        CpRow {
            width: parent.width; label: "Session"; hint: "picked first (the button at the bottom left switches)"
            CpSegmented {
                width: Math.min(420, 120 * Math.max(1, page.sessions.length))
                current: Config.greeter.session || (page.sessions[0] ? page.sessions[0].value : "")
                options: page.sessions
                onPicked: v => Config.greeter.session = v
            }
        }
        CpRow {
            width: parent.width; label: "User"; hint: "blank: the first person on this machine"
            CpField { width: 200; text: Config.greeter.user; placeholder: Quickshell.env("USER"); onAccepted: t => Config.greeter.user = t.trim() }
        }
    }
}
