// Bar.qml — the 43PR bar, moved here from waybar.
//   left: workspaces | centre: clock | right: mpris, cpu, ram, gpu, temp, volume, network, tray, power
// Everything adjustable lives in Config.bar / Config.workspaces (Customize tab of the control panel):
// style (islands | solid | floating | clear), opacity, radius, clock format, modules, workspace look.
// Default metrics = the old waybar style.css (11px, 22px islands, hover-grow); text uses native
// rendering + full hinting = GTK's whole-pixel advances and greyscale AA. Stats come from SysInfo.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Hyprland
import Quickshell.WindowManager
import Quickshell.Services.Mpris
import Quickshell.Services.Pipewire
import Quickshell.Services.SystemTray
import Quickshell.Widgets
import QtQuick
import QtQuick.Effects

PanelWindow {
    id: bar

    required property var shell     // shell.qml, for the control panel / settings panels

    readonly property string font: Theme.fontFamily

    readonly property bool bottom: Config.bar.position === "bottom"
    readonly property int h: Config.bar.height
    readonly property int ih: h - 6                   // island height (22 at the default 28)
    readonly property bool centered: Config.bar.layout === "centered"
    property alias clockIsland: clockIsland           // the control panel grows out of it

    anchors { top: !bottom; bottom: bottom; left: true; right: true }
    implicitHeight: h
    // Hyprland puts windows gaps_out top=3 under the bar; Mango's outer gap is one value (8)
    // for top and bottom, so reserve 5 less there to land on the same 3px
    exclusiveZone: Wm.kind === "mango" && !bottom ? h - 5 : h
    color: "transparent"
    WlrLayershell.namespace: "quickshell-bar"
    WlrLayershell.layer: WlrLayer.Top

    // caffeine: the bar is always mapped, so it carries the idle inhibitor
    IdleInhibitor { window: bar; enabled: Caffeine.active }

    // ---------------------------------------------------------------- building blocks

    component Island: Rectangle {
        default property alias content: row.data
        y: bar.bottom ? 2 : 4
        height: bar.ih
        width: row.implicitWidth
        radius: Config.bar.radius
        border.width: Config.bar.outline && Config.bar.style === "islands" ? 1 : 0
        border.color: Theme.alpha(Theme.text, 0.12)
        color: Config.bar.style === "islands" ? Theme.alpha(Theme.bgPanel, Config.bar.opacity) : "transparent"
        Behavior on color { ColorAnimation { duration: 250 } }
        Row { id: row; height: parent.height }
    }

    // solid = full-width strip, floating = one rounded bar; islands/clear draw nothing here
    Rectangle {
        visible: Config.bar.style === "solid" || Config.bar.style === "floating"
        readonly property bool floating: Config.bar.style === "floating"
        x: floating ? 6 : 0
        y: floating ? (bar.bottom ? 1 : 3) : 0
        width: bar.width - 2 * x
        height: floating ? bar.h - 4 : bar.h
        radius: floating ? Config.bar.radius : 0
        color: Theme.alpha(Theme.bgPanel, Config.bar.opacity)
    }

    // one waybar module: margin 0 2px, padding 0 10px, grows to 15px on hover
    component Module: Item {
        id: m
        property string text
        property int fontSize: Config.bar.fontSize
        property bool bold: false
        property bool grow: true
        property real padL: 10
        property real padR: 10
        property real marL: 2
        property real marR: 2
        property string tooltip: ""
        property bool tooltipRich: false
        readonly property alias hovered: mouse.containsMouse
        signal clicked(int button)
        signal scrolled(int steps)

        property bool shown: true
        property bool rich: false                  // text carries <font> colour markup
        property bool accent: false                // icon-only module: whole thing accent with accentLabels
        visible: shown && text !== ""
        height: bar.ih
        width: visible ? marL + padL + label.implicitWidth + padR + marR : 0

        Text {
            id: label
            x: m.marL + m.padL
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: m.fontSize > Config.bar.fontSize ? 1 : 0    // GTK sits the bigger glyphs 1px lower
            text: m.text
            textFormat: m.rich ? Text.StyledText : Text.PlainText
            color: m.accent && Config.bar.accentLabels ? Theme.accent : Theme.text
            font.family: bar.font
            font.hintingPreference: Font.PreferFullHinting
            renderType: Text.NativeRendering
            font.pixelSize: m.grow && Config.bar.hoverGrow && mouse.containsMouse ? Config.bar.fontSize + 4 : m.fontSize
            font.bold: m.bold
            Behavior on font.pixelSize { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            onClicked: e => m.clicked(e.button)
            onWheel: e => { if (e.angleDelta.y !== 0) m.scrolled(e.angleDelta.y > 0 ? 1 : -1) }
            onContainsMouseChanged: containsMouse ? bar.showTip(m) : bar.hideTip(m)
        }
    }

    function run(cmd) { Quickshell.execDetached(cmd) }

    // focused window's title — overlays (launcher, panel) take focus without being windows, so keep the last
    property var focusedWindow: null
    readonly property string windowTitle: focusedWindow ? focusedWindow.title : ""
    Component.onCompleted: focusedWindow = ToplevelManager.activeToplevel
    Connections {
        target: ToplevelManager
        function onActiveToplevelChanged() { if (ToplevelManager.activeToplevel) bar.focusedWindow = ToplevelManager.activeToplevel }
    }
    // "CPU" → accent-coloured when Config.bar.accentLabels
    function lbl(t) { return Config.bar.accentLabels ? `<font color="${Theme.accent}">${t}</font>` : t }

    // ---- scroll actions (Config.bar.scrollClock / scrollStats) ----
    function changeVolume(steps) {
        if (!audioReady) return
        sink.audio.volume = Math.max(0, Math.min(100, volume + Config.bar.volumeStep * steps)) / 100
    }
    function scrollAction(action, steps) {
        if (action === "volume") changeVolume(steps)
        else if (action === "workspaces") scrollWorkspace(steps)
    }

    // ---------------------------------------------------------------- tooltip (one shared popup)

    property Item tipItem: null
    function showTip(item) {
        if (!item.tooltip) return
        tipLoader.active = false
        tipItem = item
        tipDelay.restart()
    }
    function hideTip(item) {
        if (tipItem !== item) return
        tipDelay.stop()
        tipLoader.active = false
        tipItem = null
    }
    Timer { id: tipDelay; interval: 500; onTriggered: tipLoader.active = true }

    LazyLoader {
        id: tipLoader
        PopupWindow {
            visible: true
            color: "transparent"
            anchor.item: bar.tipItem
            anchor.rect.width: bar.tipItem ? bar.tipItem.width : 0
            anchor.rect.y: bar.bottom ? -6 : 0
            anchor.rect.height: bar.tipItem && !bar.bottom ? bar.tipItem.height + 6 : 0
            anchor.edges: bar.bottom ? Edges.Top : Edges.Bottom
            anchor.gravity: bar.bottom ? Edges.Top : Edges.Bottom
            implicitWidth: tipText.implicitWidth + 20
            implicitHeight: tipText.implicitHeight + 14

            Rectangle {
                anchors.fill: parent
                radius: 10
                color: Theme.alpha(Theme.bgPanel, 0.85)
                border.width: 1
                border.color: Theme.alpha(Theme.text, 0.08)
                Text {
                    id: tipText
                    anchors.centerIn: parent
                    text: bar.tipItem ? bar.tipItem.tooltip : ""
                    textFormat: bar.tipItem && bar.tipItem.tooltipRich ? Text.RichText : Text.PlainText
                    color: Theme.text
                    font.family: bar.font
            font.hintingPreference: Font.PreferFullHinting
            renderType: Text.NativeRendering
                    font.pixelSize: 13
                    font.bold: true
                }
            }
        }
    }

    // ---------------------------------------------------------------- left: workspaces

    // workspaces / tags come from the compositor layer (Mango, Hyprland, niri)
    readonly property var workspaces: Wm.workspaces
    function goto(entry) { Wm.focusWorkspace(entry) }
    function scrollWorkspace(steps) { Wm.scroll(steps) }

    Island {
        // dwl tags run the full bar height from the edge; everything else sits in an island
        readonly property bool flush: Config.workspaces.style === "dwl" && !bar.centered
        x: flush ? 0 : bar.centered ? Math.floor((bar.width - width) / 2) : 6
        y: flush ? 0 : (bar.bottom ? 2 : 4)
        height: flush ? bar.h : bar.ih
        radius: flush ? 0 : Config.bar.radius
        // #workspaces: margin 0 2px + padding 0 6px
        Item { width: parent.parent.flush ? 0 : 8; height: 1 }
        WsIndicator {
            slotHeight: parent.parent.flush ? bar.h : bar.ih
            entries: bar.workspaces
            onActivated: entry => bar.goto(entry)
        }
        Item { width: parent.parent.flush ? 0 : 8; height: 1 }

        // focused window's title, dwl-style (Config.bar.modules.title)
        Module {
            shown: Config.bar.modules.title && !bar.centered
            height: parent.height
            grow: false
            padL: 12
            text: bar.windowTitle.length > Config.bar.titleWidth ? bar.windowTitle.slice(0, Config.bar.titleWidth - 1) + "…" : bar.windowTitle
            tooltip: bar.windowTitle.length > Config.bar.titleWidth ? bar.windowTitle : ""
        }

        // scrolling anywhere on the island walks the workspaces, like waybar
        WheelHandler {
            onWheel: e => { if (e.angleDelta.y !== 0) bar.scrollWorkspace(e.angleDelta.y > 0 ? 1 : -1) }
        }
    }

    // ---------------------------------------------------------------- centre: clock

    SystemClock { id: clock; precision: Config.bar.clock.includes("ss") ? SystemClock.Seconds : SystemClock.Minutes }

    function calendar() {
        const now = clock.date, loc = Qt.locale()
        const y = now.getFullYear(), mo = now.getMonth(), today = now.getDate()
        const first = loc.firstDayOfWeek % 7                 // 0 = Sunday
        const days = new Date(y, mo + 1, 0).getDate()
        const lead = (new Date(y, mo, 1).getDay() - first + 7) % 7
        const title = loc.standaloneMonthName(mo) + " " + y
        const pad = Math.max(0, Math.floor((20 - title.length) / 2))
        let out = " ".repeat(pad) + title + "\n"
        const names = []
        for (let i = 0; i < 7; i++) names.push(loc.dayName((first + i) % 7, Locale.ShortFormat).slice(0, 2))
        out += names.join(" ") + "\n"
        let line = "   ".repeat(lead)
        for (let d = 1; d <= days; d++) {
            const cell = (d < 10 ? " " : "") + d
            line += d === today ? `<b><u>${cell}</u></b>` : cell
            if ((lead + d) % 7 === 0) { out += line + "\n"; line = "" } else line += " "
        }
        if (line.trim()) out += line.replace(/ $/, "")
        return `<pre style="font-family:'${bar.font}'">${out.replace(/\n$/, "")}</pre><small>click: control panel</small>`
    }

    Island {
        id: clockIsland
        x: bar.centered ? 6 : Math.floor((bar.width - width) / 2)
        // the control panel grows out of this island, so it steps aside while that's open
        opacity: SysInfo.panelOpen ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 140 } }
        Module {
            text: Qt.formatDateTime(clock.date, Config.bar.clock)
            bold: Config.bar.clockBold
            tooltipRich: true
            tooltip: hovered ? bar.calendar() : ""
            onClicked: { const p = bar.shell.notifsPanel(); p.showing = !p.showing }
            onScrolled: steps => bar.scrollAction(Config.bar.scrollClock, steps)
        }
    }

    // ---------------------------------------------------------------- right

    // mpris: "♪  title - artist", scrolled 20 chars wide while playing
    readonly property MprisPlayer player: {
        const ps = Mpris.players.values
        return ps.find(p => p.isPlaying) ?? ps[0] ?? null
    }
    readonly property string track: {
        if (!player) return ""
        const t = player.trackTitle || "", a = player.trackArtist || ""
        return (a ? `${t} - ${a}` : t).slice(0, 60)
    }
    property int marquee: 0
    onTrackChanged: marquee = 0
    Timer {
        interval: 150
        repeat: true
        running: Config.bar.marquee && !GameMode.quiet && bar.player !== null && bar.player.isPlaying && bar.track.length > Config.bar.mediaWidth
        onTriggered: bar.marquee = (bar.marquee + 1) % (bar.track.length + 5)
    }
    readonly property string trackView: {
        const w = Config.bar.mediaWidth
        if (!Config.bar.marquee || track.length <= w || !(player && player.isPlaying)) return track.slice(0, w)
        const loop = track + "     "
        return (loop.slice(marquee) + loop.slice(0, marquee)).slice(0, w)
    }

    // volume (default sink)
    readonly property PwNode sink: Pipewire.defaultAudioSink
    PwObjectTracker { objects: bar.sink ? [bar.sink] : [] }
    readonly property bool audioReady: !!sink && sink.ready && !!sink.audio
    readonly property int volume: audioReady ? Math.round(sink.audio.volume * 100) : 0

    Island {
        x: bar.width - 6 - width

        Module {
            shown: Config.bar.modules.media
            text: bar.track ? "♪  " + bar.trackView : ""
            opacity: bar.player && !bar.player.isPlaying ? 0.55 : 1
            id: mediaModule
            tooltip: Config.bar.mediaPopup ? "" : bar.track
            onHoveredChanged: bar.mediaHover = hovered
            onScrolled: steps => bar.scrollAction(Config.bar.scrollStats, steps)
            onClicked: b => {
                if (!bar.player) return
                if (b === Qt.LeftButton) bar.player.togglePlaying()
                else if (b === Qt.RightButton) bar.player.next()
                else bar.player.previous()
            }
        }

        // caffeine on: a coffee cup; click to let the screen sleep again
        Module {
            shown: Caffeine.active
            accent: true
            grow: false
            fontSize: Config.bar.fontSize + 2
            padR: 7
            text: String.fromCodePoint(0xf0176)
            tooltip: "Caffeine: the screen won't sleep or lock — click to turn off"
            onClicked: Caffeine.active = false
        }

        Module { shown: Config.bar.modules.cpu; rich: true; text: `${bar.lbl("CPU")} ${SysInfo.cpu}%`; onScrolled: steps => bar.scrollAction(Config.bar.scrollStats, steps) }

        Module {
            shown: Config.bar.modules.ram
            onScrolled: steps => bar.scrollAction(Config.bar.scrollStats, steps)
            rich: true
            text: `${bar.lbl("RAM")} ${SysInfo.ram}%`
            tooltip: `${SysInfo.ramUsed.toFixed(1)} / ${SysInfo.ramTotal.toFixed(1)} GiB`
        }

        Module {
            shown: Config.bar.modules.gpu
            onScrolled: steps => bar.scrollAction(Config.bar.scrollStats, steps)
            rich: true
            text: `${bar.lbl("GPU")} ${SysInfo.gpu}%`
            tooltip: SysInfo.vramTotal ? `VRAM ${SysInfo.vramUsed.toFixed(1)} / ${SysInfo.vramTotal.toFixed(1)} GiB` : ""
        }

        Module {
            shown: Config.bar.modules.temp
            onScrolled: steps => bar.scrollAction(Config.bar.scrollStats, steps)
            text: `${SysInfo.cpuTemp}°C`
            tooltip: `CPU ${SysInfo.cpuTemp}°C   GPU ${SysInfo.gpuTemp}°C`
        }

        Module {
            shown: Config.bar.modules.volume
            fontSize: Config.bar.fontSize + 2
            padR: 0
            marR: 0
            text: !bar.audioReady ? "" : bar.sink.audio.muted ? "Muted"
                : ["", "", ""][Math.min(2, Math.floor(bar.volume / 33))] + "  " + bar.volume + "%"
            onClicked: b => {
                if (b === Qt.RightButton) bar.sink.audio.muted = !bar.sink.audio.muted
                else if (b === Qt.LeftButton) bar.shell.panelTab(4)
                else if (b === Qt.MiddleButton) bar.shell.openSettings("sound")
            }
            onScrolled: steps => bar.changeVolume(steps)
        }

        Module {
            shown: Config.bar.modules.network
            accent: true
            onScrolled: steps => bar.scrollAction(Config.bar.scrollStats, steps)
            fontSize: Config.bar.fontSize + 2
            padR: 7         // Qt's advance for the nerd glyph is 3px wider than GTK's
            text: !SysInfo.netUp ? String.fromCodePoint(0xf05aa) : SysInfo.netWifi ? "\uf1eb" : String.fromCodePoint(0xf0200)
            tooltip: SysInfo.netUp ? `${SysInfo.netIf}  ${SysInfo.netIp}\n⇣ ${SysInfo.rate(SysInfo.netDown)}  ⇡ ${SysInfo.rate(SysInfo.netUpRate)}` : ""
            onClicked: b => {
                if (b === Qt.LeftButton) bar.shell.panelTab(5)
                else if (b === Qt.MiddleButton) bar.shell.openSettings("network")
            }
        }

        // tray: padding 0 6px 0 10px, 14px icons 8px apart
        Item {
            visible: Config.bar.modules.tray && SystemTray.items.values.length > 0
            width: visible ? trayRow.implicitWidth + 16 : 0
            height: bar.ih
            Row {
                id: trayRow
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Repeater {
                    model: SystemTray.items
                    Item {
                        id: trayItem
                        required property SystemTrayItem modelData
                        readonly property string tooltip: modelData.tooltipTitle || modelData.title || ""
                        readonly property bool tooltipRich: false
                        width: Config.bar.trayIconSize
                        height: bar.ih
                        Rectangle {
                            anchors.centerIn: parent
                            width: 20; height: 20
                            radius: 6
                            visible: trayItem.modelData.status === Status.NeedsAttention
                            color: Theme.alpha(Theme.danger, 0.35)
                        }
                        IconImage {
                            anchors.centerIn: parent
                            implicitSize: Config.bar.trayIconSize
                            source: trayItem.modelData.icon
                        }
                        MouseArea {
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                            onContainsMouseChanged: containsMouse ? bar.showTip(trayItem) : bar.hideTip(trayItem)
                            onClicked: e => {
                                const it = trayItem.modelData
                                if (e.button === Qt.MiddleButton) it.secondaryActivate()
                                else if (e.button === Qt.RightButton || it.onlyMenu) {
                                    if (it.hasMenu || it.menu) bar.openTrayMenu(it, trayItem.mapToItem(null, trayItem.width / 2, 0).x)
                                } else it.activate()
                            }
                            onWheel: e => trayItem.modelData.scroll(e.angleDelta.y / 120, false)
                        }
                    }
                }
            }
        }

        Module {
            shown: Config.bar.modules.power
            accent: true
            onScrolled: steps => bar.scrollAction(Config.bar.scrollStats, steps)
            text: "⏻"
            fontSize: Config.bar.fontSize + 4
            grow: false
            padR: 6         // ⏻: Qt's advance is 4px wider than GTK's
            marR: 10
            onClicked: bar.run(["kusanagi", "msg", "power", "toggle"])
        }
    }

    // ---------------------------------------------------------------- media popover
    // hover the song (or `kusanagi msg bar media`) for cover + controls
    property bool mediaHover: false
    property bool mediaPopupHover: false
    property bool mediaPinned: false
    readonly property bool mediaWanted: Config.bar.mediaPopup && player !== null && Config.bar.modules.media
        && (mediaHover || mediaPopupHover || mediaPinned)
    onMediaWantedChanged: {
        if (mediaWanted) { mediaClose.stop(); if (!mediaLoader.active) mediaOpen.restart(); else mediaLoader.item.closing = false }
        else { mediaOpen.stop(); mediaClose.restart() }
    }
    onPlayerChanged: if (!player) mediaPinned = false
    Timer { id: mediaOpen; interval: bar.mediaPinned ? 0 : 280; onTriggered: mediaLoader.active = true }
    Timer {
        id: mediaClose
        interval: 260
        onTriggered: { if (mediaLoader.item) mediaLoader.item.closing = true; mediaUnload.restart() }
    }
    Timer { id: mediaUnload; interval: Config.ms(200); onTriggered: if (!bar.mediaWanted) mediaLoader.active = false }

    LazyLoader {
        id: mediaLoader
        MediaPopup {
            anchorItem: mediaModule
            player: bar.player
            onHoverChanged: inside => bar.mediaPopupHover = inside
        }
    }

    // ---------------------------------------------------------------- tray menus (drawn by Kusanagi)
    property var trayMenuFor: null
    property real trayMenuX: 0
    // IPC / keybind: menu of the n-th tray icon (from the left)
    function openTrayIndex(n) {
        const items = SystemTray.items.values
        if (n >= 0 && n < items.length) openTrayMenu(items[n], bar.width - 120)
    }
    function openTrayMenu(item, x) {
        if (trayMenuLoader.active && trayMenuFor === item) { trayMenuLoader.item.close(); return }
        trayMenuLoader.active = false
        trayMenuFor = item; trayMenuX = x
        trayMenuLoader.active = true
    }
    LazyLoader {
        id: trayMenuLoader
        TrayMenu {
            trayItem: bar.trayMenuFor
            anchorX: bar.trayMenuX
            onDismissed: { trayMenuLoader.active = false; bar.trayMenuFor = null }
        }
    }
}
