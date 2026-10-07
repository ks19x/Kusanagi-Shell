// WdgMedia.qml — now playing: cover, title, artist, progress, prev / play / next. Hidden with no player.
import Quickshell
import Quickshell.Services.Mpris
import Quickshell.Widgets
import QtQuick

Item {
    id: m
    property var w
    readonly property MprisPlayer player: { const ps = Mpris.players.values; return ps.find(p => p.isPlaying) ?? ps[0] ?? null }
    readonly property bool shown: player !== null && (player.trackTitle || "") !== ""
    implicitWidth: 360
    implicitHeight: 112
    width: implicitWidth
    height: implicitHeight
    // MPRIS doesn't announce position: ask once a second while playing (and only while shown)
    Timer { interval: 1000; repeat: true; running: m.shown && m.player.isPlaying; onTriggered: m.player.positionChanged() }

    ClippingRectangle {
        id: cover
        width: 112; height: 112; radius: 14
        color: Theme.alpha(Theme.text, 0.08)
        Image { anchors.fill: parent; source: m.player ? m.player.trackArtUrl : ""; fillMode: Image.PreserveAspectCrop; asynchronous: true; sourceSize: Qt.size(224, 224) }
        CpIcon { anchors.centerIn: parent; visible: !m.player || !m.player.trackArtUrl; cp: 0xf075a; font.pixelSize: 40; color: Theme.textDim }
    }
    Column {
        anchors { left: cover.right; leftMargin: 18; right: parent.right; verticalCenter: parent.verticalCenter }
        spacing: 4
        CpText { width: parent.width; elide: Text.ElideRight; text: m.player ? m.player.trackTitle : ""; font.pixelSize: 16; font.bold: true }
        CpText { width: parent.width; elide: Text.ElideRight; text: m.player ? (m.player.trackArtist || m.player.identity) : ""; font.pixelSize: 12; color: Theme.textDim }
        Item { width: 1; height: 6 }
        Rectangle {
            width: parent.width; height: 4; radius: 2
            color: Theme.alpha(Theme.text, 0.15)
            Rectangle {
                height: parent.height; radius: 2; color: Theme.accent
                width: m.player && m.player.length > 0 ? parent.width * Math.min(1, m.player.position / m.player.length) : 0
                Behavior on width { NumberAnimation { duration: 900 } }
            }
        }
        Row {
            spacing: 6
            topPadding: 4
            CpIconButton { width: 30; height: 30; icon: 0xf04ae; onClicked: m.player.previous() }
            CpIconButton { width: 30; height: 30; filled: true; icon: m.player && m.player.isPlaying ? 0xf03e4 : 0xf040a; onClicked: m.player.togglePlaying() }
            CpIconButton { width: 30; height: 30; icon: 0xf04ad; onClicked: m.player.next() }
        }
    }
}
