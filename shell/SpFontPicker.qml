// SpFontPicker.qml — every installed font, each shown in itself; search to narrow it down.
// `current` is a family name; `picked(family)`. Only the rows on screen are drawn (ListView).
import QtQuick

Column {
    id: root
    property string current: ""
    property string sample: "Kusanagi  0123  The quick brown fox"
    signal picked(string family)
    spacing: 8
    width: parent ? parent.width : 600

    // Qt lists every style as its own family sometimes ("Foo Light"): keep one per name, Nerd Fonts first
    readonly property var all: {
        const seen = {}, out = []
        for (const f of Qt.fontFamilies()) {
            if (seen[f] || f.startsWith(".")) continue
            seen[f] = true; out.push(f)
        }
        return out.sort((a, b) => (b.includes("Nerd") - a.includes("Nerd")) || a.localeCompare(b))
    }
    readonly property var shown: {
        const q = search.text.trim().toLowerCase()
        return q ? all.filter(f => f.toLowerCase().includes(q)) : all
    }

    Row {
        width: parent.width
        spacing: 10
        CpField {
            id: search
            width: parent.width - count.width - 10
            icon: 0xf0349
            placeholder: "Search " + root.all.length + " fonts…"
        }
        CpText { id: count; text: root.shown.length + " shown"; font.pixelSize: 10; color: Theme.textDim; height: search.height }
    }

    Rectangle {
        width: parent.width
        height: 300
        radius: 12
        color: Theme.alpha(Theme.text, 0.03)
        clip: true
        ListView {
            id: list
            anchors { fill: parent; margins: 6 }
            model: root.shown
            spacing: 2
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            delegate: Rectangle {
                id: row
                required property string modelData
                readonly property bool on: modelData === root.current
                width: list.width
                height: 46
                radius: 9
                color: on ? Theme.alpha(Theme.accent, 0.16) : hov.hovered ? Theme.alpha(Theme.text, 0.06) : "transparent"
                border.width: on ? 1 : 0
                border.color: Theme.alpha(Theme.accent, 0.5)
                Column {
                    x: 12; anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 60
                    spacing: 1
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: root.sample
                        font.family: row.modelData
                        font.pixelSize: 15
                        color: Theme.text
                    }
                    CpText { text: row.modelData; font.pixelSize: 10; color: Theme.textDim }
                }
                CpIcon {
                    visible: row.on
                    anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                    cp: 0xf012c; color: Theme.accent
                }
                HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.picked(row.modelData) }
            }
        }
        Rectangle {
            visible: list.contentHeight > list.height
            x: parent.width - 5; width: 3; radius: 1.5
            y: 6 + list.visibleArea.yPosition * (parent.height - 12)
            height: list.visibleArea.heightRatio * (parent.height - 12)
            color: Theme.alpha(Theme.text, 0.2)
        }
    }
}
