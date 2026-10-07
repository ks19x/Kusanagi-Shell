// BarWindow.qml — one bar (BarSpec.bars[i]) on one screen. Bar.qml makes one per (bar, screen).
//   position top|bottom|left|right · size = thickness · length 0 = full, ≤1 = fraction, "auto" = fit, else px
//   align start|center|end (short bars) · margin [edge, inner, sides] (floating) · padding at the ends
//   exclusive true | false | px · layer top|bottom|overlay · autohide (slides away, comes back at the edge)
import Quickshell
import Quickshell.Wayland
import QtQuick

PanelWindow {
    id: win

    required property string key            // "<bar index>|<screen name>"
    required property var host

    readonly property int bi: parseInt(key)
    readonly property string screenName: key.slice(key.indexOf("|") + 1)
    screen: Quickshell.screens.find(s => s.name === screenName) ?? null

    readonly property var barWindow: win
    readonly property var spec: BarSpec.bars[bi] ?? BarSpec.bars[0]
    readonly property string edge: spec.position
    readonly property bool vertical: edge === "left" || edge === "right"
    readonly property int size: spec.size
    readonly property var margin: spec.margin

    readonly property real screenLen: screen ? (vertical ? screen.height : screen.width) : 1920
    readonly property bool fit: spec.length === "auto"
    readonly property real len: fit ? Math.max(content.natural, size) : spec.length <= 0 ? 0 : spec.length <= 1 ? Math.round(screenLen * spec.length) : spec.length
    readonly property bool full: len === 0

    anchors {
        top: edge === "top" || (vertical && (full || spec.align === "start"))
        bottom: edge === "bottom" || (vertical && (full || spec.align === "end"))
        left: edge === "left" || (!vertical && (full || spec.align === "start"))
        right: edge === "right" || (!vertical && (full || spec.align === "end"))
    }
    margins {
        top: edge === "top" ? margin[0] : vertical ? margin[2] : 0
        bottom: edge === "bottom" ? margin[0] : vertical ? margin[2] : 0
        left: edge === "left" ? margin[0] : !vertical ? margin[2] : 0
        right: edge === "right" ? margin[0] : !vertical ? margin[2] : 0
    }
    implicitHeight: vertical ? (full ? 0 : len) : size
    implicitWidth: vertical ? size : (full ? 0 : len)

    // the compositor adds the edge margin itself; the inner margin is extra room before windows
    exclusionMode: spec.exclusive === false || spec.autohide ? ExclusionMode.Ignore : ExclusionMode.Normal
    exclusiveZone: typeof spec.exclusive === "number" ? spec.exclusive : size + margin[1]
    color: "transparent"
    WlrLayershell.namespace: "quickshell-bar"
    WlrLayershell.layer: spec.layer === "bottom" ? WlrLayer.Bottom : spec.layer === "overlay" ? WlrLayer.Overlay
                       : spec.layer === "background" ? WlrLayer.Background : WlrLayer.Top

    // where this window's top-left sits on its screen (the control panel morphs from screen coords)
    readonly property real screenX: edge === "right" ? (screen ? screen.width : 0) - margin[0] - width
        : edge === "left" ? margin[0] : full || spec.align === "start" ? margin[2]
        : spec.align === "end" ? (screen ? screen.width : 0) - margin[2] - width : Math.round(((screen ? screen.width : 0) - width) / 2)
    readonly property real screenY: edge === "bottom" ? (screen ? screen.height : 0) - margin[0] - height
        : edge === "top" ? margin[0] : full || spec.align === "start" ? margin[2]
        : spec.align === "end" ? (screen ? screen.height : 0) - margin[2] - height : Math.round(((screen ? screen.height : 0) - height) / 2)

    // caffeine: one always-mapped window carries the idle inhibitor
    IdleInhibitor { window: win; enabled: Caffeine.active && win.host.primaryKey === win.key }

    // ---- autohide: slide out, keep a 2px strip at the edge that brings it back ----
    property bool revealed: !spec.autohide || hover.hovered || host.popupOpen
    HoverHandler { id: hover }
    Timer { id: hideLater; interval: 450; onTriggered: win.shown = win.revealed }
    property bool shown: true
    onRevealedChanged: if (revealed) { hideLater.stop(); shown = true } else hideLater.restart()
    readonly property real slide: shown ? 0 : size - 2
    // no mask unless hidden: a Region without an item is an EMPTY mask (every click falls through)
    mask: spec.autohide && !win.shown ? hotRegion : null
    Region { id: hotRegion; item: hotStrip }
    Item {
        id: hotStrip
        x: win.edge === "right" ? win.width - 2 : 0
        y: win.edge === "bottom" ? win.height - 2 : 0
        width: win.vertical ? 2 : win.width
        height: win.vertical ? win.height : 2
    }

    Item {
        id: body
        width: win.width
        height: win.height
        x: win.edge === "left" ? -win.slideAnim : win.edge === "right" ? win.slideAnim : 0
        y: win.edge === "top" ? -win.slideAnim : win.edge === "bottom" ? win.slideAnim : 0

        BarContent { id: content; anchors.fill: parent; win: barWindow }   // `win` here would be its own property
    }
    property real slideAnim: slide
    Behavior on slideAnim { NumberAnimation { duration: Config.ms(220); easing.type: Easing.OutCubic } }
}
