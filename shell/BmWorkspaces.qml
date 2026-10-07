// BmWorkspaces.qml — workspaces / tags. Options: style (pills dots numbers roman kanji custom dwl), icons, glow,
// fontSize, colors { active, occupied, empty, urgent, onActive }. Defaults: Settings → Workspaces.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property var o: m ? m.eff : ({})
    readonly property var c: o.colors || ({})
    readonly property bool visual: true
    readonly property var vars: ({})
    readonly property string format: ""
    readonly property var actions: ({ scrollUp: "workspace:prev", scrollDown: "workspace:next" })
    implicitWidth: ws.width
    implicitHeight: ws.height
    width: implicitWidth
    height: implicitHeight
    WsIndicator {
        id: ws
        entries: Wm.workspaces
        vertical: p.m ? p.m.vertical : false
        slotHeight: p.m ? p.m.innerCross : 22
        style: p.o.style || Config.workspaces.style
        glow: p.o.glow ?? Config.workspaces.glow
        iconString: Array.isArray(p.o.icons) ? p.o.icons.join(" ") : (p.o.icons || Config.workspaces.icons)
        fontSize: p.o.fontSize ? BarSpec.fontSize(p.o.fontSize, p.m.win.spec.fontSize) : (style === "dwl" ? Config.bar.fontSize : 12)
        activeColor: p.c.active ? BarSpec.color(p.c.active) : Config.workspaces.activeColor === "accent2" ? Theme.accent2 : Config.workspaces.activeColor === "text" ? Theme.text : Theme.accent
        occupiedColor: BarSpec.color(p.c.occupied, "text/0.85")
        emptyColor: BarSpec.color(p.c.empty, "text/0.35")
        urgentColor: BarSpec.color(p.c.urgent, "danger")
        onActiveColor: BarSpec.color(p.c.onActive, "bg")
        onActivated: entry => { if (!p.m.win.preview) Wm.focusWorkspace(entry) }
    }
}
