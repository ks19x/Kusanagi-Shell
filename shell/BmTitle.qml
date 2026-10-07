// BmTitle.qml — focused window. Options: maxLength (chars). Vars: title, app.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import Quickshell.Wayland
import QtQuick

Item {
    id: p
    property var m
    readonly property int max: m && m.eff.maxLength ? m.eff.maxLength : Config.bar.titleWidth
    // overlays (launcher, panel) take focus without being windows — keep the last real one
    property var win: ToplevelManager.activeToplevel
    Connections {
        target: ToplevelManager
        function onActiveToplevelChanged() { if (ToplevelManager.activeToplevel) p.win = ToplevelManager.activeToplevel }
    }
    readonly property string full: win ? win.title : ""
    readonly property var vars: ({ title: full.length > max ? full.slice(0, max - 1) + "…" : full, app: win ? win.appId : "" })
    readonly property string format: "{title}"
    readonly property string tooltip: full.length > max ? full : ""
    readonly property bool shown: full !== ""
}
