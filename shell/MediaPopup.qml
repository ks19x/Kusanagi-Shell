// MediaPopup.qml — hover card under the bar's song: cover, title/artist, seekable progress,
// prev / play / next and which player it is. Kept open while the pointer is on it.
import Quickshell
import Quickshell.Widgets
import Quickshell.Services.Mpris
import QtQuick
import QtQuick.Effects

PopupWindow {
    id: root

    required property Item anchorItem
    required property MprisPlayer player
    property bool closing: false
    signal hoverChanged(bool inside)

    anchor.item: anchorItem
    anchor.rect.width: anchorItem.width
    readonly property bool above: Config.bar.position === "bottom"
    anchor.rect.y: above ? -6 : 0
    anchor.rect.height: above ? 0 : anchorItem.height + 6
    anchor.edges: above ? Edges.Top : Edges.Bottom
    anchor.gravity: above ? Edges.Top : Edges.Bottom
    anchor.adjustment: PopupAdjustment.Slide
    implicitWidth: 360 + 24
    implicitHeight: 132 + 24
    color: "transparent"
    visible: true

    function fmt(sec) {
        if (!(sec > 0)) return "0:00"
        const m = Math.floor(sec / 60), s = Math.floor(sec % 60)
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    // MPRIS doesn't push the position; nudge it while visible and playing
    Timer {
        running: root.player !== null && root.player.isPlaying
        interval: 1000; repeat: true; triggeredOnStart: true
        onTriggered: root.player.positionChanged()
    }

    RectangularShadow {

        visible: Config.look.shadows
        anchors.fill: card
        opacity: card.opacity
        blur: 20
        offset.y: 6
        radius: card.radius
        color: Theme.alpha("#000000", 0.4)
    }

    Rectangle {
        id: card
        x: 12
        width: 360
        height: 132
        radius: Math.max(8, Config.look.radius - 2)
        color: Theme.alpha(Theme.bgPanel, Config.panel.opacity)
        border.width: Theme.surfaceBorderWidth
        border.color: Theme.surfaceBorder

        // drops in, lifts away
        y: root.closing || !shown ? 4 : 12
        opacity: root.closing || !shown ? 0 : 1
        property bool shown: false
        Component.onCompleted: shown = true
        Behavior on y { NumberAnimation { duration: Config.ms(260); easing.type: Easing.OutQuint } }
        Behavior on opacity { NumberAnimation { duration: Config.ms(180) } }

        HoverHandler { onHoveredChanged: root.hoverChanged(hovered) }

        ClippingRectangle {
            id: art
            x: 14; y: 14
            width: 104; height: 104
            radius: Math.max(6, Config.look.radius - 8)
            color: Theme.alpha(Theme.text, 0.08)
            CpIcon { anchors.centerIn: parent; cp: 0xf075a; font.pixelSize: 30; color: Theme.textDim; visible: cover.status !== Image.Ready }
            Image {
                id: cover
                anchors.fill: parent
                source: root.player ? root.player.trackArtUrl : ""
                fillMode: Image.PreserveAspectCrop
                sourceSize: Qt.size(208, 208)
                asynchronous: true
            }
        }

        Column {
            anchors { left: art.right; leftMargin: 14; right: parent.right; rightMargin: 14; top: parent.top; topMargin: 14 }
            spacing: 2

            CpText {
                width: parent.width
                text: root.player ? (root.player.identity || "") : ""
                font.pixelSize: 9
                font.bold: true
                font.letterSpacing: 1.5
                color: Theme.accent
                elide: Text.ElideRight
            }
            CpText {
                width: parent.width
                text: root.player ? (root.player.trackTitle || "Unknown") : ""
                font.pixelSize: 13
                font.bold: true
                elide: Text.ElideRight
            }
            CpText {
                width: parent.width
                text: root.player ? (root.player.trackArtist || "") : ""
                font.pixelSize: 11
                color: Theme.textDim
                elide: Text.ElideRight
            }
        }

        // progress + times
        Item {
            id: progress
            anchors { left: art.right; leftMargin: 14; right: parent.right; rightMargin: 14 }
            y: 72
            height: 14
            readonly property real frac: root.player && root.player.length > 0 ? Math.min(1, root.player.position / root.player.length) : 0

            Rectangle {
                id: track
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 4; radius: 2
                color: Theme.alpha(Theme.text, 0.12)
                Rectangle {
                    width: parent.width * progress.frac
                    height: parent.height; radius: 2
                    color: Theme.accent
                    Behavior on width { NumberAnimation { duration: Config.ms(900); easing.type: Easing.Linear } }
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                enabled: root.player !== null && root.player.canSeek
                onClicked: e => root.player.position = root.player.length * Math.max(0, Math.min(1, e.x / width))
            }
        }
        CpText {
            anchors { left: art.right; leftMargin: 14; top: progress.bottom }
            text: root.player ? root.fmt(root.player.position) : ""
            font.pixelSize: 9
            color: Theme.textDim
        }
        CpText {
            anchors { right: parent.right; rightMargin: 14; top: progress.bottom }
            text: root.player ? root.fmt(root.player.length) : ""
            font.pixelSize: 9
            color: Theme.textDim
        }

        Row {
            anchors { horizontalCenter: progress.horizontalCenter; bottom: parent.bottom; bottomMargin: 8 }
            spacing: 6
            CpIconButton { width: 30; height: 30; icon: 0xf04ae; onClicked: root.player.previous() }
            CpIconButton {
                width: 34; height: 34
                icon: root.player && root.player.isPlaying ? 0xf03e4 : 0xf040a
                iconSize: 17
                filled: true
                onClicked: root.player.togglePlaying()
            }
            CpIconButton { width: 30; height: 30; icon: 0xf04ad; onClicked: root.player.next() }
        }
    }
}
