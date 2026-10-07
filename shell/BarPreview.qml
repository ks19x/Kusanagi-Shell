// BarPreview.qml — a live, scaled drawing of one bar spec (raw, as in Config.bars) on a slice of
// "desktop": the real BarContent with a stand-in window and a host that only borrows live data
// (media, volume) and ignores clicks. Settings → Bar editor and preset cards use it.
import Quickshell
import Quickshell.Io
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
    // drag and drop (pickable only): from = "sec:gi:mi"; to = a module's key, or "section:<name>" (append)
    signal moved(string from, string to, bool after)

    // ---- drag and drop bookkeeping ----
    property var regs: []                        // modules and groups drawn in this preview
    property string dragKey: ""
    property string dragLabel: ""
    property point dragAt: Qt.point(0, 0)
    property var target: null                    // { key, after, x, y, w, h }
    function reg(it) { if (regs.indexOf(it) < 0) regs = regs.concat([it]) }
    function unreg(it) { regs = regs.filter(x => x !== it) }
    function hit(p) {
        const dk = dragKey, dragGroup = dk.endsWith(":-1") && dk.split(":").length === 3
        let best = null, bestD = Infinity
        for (const it of regs) {
            if (!it.visible || !it.pathKey || it.pathKey === dk) continue
            // not into itself, and a group never into another group
            const k = it.pathKey.split(":"), dkp = dk.split(":")
            if (k[0] === dkp[0] && k[1] === dkp[1] && dkp[2] === "-1") continue
            if (dragGroup && k[2] !== "-1") continue
            const r = it.mapToItem(pv, 0, 0, it.width, it.height)
            const along = vertical ? p.y : p.x, lo = vertical ? r.y : r.x, len = vertical ? r.height : r.width
            const d = along < lo ? lo - along : along > lo + len ? along - lo - len : 0
            if (d < bestD) { bestD = d; best = { key: it.pathKey, after: along > lo + len / 2, x: r.x, y: r.y, w: r.width, h: r.height } }
        }
        // far from everything: drop at the end of the section under the pointer
        if (!best || bestD > 60) {
            const f = (vertical ? p.y / height : p.x / width)
            const sec = f < 0.34 ? "start" : f > 0.66 ? "end" : "center"
            return { key: "section:" + sec, after: true, x: vertical ? 0 : width * (sec === "start" ? 0.02 : sec === "center" ? 0.5 : 0.98), y: vertical ? height * (sec === "start" ? 0.02 : sec === "center" ? 0.5 : 0.98) : 0, w: 0, h: vertical ? 0 : height }
        }
        return best
    }
    function dragBegin(key, label) { dragKey = key; dragLabel = label }
    function dragMove(sx, sy) { dragAt = pv.mapFromItem(null, sx, sy); target = hit(dragAt) }
    function dragEnd() {
        if (dragKey && target && target.key !== dragKey) moved(dragKey, target.key, target.after)
        dragKey = ""; target = null
    }
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
        readonly property string dragKey: pv.dragKey
        readonly property bool canDrag: pv.pickable
        function reg(it) { pv.reg(it) }
        function unreg(it) { pv.unreg(it) }
        function dragBegin(k, l) { pv.dragBegin(k, l) }
        function dragMove(x, y) { pv.dragMove(x, y) }
        function dragEnd() { pv.dragEnd() }
        readonly property var pick: pv.pickable ? (s, g, m) => pv.picked(s, g, m) : null
        function runAction() {}
        function showTip() {}
        function hideTip() {}
        function registerClock() {}
        function unregisterClock() {}
        function openTrayMenu() {}
    }

    // snapshot (galleries, preset cards): a picture instead of a live bar. Pictures are cached on disk
    // by layout + colours + font, so after the first time no live bar is built at all.
    property bool snapshot: false
    readonly property string cacheKey: {
        if (width <= 0 || height <= 0) return ""      // laid out first: the size is part of the key
        const src = JSON.stringify(bar || {}) + "|" + Theme.accent + Theme.accent2 + Theme.bgPanel + Theme.text + Theme.fontFamily
                    + "|" + Math.round(width) + "x" + Math.round(height)
        let h = 5381
        for (let i = 0; i < src.length; i++) h = ((h << 5) + h + src.charCodeAt(i)) | 0
        return (h >>> 0).toString(16)
    }
    readonly property string cacheFile: Quickshell.env("HOME") + "/.cache/kusanagi/previews/" + cacheKey + ".png"
    // live until a picture is showing (a cached one, or the one just grabbed)
    property bool live: !snapshot
    // look it up once the size has settled (cards resize a few times while they are laid out)
    onCacheKeyChanged: if (snapshot && cacheKey) { live = false; settle.restart() }
    Timer { id: settle; interval: 120; onTriggered: shot.source = "file://" + pv.cacheFile }
    Component.onCompleted: if (snapshot) mk.running = true
    Loader {
        id: liveLoader
        anchors.fill: parent
        active: pv.live
        sourceComponent: Item {
            anchors.fill: parent
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
    }
    Image {
        id: shot
        anchors.fill: parent
        visible: !pv.live && status === Image.Ready
        cache: false
        asynchronous: true
        // not cached yet: build it live once, then grab and keep the picture
        onStatusChanged: if (pv.snapshot && status === Image.Error && source == "file://" + pv.cacheFile) pv.live = true
    }
    Timer {
        id: grab
        interval: 450
        running: pv.snapshot && pv.live && pv.width > 0
        onTriggered: liveLoader.grabToImage(r => {
            r.saveToFile(pv.cacheFile)
            shot.source = r.url
            pv.live = false
        })
    }
    Process { id: mk; command: ["mkdir", "-p", Quickshell.env("HOME") + "/.cache/kusanagi/previews"] }

    // ---- drag feedback: where it will land, and what is being carried ----
    Rectangle {
        visible: pv.dragKey !== "" && !!pv.target
        color: Theme.accent
        radius: 1.5
        width: pv.vertical ? (pv.target ? Math.max(pv.target.w, 20) : 0) : 3
        height: pv.vertical ? 3 : (pv.target ? Math.max(pv.target.h, 14) : 0)
        x: !pv.target ? 0 : pv.vertical ? pv.target.x : (pv.target.after ? pv.target.x + pv.target.w : pv.target.x) - 1.5
        y: !pv.target ? 0 : pv.vertical ? (pv.target.after ? pv.target.y + pv.target.h : pv.target.y) - 1.5 : pv.target.y
        Behavior on x { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
        Behavior on y { NumberAnimation { duration: 90; easing.type: Easing.OutCubic } }
    }
    Rectangle {
        visible: pv.dragKey !== ""
        x: pv.dragAt.x + 10; y: pv.dragAt.y - height - 4
        width: ghost.implicitWidth + 16; height: 22; radius: 11
        color: Theme.accent
        CpText { id: ghost; anchors.centerIn: parent; text: pv.dragLabel || "move"; color: Theme.bgPanel; font.pixelSize: 11; font.bold: true; textFormat: Text.PlainText }
    }
}
