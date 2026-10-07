// BarPreview.qml — a live, scaled drawing of one bar spec (raw, as in Config.bars) on a slice of
// "desktop": the real BarContent with a stand-in window and a host that only borrows live data
// (media, volume) and ignores clicks. Settings → Bar editor and preset cards use it.
import QtQuick

Item {
    id: pv

    property var bar: ({})                       // a raw bar spec (normalised here)
    property real screenW: Math.max(800, Math.round(width / 0.62))   // the virtual screen it is drawn on
    property real screenH: 720
    property bool desktop: true                  // draw the wallpaper-ish backdrop
    property real fixedScale: 0                  // > 0: draw at this scale and show the top-left of it
    property bool pickable: false                // clicks select modules (picked) instead of doing things
    property string selKey: ""                   // "section:entry:module" outlined
    signal picked(string section, int gi, int mi)
    clip: true

    // plain JS first (QML hands over list/map types Array.isArray etc. do not know)
    readonly property var spec: BarSpec.normBar(JSON.parse(JSON.stringify(bar || {})), 0)
    readonly property bool vertical: spec.position === "left" || spec.position === "right"
    readonly property real thick: spec.size + spec.margin[0] + spec.margin[1]
    // how much of the virtual screen is shown: a strip along the bar's edge
    readonly property real viewW: vertical ? Math.max(thick + 60, screenH * width / Math.max(1, height)) : screenW
    readonly property real viewH: vertical ? screenH : thick + 16
    readonly property real k: fixedScale > 0 ? fixedScale : Math.min(width / viewW, height / viewH)

    QtObject {
        id: fakeHost
        readonly property var real: BarSpec.host
        readonly property var player: real ? real.player : null
        readonly property bool audioReady: real ? real.audioReady : false
        readonly property var sink: real ? real.sink : null
        readonly property int volume: real ? real.volume : 0
        readonly property bool micReady: real ? real.micReady : false
        readonly property var source: real ? real.source : null
        readonly property int micVolume: real ? real.micVolume : 0
        property var clockGroup: null
        property var mediaAnchor: null
        property bool mediaHover: false
        readonly property bool popupOpen: false
        readonly property string selKey: pv.selKey
        readonly property var pick: pv.pickable ? (s, g, m) => pv.picked(s, g, m) : null
        function runAction() {}
        function showTip() {}
        function hideTip() {}
        function registerClock() {}
        function unregisterClock() {}
        function openTrayMenu() {}
    }

    Item {
        id: screen
        width: pv.viewW
        height: pv.viewH
        scale: pv.k
        transformOrigin: Item.TopLeft
        x: pv.fixedScale > 0 ? 0 : Math.round((pv.width - width * pv.k) / 2)
        y: pv.fixedScale > 0 ? 0 : Math.round((pv.height - height * pv.k) / 2)

        Rectangle {
            visible: pv.desktop
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.darker(Theme.accent, 2.6) }
                GradientStop { position: 1; color: Qt.darker(Theme.accent2, 3.4) }
            }
        }

        // the window's place on the virtual screen, as the compositor would put it
        QtObject {
            id: fakeWin
            readonly property bool preview: true
            readonly property var spec: pv.spec
            readonly property string edge: pv.spec.position
            readonly property bool vertical: pv.vertical
            readonly property int size: pv.spec.size
            readonly property var margin: pv.spec.margin
            readonly property var host: fakeHost
            readonly property real screenX: 0
            readonly property real screenY: 0
            readonly property string key: "preview"
        }
        readonly property real full: pv.vertical ? pv.screenH : pv.screenW
        readonly property real len: pv.spec.length === "auto" ? Math.max(barc.natural, pv.spec.size)
            : pv.spec.length <= 0 ? full - 2 * pv.spec.margin[2]
            : pv.spec.length <= 1 ? Math.round(full * pv.spec.length) : pv.spec.length
        readonly property real at: pv.spec.length !== "auto" && pv.spec.length <= 0 || pv.spec.align === "start" ? pv.spec.margin[2]
            : pv.spec.align === "end" ? full - pv.spec.margin[2] - len : Math.round((full - len) / 2)

        BarContent {
            id: barc
            win: fakeWin
            width: pv.vertical ? pv.spec.size : screen.len
            height: pv.vertical ? screen.len : pv.spec.size
            x: pv.vertical ? (fakeWin.edge === "left" ? pv.spec.margin[0] : screen.width - pv.spec.margin[0] - width) : screen.at
            y: pv.vertical ? screen.at : (fakeWin.edge === "top" ? pv.spec.margin[0] : screen.height - pv.spec.margin[0] - height)
        }
    }
}
