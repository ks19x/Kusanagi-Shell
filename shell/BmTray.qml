// BmTray.qml — system tray. Options: iconSize, spacing.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import Quickshell
import Quickshell.Services.SystemTray
import Quickshell.Widgets
import QtQuick

Item {
    id: p
    property var m
    readonly property bool visual: true
    readonly property var vars: ({})
    readonly property string format: ""
    readonly property bool shown: SystemTray.items.values.length > 0
    readonly property int iconSize: m && m.eff.iconSize ? m.eff.iconSize : Config.bar.trayIconSize
    readonly property bool vertical: m ? m.vertical : false
    implicitWidth: grid.implicitWidth
    implicitHeight: grid.implicitHeight
    width: implicitWidth
    height: implicitHeight

    Grid {
        id: grid
        // one line: spare cells along the flow cost nothing (spare rows ACROSS it would take spacing)
        rows: p.vertical ? 1000 : 1
        columns: p.vertical ? 1 : 1000
        flow: p.vertical ? Grid.TopToBottom : Grid.LeftToRight
        spacing: p.m && p.m.eff.spacing !== undefined ? p.m.eff.spacing : 8
        Repeater {
            id: trayRep
            model: SystemTray.items
            Item {
                id: trayItem
                required property SystemTrayItem modelData
                readonly property string tooltip: modelData.tooltipTitle || modelData.title || ""
                readonly property bool tooltipRich: false
                readonly property bool hovered: area.containsMouse
                readonly property var win: p.m ? p.m.win : null
                width: p.vertical ? (p.m ? p.m.innerCross : 22) : p.iconSize
                height: p.vertical ? p.iconSize + 4 : (p.m ? p.m.innerCross : 22)
                Rectangle {
                    anchors.centerIn: parent
                    width: 20; height: 20
                    radius: 6
                    visible: trayItem.modelData.status === Status.NeedsAttention
                    color: Theme.alpha(Theme.danger, 0.35)
                }
                IconImage {
                    anchors.centerIn: parent
                    implicitSize: p.iconSize
                    source: trayItem.modelData.icon
                }
                MouseArea {
                    id: area
                    anchors.fill: parent
                    enabled: !(p.m && p.m.win.preview)
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    onContainsMouseChanged: containsMouse ? p.m.host.showTip(trayItem) : p.m.host.hideTip(trayItem)
                    onClicked: e => {
                        const it = trayItem.modelData
                        if (e.button === Qt.MiddleButton) it.secondaryActivate()
                        else if (e.button === Qt.RightButton || it.onlyMenu) {
                            if (!(it.hasMenu || it.menu)) return
                            const w = p.m.win, c = trayItem.mapToItem(null, trayItem.width / 2, trayItem.height / 2)
                            p.m.host.openTrayMenu(it, Qt.point(c.x + w.screenX, c.y + w.screenY), w.edge, w.size + w.margin[0] + w.margin[1])
                        } else it.activate()
                    }
                    onWheel: e => trayItem.modelData.scroll(e.angleDelta.y / 120, false)
                }
            }
        }
    }
}
