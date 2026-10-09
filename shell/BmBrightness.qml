// BmBrightness.qml — screen brightness (laptop panel / DDC/CI monitors; hidden when nothing can be dimmed).
// Options: step. Vars: percent, icon. Level = percent. Scroll changes every screen.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property int percent: Math.round(Brightness.level * 100)
    readonly property var vars: ({ percent: percent })
    readonly property real level: percent
    readonly property var icons: [0xf00dd, 0xf00de, 0xf00df].map(c => String.fromCodePoint(c))
    readonly property string format: "{icon}  {percent}%"
    readonly property bool shown: Brightness.available
    readonly property var actions: ({ click: "panel:home", middleClick: "settings:display", scrollUp: "brightness:up", scrollDown: "brightness:down" })
    readonly property string tooltip: Brightness.displays.map(d => d.name + ": " + Math.round(Brightness.value(d.key) * 100) + "%").join("\n")
}
