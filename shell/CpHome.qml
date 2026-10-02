// CpHome.qml — control panel home: quick tiles, volume/mic, now playing, live mini stats
import Quickshell
import Quickshell.Io
import Quickshell.Widgets
import Quickshell.Services.Mpris
import Quickshell.Services.Pipewire
import QtQuick

Column {
    id: root
    required property var panel

    spacing: 12


    // ---- audio ----
    readonly property PwNode sink: Pipewire.defaultAudioSink
    readonly property PwNode source: Pipewire.defaultAudioSource
    PwObjectTracker { objects: [root.sink, root.source].filter(n => n) }
    readonly property bool sinkReady: !!sink && sink.ready && !!sink.audio
    readonly property bool sourceReady: !!source && source.ready && !!source.audio

    // ---- night light (gammastep) ----
    property bool nightlight: false
    // re-checked every time the panel opens (it stays loaded in between)
    Connections { target: root.panel; function onShowingChanged() { if (root.panel.showing) nightCheck.running = true } }
    Process {
        id: nightCheck
        running: true
        command: ["pgrep", "-x", "gammastep"]
        onExited: code => root.nightlight = code === 0
    }
    Process { id: nightSet; onExited: nightCheck.running = true }

    // ---------- tiles (which + order: Config.panel.tiles) ----------
    function tile(id) {
        switch (id) {
        case "nightlight": return { icon: 0xf0594, label: "Night light", sub: nightlight ? Config.display.nightTemp + "K" : "Off", on: nightlight }
        case "dnd": return { icon: Notifs.dnd ? 0xf009b : 0xf009a, label: "Do not disturb", sub: Notifs.dnd ? "Silenced" : "Off", on: Notifs.dnd }
        case "mic": {
            const muted = sourceReady && source.audio.muted
            return { icon: muted ? 0xf036d : 0xf036c, label: "Microphone", sub: muted ? "Muted" : "Live", on: sourceReady && !muted }
        }
        case "gamemode": return { icon: 0xf0297, label: "Game mode", on: GameMode.active,
                                  sub: GameMode.active ? (GameMode.manual ? "On" : "On · auto") : (GameMode.auto ? "Auto" : "Off") }
        case "screenshot": return { icon: 0xf0e51, label: "Screenshot", sub: "Region" }
        case "record": return { icon: 0xf044a, label: "Record", sub: "Start / stop" }
        case "colorpicker": return { icon: 0xf020a, label: "Colour picker", sub: "Copy hex" }
        case "wallpaper": return { icon: 0xf0e09, label: "Wallpaper", sub: "Pick & theme" }
        case "clipboard": return { icon: 0xf0147, label: "Clipboard", sub: "History" }
        case "lock": return { icon: 0xf033e, label: "Lock", sub: "Lock screen" }
        case "settings": return { icon: 0xf0493, label: "Settings", sub: "Kusanagi" }
        case "launcher": return { icon: 0xf003b, label: "Apps", sub: "Launcher" }
        }
        return { icon: 0, label: id, sub: "" }
    }
    function press(id) {
        const ipc = what => ["kusanagi", "msg"].concat(what)
        switch (id) {
        case "nightlight":
            nightSet.command = ["sh", "-c", nightlight ? "pkill -x gammastep"
                : `setsid -f gammastep -O ${Config.display.nightTemp} >/dev/null 2>&1; sleep 0.3`]
            nightSet.running = true
            nightlight = !nightlight
            break
        case "dnd": Notifs.dnd = !Notifs.dnd; break
        case "mic": if (sourceReady) source.audio.muted = !source.audio.muted; break
        case "gamemode": GameMode.toggle(); break
        case "screenshot": panel.runClosed(["kusanagi", "screenshot", "region"]); break
        case "record": panel.runClosed(["gsr-ui-cli", "toggle-record"]); break
        case "colorpicker": panel.runClosed(["kusanagi", "colorpick"]); break
        case "wallpaper": panel.runClosed(ipc(["wallpaper", "toggle"])); break
        case "clipboard": panel.runClosed(ipc(["clipboard", "toggle"])); break
        case "lock": panel.runClosed(ipc(["lock", "lock"])); break
        case "settings": panel.runClosed(ipc(["settings", "open"])); break
        case "launcher": panel.runClosed(ipc(["launcher", "open"])); break
        }
    }

    Grid {
        width: parent.width
        visible: Config.panel.tiles.length > 0
        columns: 4
        spacing: 8
        readonly property real cell: (width - 3 * spacing) / 4

        Repeater {
            model: Config.panel.tiles
            CpTile {
                required property string modelData
                readonly property var t: root.tile(modelData)
                width: parent.cell
                icon: t.icon
                label: t.label
                sub: t.sub
                on: !!t.on
                onClicked: root.press(modelData)
            }
        }
    }

    // ---------- sliders ----------
    CpSlider {
        width: parent.width
        icon: !root.sinkReady || root.sink.audio.muted ? 0xf0581 : 0xf057e
        label: root.sinkReady ? (root.sink.nickname || root.sink.description || "Output") : "Output"
        value: root.sinkReady ? root.sink.audio.volume : 0
        muted: root.sinkReady && root.sink.audio.muted
        onMoved: v => { if (root.sinkReady) root.sink.audio.volume = v }
        onIconClicked: if (root.sinkReady) root.sink.audio.muted = !root.sink.audio.muted
    }
    CpSlider {
        width: parent.width
        icon: !root.sourceReady || root.source.audio.muted ? 0xf036d : 0xf036c
        label: "Microphone"
        value: root.sourceReady ? root.source.audio.volume : 0
        muted: root.sourceReady && root.source.audio.muted
        onMoved: v => { if (root.sourceReady) root.source.audio.volume = v }
        onIconClicked: if (root.sourceReady) root.source.audio.muted = !root.source.audio.muted
    }

    // ---------- now playing ----------
    readonly property MprisPlayer player: {
        const ps = Mpris.players.values
        return ps.find(p => p.isPlaying) ?? ps[0] ?? null
    }

    CpCard {
        width: parent.width
        height: 96
        visible: Config.panel.showMedia && root.player !== null

        // nudge the position along while playing (MPRIS doesn't push it)
        Timer {
            running: root.panel.showing && root.player !== null && root.player.isPlaying
            interval: 1000; repeat: true
            onTriggered: root.player.positionChanged()
        }

        ClippingRectangle {
            id: art
            x: 12; y: 12
            width: 72; height: 72
            radius: Math.max(6, Config.look.radius - 10)
            color: Theme.alpha(Theme.text, 0.08)
            CpIcon { anchors.centerIn: parent; cp: 0xf075a; font.pixelSize: 26; color: Theme.textDim; visible: cover.status !== Image.Ready }
            Image {
                id: cover
                anchors.fill: parent
                source: root.player ? root.player.trackArtUrl : ""
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(144, 144)
                asynchronous: true
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Config.ms(300) } }
            }
        }

        Column {
            anchors { left: art.right; leftMargin: 14; right: controls.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
            spacing: 2
            CpText {
                width: parent.width
                text: root.player ? (root.player.trackTitle || "Unknown") : ""
                elide: Text.ElideRight
                font.pixelSize: 13
                font.bold: true
            }
            CpText {
                width: parent.width
                text: root.player ? (root.player.trackArtist || root.player.identity || "") : ""
                elide: Text.ElideRight
                font.pixelSize: 11
                color: Theme.textDim
            }
            Item { width: 1; height: 8 }
            // progress — click to seek
            Rectangle {
                id: bar
                width: parent.width
                height: 4
                radius: 2
                color: Theme.alpha(Theme.text, 0.1)
                readonly property real frac: root.player && root.player.length > 0
                    ? Math.min(1, root.player.position / root.player.length) : 0
                Rectangle {
                    width: parent.width * bar.frac
                    height: parent.height
                    radius: 2
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: Config.ms(900); easing.type: Easing.Linear } }
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -6
                    cursorShape: Qt.PointingHandCursor
                    enabled: root.player !== null && root.player.canSeek
                    onClicked: e => root.player.position = root.player.length * Math.max(0, Math.min(1, (e.x - 6) / bar.width))
                }
            }
        }

        Row {
            id: controls
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            spacing: 2
            CpIconButton { icon: 0xf04ae; onClicked: root.player.previous() }
            CpIconButton {
                icon: root.player && root.player.isPlaying ? 0xf03e4 : 0xf040a
                iconSize: 18
                width: 40; height: 40
                filled: true
                onClicked: root.player.togglePlaying()
            }
            CpIconButton { icon: 0xf04ad; onClicked: root.player.next() }
        }
    }

    // ---------- mini stats (click → System tab) ----------
    Row {
        visible: Config.panel.showStats
        width: parent.width
        spacing: 8
        readonly property real cell: (width - 3 * spacing) / 4

        Repeater {
            model: [
                { icon: 0xf0ee0, label: "CPU", value: SysInfo.cpu, text: SysInfo.cpu + "%" },
                { icon: 0xf035b, label: "RAM", value: SysInfo.ram, text: SysInfo.ram + "%" },
                { icon: 0xf08ae, label: "GPU", value: SysInfo.gpu, text: SysInfo.gpu + "%" },
                { icon: 0xf050f, label: "TEMP", value: SysInfo.cpuTemp, text: SysInfo.cpuTemp + "°", hot: SysInfo.cpuTemp >= 85, warm: SysInfo.cpuTemp >= 70 }
            ]
            CpCard {
                id: stat
                required property var modelData
                width: parent.cell
                height: 54
                color: Theme.alpha(Theme.text, statArea.containsMouse ? 0.08 : 0.045)
                Behavior on color { ColorAnimation { duration: Config.ms(160) } }

                CpIcon { x: 12; y: 10; cp: stat.modelData.icon; font.pixelSize: 14; color: Theme.textDim }
                CpText { x: 32; y: 9; text: stat.modelData.label; font.pixelSize: 10; font.bold: true; color: Theme.textDim; font.letterSpacing: 1 }
                CpText {
                    anchors { right: parent.right; rightMargin: 12; top: parent.top; topMargin: 7 }
                    text: stat.modelData.text
                    font.pixelSize: 13
                    font.bold: true
                }
                Rectangle {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 12; bottomMargin: 12 }
                    height: 4; radius: 2
                    color: Theme.alpha(Theme.text, 0.1)
                    Rectangle {
                        width: parent.width * Math.min(100, stat.modelData.value) / 100
                        height: parent.height; radius: 2
                        color: stat.modelData.hot ? Theme.danger : stat.modelData.warm ? "#e8be62" : Theme.accent
                        Behavior on width { NumberAnimation { duration: Config.ms(600); easing.type: Easing.OutCubic } }
                    }
                }
                MouseArea { id: statArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.panel.tab = 1 }
            }
        }
    }
}
