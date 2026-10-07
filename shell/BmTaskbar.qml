// BmTaskbar.qml — open windows (all of them, not per workspace): app icon, optional title.
// Options: titles (bool), titleWidth (chars), iconSize, spacing, colors { active, hover, text }.
// Click = focus, middle = close. Provider for BarModule (see its header).
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
    readonly property int iconSize: o.iconSize || 16
    readonly property int tw: o.titleWidth || 18
    readonly property var c: o.colors || ({})
    readonly property bool shown: ToplevelManager.toplevels.values.length > 0
    implicitWidth: grid.implicitWidth
    implicitHeight: grid.implicitHeight
    width: implicitWidth
    height: implicitHeight

    function iconFor(appId) {
        const e = DesktopEntries.heuristicLookup(appId)
        return Quickshell.iconPath(e && e.icon ? e.icon : appId, "application-x-executable")
    }

    Grid {
        id: grid
        rows: 1000
        columns: 1000         // one line either way: flow picks the direction
        flow: p.vertical ? Grid.TopToBottom : Grid.LeftToRight
        spacing: p.o.spacing !== undefined ? p.o.spacing : 4
        Repeater {
            model: ToplevelManager.toplevels
            Item {
                id: t
                required property var modelData
                readonly property bool active: modelData.activated
                readonly property string title: modelData.title || modelData.appId || ""
                readonly property string tooltip: title
                readonly property bool tooltipRich: false
                readonly property bool hovered: area.containsMouse
                readonly property var win: p.m ? p.m.win : null
                readonly property real cross: p.m ? p.m.innerCross : 22
                width: p.vertical ? cross : row.implicitWidth + 12
                height: p.vertical ? p.iconSize + 10 : cross
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 6
                    color: t.active ? BarSpec.color(p.c.active, "text/0.14") : t.hovered ? BarSpec.color(p.c.hover, "text/0.07") : "transparent"
                    Behavior on color { ColorAnimation { duration: 140 } }
                }
                Row {
                    id: row
                    anchors.centerIn: parent
                    spacing: 6
                    IconImage { implicitSize: p.iconSize; source: p.iconFor(t.modelData.appId); anchors.verticalCenter: parent.verticalCenter }
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
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !(p.m && p.m.win.preview)
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    onClicked: e => e.button === Qt.MiddleButton ? t.modelData.close() : t.modelData.activate()
                    onContainsMouseChanged: containsMouse ? p.m.host.showTip(t) : p.m.host.hideTip(t)
                }
            }
        }
    }
}
