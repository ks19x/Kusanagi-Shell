// Bar.qml — hosts every bar: one BarWindow per (BarSpec bar, screen), plus what they share —
// tooltip, media popover, tray menus, audio / media state, and the built-in actions modules call.
// What the bars look like is data: BarSpec.qml (Config.bars, or the classic Settings → Bar options).
//
// Actions (module "click", "rightClick", "middleClick", "scrollUp", "scrollDown"):
//   panel[:home|sound|network|system|inbox|quick] · launcher · settings[:page] · power · wallpaper
//   clipboard · lock · notifs · dnd · caffeine · gamemode · preset:next · alt (toggle the alt format)
//   media:toggle|next|prev|popup · volume:up|down|mute · mic:up|down|mute · workspace:prev|next
//   none · anything else runs as a shell command
import Quickshell
import Quickshell.Services.Mpris
import Quickshell.Services.Pipewire
import Quickshell.Services.SystemTray
import QtQuick

Scope {
    id: host

    required property var shell     // shell.qml, for the control panel / settings panels
    readonly property var barHost: host
    Component.onCompleted: BarSpec.host = host

    Variants {
        model: BarSpec.instanceKeys(Quickshell.screens)
        BarWindow {
            required property var modelData
            key: modelData
            host: barHost        // `host` here would be the window's own property
        }
    }
    readonly property string primaryKey: BarSpec.instanceKeys(Quickshell.screens)[0] ?? ""

    // ---------------------------------------------------------------- shared state

    // mpris: the playing player, else the first
    readonly property MprisPlayer player: {
        const ps = Mpris.players.values
        return ps.find(p => p.isPlaying) ?? ps[0] ?? null
    }

    // volume (default sink), mic (default source — tracked only when a mic module is on a bar)
    readonly property PwNode sink: Pipewire.defaultAudioSink
    readonly property PwNode source: BarSpec.uses.mic ? Pipewire.defaultAudioSource : null
    PwObjectTracker { objects: [host.sink, host.source].filter(n => !!n) }
    readonly property bool audioReady: !!sink && sink.ready && !!sink.audio
    readonly property int volume: audioReady ? Math.round(sink.audio.volume * 100) : 0
    readonly property bool micReady: !!source && source.ready && !!source.audio
    readonly property int micVolume: micReady ? Math.round(source.audio.volume * 100) : 0
    function changeVolume(steps, step) {
        if (!audioReady) return
        sink.audio.volume = Math.max(0, Math.min(100, volume + (step || Config.bar.volumeStep) * steps)) / 100
    }
    function changeMic(steps, step) {
        if (!micReady) return
        source.audio.volume = Math.max(0, Math.min(100, micVolume + (step || Config.bar.volumeStep) * steps)) / 100
    }

    // the control panel grows out of the first clock's group
    property var clockGroup: null
    property var clockWin: null
    function registerClock(group, win) { if (!clockGroup) { clockGroup = group; clockWin = win } }
    function unregisterClock(group) { if (clockGroup === group) { clockGroup = null; clockWin = null } }
    // { x, y, w, h, edge } in screen coordinates, or null
    function clockRect() {
        const g = clockGroup, w = clockWin
        if (!g || !w || !g.visible) return null
        const p = g.box.mapToItem(null, 0, 0)
        return { x: p.x + w.screenX, y: p.y + w.screenY, w: g.box.width, h: g.box.height, edge: w.edge }
    }

    // something hangs off a bar: autohide bars stay out
    readonly property bool popupOpen: mediaLoader.active || trayMenuLoader.active || SysInfo.panelOpen

    // ---------------------------------------------------------------- actions

    function run(cmd) { Quickshell.execDetached(cmd) }
    function runAction(a, mod, steps) {
        if (a === undefined || a === null || a === "" || a === "none") return
        if (Array.isArray(a)) { run(a); return }
        const s = String(a), i = s.indexOf(":"), name = i > 0 ? s.slice(0, i) : s, arg = i > 0 ? s.slice(i + 1) : ""
        const tabs = { home: 0, sound: 4, network: 5, system: 1, inbox: 2, quick: 3 }
        const step = mod && mod.eff && mod.eff.step ? mod.eff.step : 0
        switch (name) {
        case "panel":
            if (arg in tabs) shell.panelTab(tabs[arg]); else { const p = shell.notifsPanel(); p.showing = !p.showing }
            return
        case "launcher": run(["kusanagi", "msg", "launcher", "toggle"]); return
        case "settings": shell.openSettings(arg || undefined); return
        case "power": run(["kusanagi", "msg", "power", "toggle"]); return
        case "wallpaper": run(["kusanagi", "msg", "wallpaper", "toggle"]); return
        case "clipboard": run(["kusanagi", "msg", "clipboard", "toggle"]); return
        case "lock": run(["kusanagi", "msg", "lock", "lock"]); return
        case "notifs": shell.panelTab(2); return
        case "dnd": Notifs.dnd = !Notifs.dnd; return
        case "caffeine": Caffeine.active = !Caffeine.active; return
        case "gamemode": run(["kusanagi", "msg", "gamemode", "toggle"]); return
        case "preset": if (arg === "next") Presets.next(); else Presets.applyNamed(arg); return
        case "alt": if (mod) mod.altOn = !mod.altOn; return
        case "media":
            if (arg === "popup") { mediaPinned = !mediaPinned; return }
            if (!player) return
            if (arg === "next") player.next(); else if (arg === "prev") player.previous(); else player.togglePlaying()
            return
        case "volume":
            if (arg === "mute") { if (audioReady) sink.audio.muted = !sink.audio.muted }
            else changeVolume(arg === "down" ? -1 : 1, step)
            return
        case "mic":
            if (!source && Pipewire.defaultAudioSource && arg === "mute") { const src = Pipewire.defaultAudioSource; if (src.audio) src.audio.muted = !src.audio.muted; return }
            if (arg === "mute") { if (micReady) source.audio.muted = !source.audio.muted }
            else changeMic(arg === "down" ? -1 : 1, step)
            return
        case "workspace": Wm.scroll(arg === "next" ? -1 : 1); return
        }
        run(["sh", "-c", s])
    }

    // ---------------------------------------------------------------- tooltip (one shared popup)

    property Item tipItem: null
    function showTip(item) {
        if (!item.tooltip && !item.hovered) return
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
    Timer { id: tipDelay; interval: 500; onTriggered: if (host.tipItem && host.tipItem.tooltip) tipLoader.active = true }

    LazyLoader {
        id: tipLoader
        PopupWindow {
            visible: true
            color: "transparent"
            readonly property string edge: host.tipItem && host.tipItem.win ? host.tipItem.win.edge : "top"
            anchor.item: host.tipItem
            anchor.rect.width: host.tipItem ? host.tipItem.width : 0
            anchor.rect.height: host.tipItem ? host.tipItem.height : 0
            anchor.edges: edge === "bottom" ? Edges.Top : edge === "left" ? Edges.Right : edge === "right" ? Edges.Left : Edges.Bottom
            anchor.gravity: anchor.edges
            anchor.adjustment: PopupAdjustment.Slide
            implicitWidth: tipText.implicitWidth + 20 + (edge === "left" || edge === "right" ? 6 : 0)
            implicitHeight: tipText.implicitHeight + 14 + (edge === "top" || edge === "bottom" ? 6 : 0)

            Rectangle {
                x: parent.edge === "left" ? 6 : 0
                y: parent.edge === "top" ? 6 : 0
                width: tipText.implicitWidth + 20
                height: tipText.implicitHeight + 14
                radius: 10
                color: Theme.alpha(Theme.bgPanel, 0.85)
                border.width: 1
                border.color: Theme.alpha(Theme.text, 0.08)
                Text {
                    id: tipText
                    anchors.centerIn: parent
                    text: host.tipItem ? host.tipItem.tooltip : ""
                    textFormat: host.tipItem && host.tipItem.tooltipRich ? Text.RichText : Text.PlainText
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.hintingPreference: Font.PreferFullHinting
                    renderType: Text.NativeRendering
                    font.pixelSize: 13
                    font.bold: true
                }
            }
        }
    }

    // ---------------------------------------------------------------- media popover
    // hover a media module (or `kusanagi msg bar media`) for cover + controls
    property Item mediaAnchor: null            // the media module hovered last (or the first one)
    property bool mediaHover: false
    property bool mediaPopupHover: false
    property bool mediaPinned: false
    readonly property bool mediaWanted: player !== null && mediaAnchor !== null && mediaAnchor.visible
        && mediaAnchor.eff.popup !== false && (mediaHover || mediaPopupHover || mediaPinned)
    onMediaWantedChanged: {
        if (mediaWanted) { mediaClose.stop(); if (!mediaLoader.active) mediaOpen.restart(); else mediaLoader.item.closing = false }
        else { mediaOpen.stop(); mediaClose.restart() }
    }
    onPlayerChanged: if (!player) mediaPinned = false
    Timer { id: mediaOpen; interval: host.mediaPinned ? 0 : 280; onTriggered: mediaLoader.active = true }
    Timer {
        id: mediaClose
        interval: 260
        onTriggered: { if (mediaLoader.item) mediaLoader.item.closing = true; mediaUnload.restart() }
    }
    Timer { id: mediaUnload; interval: Config.ms(200); onTriggered: if (!host.mediaWanted) mediaLoader.active = false }

    LazyLoader {
        id: mediaLoader
        MediaPopup {
            anchorItem: host.mediaAnchor
            edge: host.mediaAnchor && host.mediaAnchor.win ? host.mediaAnchor.win.edge : "top"
            player: host.player
            onHoverChanged: inside => host.mediaPopupHover = inside
        }
    }

    // ---------------------------------------------------------------- tray menus (drawn by Kusanagi)
    property var trayMenuFor: null
    property point trayMenuAt: Qt.point(0, 0)
    property string trayMenuEdge: "top"
    property real trayMenuInset: 28
    // IPC / keybind: menu of the n-th tray icon (from the left)
    function openTrayIndex(n) {
        const items = SystemTray.items.values
        if (n >= 0 && n < items.length) openTrayMenu(items[n], Qt.point((Quickshell.screens[0] ? Quickshell.screens[0].width : 1920) - 120, 0), BarSpec.edge, BarSpec.thickness)
    }
    // at = the icon's centre on screen; inset = how far the bar reaches into the screen
    function openTrayMenu(item, at, edge, inset) {
        if (trayMenuLoader.active && trayMenuFor === item) { trayMenuLoader.item.close(); return }
        trayMenuLoader.active = false
        trayMenuFor = item; trayMenuAt = at; trayMenuEdge = edge || "top"; trayMenuInset = inset || BarSpec.thickness
        trayMenuLoader.active = true
    }
    LazyLoader {
        id: trayMenuLoader
        TrayMenu {
            trayItem: host.trayMenuFor
            anchorX: host.trayMenuAt.x
            anchorY: host.trayMenuAt.y
            edge: host.trayMenuEdge
            inset: host.trayMenuInset
            onDismissed: { trayMenuLoader.active = false; host.trayMenuFor = null }
        }
    }
}
