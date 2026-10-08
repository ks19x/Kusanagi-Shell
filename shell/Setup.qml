// Setup.qml — the first-run wizard: opens by itself when Kusanagi starts without a settings.json, and any
// time with `kusanagi setup` (or "Kusanagi setup" in the launcher, Settings → About). Each step is a
// Settings page, so everything picked here can be changed later in the same place. Loaded only while open.
import Quickshell
import Quickshell.Io
import QtQuick

FloatingWindow {
    id: root

    property bool showing: false
    function show() { step = 0; showing = true }
    function hide() { showing = false }

    title: "Kusanagi Setup"
    implicitWidth: 1000
    implicitHeight: 700
    minimumSize: Qt.size(820, 560)
    color: Theme.alpha(Theme.bgPanel, Math.max(0.92, Config.panel.opacity))
    visible: showing
    onVisibleChanged: if (!visible) showing = false

    readonly property var steps: [
        { id: "welcome", name: "Welcome", icon: 0xf0493 },
        { id: "presets", name: "Look", icon: 0xf0e09, page: "SpPresets", desc: "Pick a whole look. Everything in it can be changed later." },
        { id: "wallpaper", name: "Wallpaper", icon: 0xf0e09, page: "SpWallpaper", desc: "Your wallpaper folder, and how pictures change. The colours follow the wallpaper." },
        { id: "bar", name: "Bar", icon: 0xf04e9, page: "SpBar", desc: "Start from a template — or build your own, piece by piece." },
        { id: "lock", name: "Lock & idle", icon: 0xf033e, page: "SpLock", desc: "How the lock screen looks, and what happens when you step away." },
        { id: "extras", name: "Extras", icon: 0xf0297 },
        { id: "login", name: "Login screen", icon: 0xf0004, page: "SpLogin", desc: "Optional: Kusanagi as the screen you log in on (needs greetd)." },
        { id: "done", name: "Done", icon: 0xf012c }
    ]
    property int step: 0
    readonly property var cur: steps[step]

    // what `kusanagi doctor` finds missing (welcome page)
    property var missing: []
    Process {
        running: root.showing
        command: ["sh", "-c", "kusanagi doctor 2>/dev/null | sed -n 's/^ *✗ *//p'"]
        stdout: StdioCollector { onStreamFinished: root.missing = text.split("\n").filter(l => l.trim()) }
    }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: root.hide()

        // ---------------- steps (left) ----------------
        Rectangle {
            id: side
            width: 230
            height: parent.height
            color: Theme.alpha(Theme.text, 0.03)
            Column {
                x: 22; y: 26
                spacing: 4
                Row {
                    spacing: 12
                    Image { width: 36; height: 36; source: Qt.resolvedUrl("logo.svg"); sourceSize: Qt.size(72, 72); smooth: true; mipmap: true }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        CpText { text: "KUSANAGI"; font.pixelSize: 14; font.bold: true; font.letterSpacing: 4 }
                        CpText { text: "setup"; font.pixelSize: 10; color: Theme.textDim; font.letterSpacing: 1 }
                    }
                }
                Item { width: 1; height: 22 }
                Repeater {
                    model: root.steps
                    Item {
                        required property var modelData
                        required property int index
                        width: 186; height: 36
                        readonly property bool active: index === root.step
                        readonly property bool done: index < root.step
                        Rectangle {
                            anchors.fill: parent; radius: 10
                            color: parent.active ? Theme.alpha(Theme.accent, 0.13) : "transparent"
                        }
                        Row {
                            x: 10; anchors.verticalCenter: parent.verticalCenter
                            spacing: 12
                            Rectangle {
                                width: 22; height: 22; radius: 11
                                anchors.verticalCenter: parent.verticalCenter
                                color: parent.parent.active ? Theme.accent : parent.parent.done ? Theme.alpha(Theme.accent, 0.3) : Theme.alpha(Theme.text, 0.08)
                                CpText {
                                    anchors.centerIn: parent
                                    text: parent.parent.parent.done ? "✓" : (parent.parent.parent.index + 1)
                                    font.pixelSize: 10; font.bold: true
                                    color: parent.parent.parent.active ? Theme.bgPanel : Theme.text
                                }
                            }
                            CpText { text: modelData.name; font.pixelSize: 12; font.bold: parent.parent.active; anchors.verticalCenter: parent.verticalCenter
                                     color: parent.parent.active ? Theme.text : Theme.alpha(Theme.text, 0.7) }
                        }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.step = index }
                    }
                }
            }
        }
        Rectangle { x: side.width; width: 1; height: parent.height; color: Theme.alpha(Theme.text, 0.06) }

        // ---------------- the step ----------------
        Item {
            id: main
            anchors { left: side.right; leftMargin: 1; right: parent.right; top: parent.top; bottom: footer.top }

            Column {
                id: heading
                x: 36; y: 30
                spacing: 4
                visible: !!root.cur.page || root.cur.id === "extras"
                CpText { text: root.cur.name; font.pixelSize: 24; font.bold: true }
                CpText { text: root.cur.desc || (root.cur.id === "extras" ? "A few things Kusanagi can take over. All optional." : ""); font.pixelSize: 12; color: Theme.textDim }
            }

            Flickable {
                id: scroller
                anchors { top: heading.visible ? heading.bottom : parent.top; topMargin: 22; left: parent.left; right: parent.right; bottom: parent.bottom }
                contentHeight: content.height + 30
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Loader {
                    id: content
                    x: 36
                    width: Math.min(740, scroller.width - 72)
                    sourceComponent: root.cur.page ? pageComp : root.cur.id === "welcome" ? welcome : root.cur.id === "extras" ? extras : done
                    onLoaded: { scroller.contentY = 0; fade.restart() }
                    NumberAnimation { id: fade; target: content; property: "opacity"; from: 0; to: 1; duration: Config.ms(220) }
                }
            }
        }

        Component { id: pageComp; Loader { source: root.cur.page + ".qml" } }

        Component {
            id: welcome
            Column {
                spacing: 18
                topPadding: 30
                Image { width: 84; height: 84; source: Qt.resolvedUrl("logo.svg"); sourceSize: Qt.size(168, 168); smooth: true; mipmap: true }
                CpText { text: "Welcome to Kusanagi"; font.pixelSize: 30; font.bold: true }
                CpText {
                    width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 13; color: Theme.alpha(Theme.text, 0.8)
                    text: "Your bar, panels, launcher, notifications, lock screen and more — all one program. A few steps to make it yours; skip any of them, and change everything later in Settings (Super+I)."
                }
                SpGroup {
                    title: "Your system"
                    CpRow { width: parent.width; label: "Compositor"; CpText { text: Wm.name; font.pixelSize: 12; color: Theme.accent } }
                    CpRow {
                        width: parent.width; label: "Missing pieces"
                        hint: root.missing.length ? "run  kusanagi doctor  in a terminal for the exact install command" : ""
                        CpText { text: root.missing.length ? root.missing.length + " found" : "none — all set"; font.pixelSize: 12; color: root.missing.length ? Theme.danger : Theme.ok }
                    }
                    Column {
                        visible: root.missing.length > 0
                        width: parent.width
                        spacing: 3
                        Repeater { model: root.missing.slice(0, 12); CpText { required property string modelData; text: "·  " + modelData; font.pixelSize: 11; color: Theme.textDim; width: parent.width; elide: Text.ElideRight } }
                    }
                }
            }
        }

        Component {
            id: extras
            Column {
                spacing: 22
                SpGroup {
                    title: "Take over from other tools"
                    CpRow { width: parent.width; label: "Lock and blank the screen when idle"; hint: "instead of hypridle / swayidle"; CpSwitch { on: Config.idle.enabled; onToggled: v => Config.idle.enabled = v } }
                    CpRow { width: parent.width; label: "Ask for admin passwords"; hint: "a polkit agent for GParted, mounting disks, pkexec…"; CpSwitch { on: Config.polkit.enabled; onToggled: v => Config.polkit.enabled = v } }
                }
                SpGroup {
                    title: "On the bar"
                    hint: "Adds them to the end of your bar (the custom layout; the classic bar becomes one)."
                    CpRow { width: parent.width; label: "Recording indicator"; hint: "shows while recording or replaying — click to save a clip"; CpSwitch { on: !!BarSpec.uses.recorder; onToggled: v => root.barModule("recorder", v) } }
                    CpRow { width: parent.width; label: "Package updates"; hint: "how many are waiting — click to install"; CpSwitch { on: !!BarSpec.uses.updates; onToggled: v => root.barModule("updates", v) } }
                }
                SpGroup {
                    title: "Launcher"
                    CpRow { width: parent.width; label: "Find Kusanagi commands"; hint: "type \"lock\", \"bar\", \"replay\"…"; CpSwitch { on: Config.launcher.commands; onToggled: v => Config.launcher.commands = v } }
                    CpRow { width: parent.width; label: "Web search as the last result"; CpSwitch { on: Config.launcher.webSearch; onToggled: v => Config.launcher.webSearch = v } }
                }
            }
        }

        Component {
            id: done
            Column {
                spacing: 18
                topPadding: 30
                CpText { text: "You're set"; font.pixelSize: 30; font.bold: true }
                CpText { width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 13; color: Theme.alpha(Theme.text, 0.8)
                         text: "A few keys to remember (Kusanagi's defaults — your compositor config may differ):" }
                SpGroup {
                    Repeater {
                        model: [["Super + Space", "launcher — apps, = calc, : emoji, / files, ? web"], ["Super + I", "Settings"], ["Super + L", "lock"],
                                ["Super + V", "clipboard history"], ["Super + A", "wallpapers"], ["Super + N", "notifications"], ["Super + G", "game mode"]]
                        CpRow {
                            required property var modelData
                            width: parent.width; label: modelData[1]
                            CpText { text: modelData[0]; font.pixelSize: 12; font.bold: true; color: Theme.accent }
                        }
                    }
                }
                CpText { width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 11; color: Theme.textDim
                         text: "Run this again any time: kusanagi setup — or search \"setup\" in the launcher." }
            }
        }

        // ---------------- back / next ----------------
        Rectangle {
            id: footer
            anchors { left: side.right; right: parent.right; bottom: parent.bottom }
            height: 64
            color: Theme.alpha(Theme.text, 0.02)
            Rectangle { width: parent.width; height: 1; color: Theme.alpha(Theme.text, 0.06) }
            Row {
                anchors { left: parent.left; leftMargin: 28; verticalCenter: parent.verticalCenter }
                CpChip { visible: root.step < root.steps.length - 1; label: "Skip setup"; onClicked: root.hide() }
            }
            Row {
                anchors { right: parent.right; rightMargin: 28; verticalCenter: parent.verticalCenter }
                spacing: 8
                CpChip { visible: root.step > 0; label: "Back"; icon: 0xf0141; onClicked: root.step-- }
                CpChip {
                    label: root.step === 0 ? "Start" : root.step === root.steps.length - 1 ? "Finish" : "Next"
                    icon: root.step === root.steps.length - 1 ? 0xf012c : 0xf0142
                    on: true
                    onClicked: if (root.step === root.steps.length - 1) root.hide(); else root.step++
                }
            }
        }
    }

    // add / remove a module at the end of every bar (the classic bar is turned into its layout first)
    function barModule(type, on) {
        let bars = JSON.parse(JSON.stringify(Config.bars || []))
        if (!bars.length) bars = [JSON.parse(JSON.stringify(BarSpec.legacy()))]
        const strip = list => (list || []).filter(e => (typeof e === "string" ? e : e.type) !== type)
            .map(e => typeof e === "object" && e.type === "group" ? Object.assign({}, e, { modules: strip(e.modules) }) : e)
        for (const b of bars) {
            b.start = strip(b.start); b.center = strip(b.center); b.end = strip(b.end)
            if (on) (b.end = b.end || []).unshift({ type: type })
        }
        Config.bars = bars
    }
}
