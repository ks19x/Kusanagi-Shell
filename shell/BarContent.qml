// BarContent.qml — what a bar draws: its background and the start · center · end sections.
// Used by BarWindow (on screen) and BarPreview (Settings), so a preview is the real thing.
// `win` is the window (or a stand-in): vertical, edge, spec, size, margin, screenX/Y, host.
import QtQuick

Item {
    id: content

    required property var win

    BarBox {
        anchors.fill: parent
        vertical: content.win.vertical
        fill: Array.isArray(content.win.spec.bg) ? content.win.spec.bg.map(c => BarSpec.color(c)) : BarSpec.color(content.win.spec.bg)
        stroke: BarSpec.color(content.win.spec.border)
        strokeWidth: content.win.spec.borderWidth
        radius: content.win.spec.radius
        line: content.win.spec.line
    }

    readonly property real pad: win.spec.padding
    // what the sections need end to end (length: "auto" bars size themselves to it)
    property real natural: 0
    function recount() {
        let n = 0, k = 0
        for (let i = 0; i < secs.count; i++) { const s = secs.itemAt(i); if (s && s.len > 0) { n += s.len; k++ } }
        natural = n + 2 * pad + Math.max(0, k - 1) * Math.max(win.spec.spacing, 8)
    }
    onPadChanged: recount()
    Repeater {
        id: secs
        model: ["start", "center", "end"]
        Grid {
            id: sec
            required property string modelData
            // exactly one line: n × 1 or 1 × n (spare rows would still take spacing)
            rows: content.win.vertical ? Math.max(1, content.win.spec[sec.modelData].length) : 1
            columns: content.win.vertical ? 1 : Math.max(1, content.win.spec[sec.modelData].length)
            flow: content.win.vertical ? Grid.TopToBottom : Grid.LeftToRight
            spacing: content.win.spec.spacing
            readonly property real room: content.win.vertical ? content.height : content.width
            readonly property real len: content.win.vertical ? implicitHeight : implicitWidth
            onLenChanged: content.recount()
            readonly property real at: modelData === "start" ? content.pad : modelData === "center" ? Math.round((room - len) / 2) : room - content.pad - len
            x: content.win.vertical ? 0 : at
            y: content.win.vertical ? at : 0
            Repeater {
                model: content.win.spec[sec.modelData].length
                BarGroup {
                    required property int index
                    spec: content.win.spec[sec.modelData][index] ?? { modules: [], gap: [0, 0], inset: [0, 0], padding: [0, 0] }
                    host: content.win.host
                    win: content.win
                    cross: content.win.size
                    section: sec.modelData
                    gi: index
                }
            }
        }
    }
}
