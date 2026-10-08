// BmUpdates.qml — waiting package updates (hidden at 0; always: true shows it anyway).
// Vars: count. Status: none some many (50+) unknown. Click: upgrade in a terminal; right click: check now.
import QtQuick

Item {
    id: p
    property var m
    readonly property var vars: ({ count: Updates.known ? Updates.count : "?" })
    readonly property string status: !Updates.known ? "unknown" : Updates.count === 0 ? "none" : Updates.count >= 50 ? "many" : "some"
    readonly property real level: Math.min(100, Updates.count)
    readonly property string format: String.fromCodePoint(0xf06b0) + " {count}"
    readonly property bool shown: Updates.count > 0 || (m && m.eff.always === true)
    readonly property var actions: ({ click: "updates:upgrade", rightClick: "updates:check" })
    readonly property string tooltip: {
        if (Updates.checking) return "Checking for updates…"
        if (!Updates.known) return "Couldn't check for updates (see: kusanagi updates)"
        if (!Updates.count) return "Up to date"
        const names = Updates.list.slice(0, 12).map(u => u.name + (u.source && u.source !== "" ? "  ·  " + u.source : ""))
        return Updates.count + (Updates.count === 1 ? " update" : " updates") + " — click to install\n" + names.join("\n") + (Updates.count > 12 ? "\n…" : "")
    }
}
