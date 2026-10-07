// SpAppPicker.qml — search your installed apps and pick one (Settings → Bar → Dock & taskbar).
// `picked(id)` gives the desktop entry id; apps listed in `exclude` are left out.
import Quickshell
import Quickshell.Widgets
import QtQuick

Column {
    id: root
    property var exclude: []
    property int max: 8
    signal picked(string id)
    spacing: 6

    CpField {
        id: q
        width: parent.width
        icon: 0xf0349
        placeholder: "Search apps to add…"
        onAccepted: if (root.results.length) root.picked(root.results[0].id)
    }
    readonly property var results: {
        const t = q.text.trim().toLowerCase()
        if (!t) return []
        return DesktopEntries.applications.values
            .filter(e => !e.noDisplay && !root.exclude.includes(e.id)
                && (e.name.toLowerCase().includes(t) || (e.genericName || "").toLowerCase().includes(t) || e.id.toLowerCase().includes(t)))
            .sort((a, b) => a.name.toLowerCase().indexOf(t) - b.name.toLowerCase().indexOf(t))
            .slice(0, root.max)
    }
    Repeater {
        model: root.results
        Rectangle {
            id: row
            required property var modelData
            width: root.width
            height: 38
            radius: 10
            color: hov.hovered ? Theme.alpha(Theme.accent, 0.14) : Theme.alpha(Theme.text, 0.04)
            Behavior on color { ColorAnimation { duration: Config.ms(120) } }
            IconImage {
                x: 10; anchors.verticalCenter: parent.verticalCenter
                implicitSize: 22
                source: Dock.iconOfEntry(row.modelData)
            }
            Column {
                x: 42; anchors.verticalCenter: parent.verticalCenter
                CpText { text: row.modelData.name; font.pixelSize: 12 }
                CpText { visible: text !== ""; text: row.modelData.genericName || ""; font.pixelSize: 10; color: Theme.textDim }
            }
            CpIcon {
                anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                cp: 0xf0415; color: Theme.accent; font.pixelSize: 16
            }
            HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: { root.picked(row.modelData.id); q.text = "" } }
        }
    }
}
