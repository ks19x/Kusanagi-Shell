// BmMedia.qml — now playing (MPRIS). Options: width (chars), marquee, popup (hover card).
// Vars: track (scrolled), title, artist, album, player. Status: playing | paused.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import Quickshell.Services.Mpris
import QtQuick

Item {
    id: p
    property var m
    readonly property var player: m ? m.host.player : null
    readonly property int w: m && m.eff.width ? m.eff.width : Config.bar.mediaWidth
    readonly property bool scroll: m && m.eff.marquee !== undefined ? m.eff.marquee : Config.bar.marquee
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
        running: p.scroll && !GameMode.quiet && p.player !== null && p.player.isPlaying && p.track.length > p.w
        onTriggered: p.marquee = (p.marquee + 1) % (p.track.length + 5)
    }
    readonly property string view: {
        if (!scroll || track.length <= w || !(player && player.isPlaying)) return track.slice(0, w)
        const loop = track + "     "
        return (loop.slice(marquee) + loop.slice(0, marquee)).slice(0, w)
    }
    readonly property var vars: ({ track: view, title: player ? player.trackTitle || "" : "", artist: player ? player.trackArtist || "" : "",
                                   album: player ? player.trackAlbum || "" : "", player: player ? player.identity || "" : "" })
    readonly property string format: "♪  {track}"
    readonly property string status: player ? (player.isPlaying ? "playing" : "paused") : ""
    readonly property bool shown: track !== ""
    readonly property var actions: ({ click: "media:toggle", rightClick: "media:next", middleClick: "media:prev" })
    readonly property string tooltip: m && m.eff.popup === false ? track : ""
    // hovering opens the media card under this module
    readonly property bool hov: m ? m.hovered : false
    onHovChanged: { if (hov) m.host.mediaAnchor = m; if (m) m.host.mediaHover = hov }
    onMChanged: if (m && !m.host.mediaAnchor) m.host.mediaAnchor = m
    Component.onDestruction: if (m && m.host.mediaAnchor === m) m.host.mediaAnchor = null
}
