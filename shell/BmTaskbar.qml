// BmTaskbar.qml — dock / taskbar: your pinned apps (Settings → Bar → Dock & taskbar, or right-click an
// icon) and whatever else is open. Grouped (one icon per app, dots for its windows) or one per window.
// Click = open / focus / next window · right-click = pin or unpin · middle = new window.
// Options: titles, titleWidth, iconSize, spacing, grouped, indicator (dot line none), magnify,
// pinned [ids] (this module only; default Config.dock.pinned), colors { active hover text indicator }.
// Provider for BarModule (see its header).
import Quickshell
import Quickshell.Wayland
import Quickshell.Widgets
import QtQuick

Item {
    id: p
    property var m
    readonly property var o: m ? m.eff : ({})
    readonly property bool visual: true
    readonly property var vars: ({})
    readonly property string format: ""
    readonly property bool vertical: m ? m.vertical : false
    readonly property bool titles: o.titles === true && !vertical
    readonly property bool grouped: o.grouped ?? (titles ? false : Config.dock.grouped)
    readonly property int iconSize: o.iconSize || 16
    readonly property int tw: o.titleWidth || 18
    readonly property string indicator: o.indicator || Config.dock.indicator
    readonly property real magnify: o.magnify ?? Config.dock.magnify
    readonly property var c: o.colors || ({})
    readonly property bool preview: m ? !!m.win.preview : false
    readonly property var pins: Array.isArray(o.pinned) ? o.pinned : Dock.pinned

    // what to show, as stable keys ("a:<app>" = an app, "w:<n>" = a window): the Repeater is only
    // rebuilt when this list really changes, not on every focus change
    property var keys: []
    readonly property var wanted: {
        const out = [], seen = {}
        for (const id of pins) { out.push("a:" + id); seen[id] = true }
        const tl = ToplevelManager.toplevels.values
        for (let i = 0; i < tl.length; i++) {
            const k = Dock.keyOf(tl[i])
            if (grouped) { if (!seen[k]) { seen[k] = true; out.push("a:" + k) } }
            else out.push("w:" + i)
        }
        // ungrouped: pinned apps that have windows show as their windows instead
        return grouped ? out : out.filter(x => !(x.startsWith("a:") && Dock.windowsOf(x.slice(2)).length))
    }
    onWantedChanged: if (JSON.stringify(wanted) !== JSON.stringify(keys)) keys = wanted
    Component.onCompleted: keys = wanted
    readonly property bool shown: keys.length > 0

    implicitWidth: grid.implicitWidth
    implicitHeight: grid.implicitHeight
    width: implicitWidth
    height: implicitHeight

    Grid {
        id: grid
        // exactly one line: n × 1 or 1 × n (spare rows would still take spacing)
        rows: p.vertical ? Math.max(1, p.keys.length) : 1
        columns: p.vertical ? 1 : Math.max(1, p.keys.length)
        flow: p.vertical ? Grid.TopToBottom : Grid.LeftToRight
        spacing: p.o.spacing !== undefined ? p.o.spacing : 4
        Repeater {
            model: p.keys
            Item {
                id: t
                required property string modelData
                readonly property bool isApp: modelData.startsWith("a:")
                readonly property string app: isApp ? modelData.slice(2) : Dock.keyOf(tl)
                readonly property var tl: isApp ? null : ToplevelManager.toplevels.values[parseInt(modelData.slice(2))] ?? null
                readonly property var wins: { void ToplevelManager.toplevels.values; return isApp ? Dock.windowsOf(app) : (tl ? [tl] : []) }
                readonly property bool running: wins.length > 0
                readonly property bool active: wins.some(w => w.activated)
                readonly property string title: tl ? (tl.title || tl.appId || "") : Dock.name(app)
                readonly property string tooltip: (isApp ? Dock.name(app) : title) + (wins.length > 1 ? "  ·  " + wins.length + " windows" : "")
                    + (Dock.isPinned(app) ? "\nright-click: unpin" : "\nright-click: pin to the dock")
                readonly property bool tooltipRich: false
                readonly property bool hovered: area.containsMouse
                readonly property var win: p.m ? p.m.win : null    // the bar (tooltips open away from its edge)
                readonly property real cross: p.m ? p.m.innerCross : 22
                width: p.vertical ? cross : row.implicitWidth + 12
                height: p.vertical ? p.iconSize + 12 : cross

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 6
                    color: t.active && p.titles ? BarSpec.color(p.c.active, "text/0.14") : t.hovered && p.titles ? BarSpec.color(p.c.hover, "text/0.07") : "transparent"
                    Behavior on color { ColorAnimation { duration: 140 } }
                }
                Row {
                    id: row
                    anchors.centerIn: parent
                    spacing: 6
                    // no icon anywhere: the app's first letter on a tile
                    Rectangle {
                        visible: ic.source == ""
                        width: p.iconSize; height: p.iconSize
                        radius: p.iconSize / 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: BarSpec.color("accent/0.3")
                        scale: ic.scale
                        transformOrigin: ic.transformOrigin
                        CpText {
                            anchors.centerIn: parent
                            text: (Dock.name(t.app) || t.app || "?").charAt(0).toUpperCase()
                            font.pixelSize: Math.round(p.iconSize * 0.55)
                            font.bold: true
                        }
                    }
                    IconImage {
                        id: ic
                        visible: source != ""
                        implicitSize: p.iconSize
                        source: Dock.icon(t.app)
                        anchors.verticalCenter: parent.verticalCenter
                        opacity: t.running || !p.titles ? 1 : 0.75
                        // grow towards the screen's middle, away from the bar's edge
                        transformOrigin: !t.win ? Item.Center : t.win.edge === "bottom" ? Item.Bottom : t.win.edge === "top" ? Item.Top
                            : t.win.edge === "left" ? Item.Left : Item.Right
                        scale: t.hovered && !p.titles ? p.magnify : 1
                        Behavior on scale { NumberAnimation { duration: Config.ms(180); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(1.4) } }
                    }
                    Text {
                        visible: p.titles
                        anchors.verticalCenter: parent.verticalCenter
                        text: t.title.length > p.tw ? t.title.slice(0, p.tw - 1) + "…" : t.title
                        color: BarSpec.color(p.c.text, "text")
                        opacity: t.active ? 1 : 0.7
                        font.family: p.m ? p.m.eff.font || p.m.win.spec.font : Theme.fontFamily
                        font.pixelSize: p.m ? p.m.baseFont : 11
                        renderType: Text.NativeRendering
                    }
                }
                // running: dots (one per window, up to 3) or a line — under the icon, or on a side bar's outer side
                Row {
                    visible: p.indicator === "dot" && t.running
                    spacing: 3
                    readonly property bool side: !!t.win && t.win.vertical
                    x: side ? (t.win.edge === "left" ? 1 : t.width - 5) : (t.width - width) / 2
                    y: side ? (t.height - 4) / 2 : t.height - 5
                    Repeater {
                        model: Math.min(3, t.wins.length)
                        Rectangle { width: 4; height: 4; radius: 2; color: t.active ? BarSpec.color(p.c.indicator, "accent") : BarSpec.color(p.c.text, "text/0.6") }
                    }
                }
                Rectangle {
                    visible: p.indicator === "line" && t.running
                    readonly property bool side: !!t.win && t.win.vertical
                    width: side ? 2 : (t.active ? t.width - 10 : 10)
                    height: side ? (t.active ? t.height - 10 : 10) : 2
                    radius: 1
                    x: side ? (t.win.edge === "left" ? 0 : t.width - 2) : (t.width - width) / 2
                    y: side ? (t.height - height) / 2 : t.height - 2
                    color: t.active ? BarSpec.color(p.c.indicator, "accent") : BarSpec.color(p.c.text, "text/0.5")
                    Behavior on width { NumberAnimation { duration: Config.ms(200); easing.type: Easing.OutCubic } }
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !p.preview
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    onClicked: e => {
                        if (e.button === Qt.RightButton) Dock.toggle(t.app)
                        else if (e.button === Qt.MiddleButton) Dock.launch(t.app)
                        else if (t.tl) t.tl.activate()
                        else Dock.activate(t.app)
                    }
                    onContainsMouseChanged: containsMouse ? p.m.host.showTip(t) : p.m.host.hideTip(t)
                }
            }
        }
    }
}
