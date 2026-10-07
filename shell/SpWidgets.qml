// SpWidgets.qml — Settings → Desktop widgets: turn them on, choose when they show, add / tune /
// remove them, and arrange them on screen (drag mode).
import Quickshell
import QtQuick

Column {
    id: page
    spacing: 22

    readonly property var items: { try { return JSON.parse(JSON.stringify(Config.widgets.items || [])) } catch (e) { return [] } }
    function edit(fn) { const l = JSON.parse(JSON.stringify(Config.widgets.items || [])); fn(l); Config.widgets.items = l }
    function set(i, key, v) { edit(l => { if (!l[i]) return; if (v === undefined) delete l[i][key]; else l[i][key] = v }) }

    readonly property var kinds: [
        { value: "clock", label: "Clock", note: "big, stacked, minimal or analog", icon: 0xf0150 },
        { value: "media", label: "Now playing", note: "cover, title, controls", icon: 0xf075a },
        { value: "stats", label: "System", note: "CPU, RAM, GPU rings", icon: 0xf029a },
        { value: "weather", label: "Weather", note: "now and the next days", icon: 0xf0599 },
        { value: "calendar", label: "Calendar", note: "this month", icon: 0xf00ed },
        { value: "greeting", label: "Greeting", note: "good evening, you", icon: 0xf1821 },
        { value: "text", label: "Text", note: "your own words", icon: 0xf0757 }
    ]
    function kind(t) { return kinds.find(k => k.value === t) ?? kinds[0] }

    SpGroup {
        title: "Desktop widgets"
        icon: 0xf056e
        hint: "Little things on your wallpaper, under your windows."
        CpRow { width: parent.width; label: "Show widgets"; CpSwitch { on: Config.widgets.enabled; onToggled: v => Config.widgets.enabled = v } }
        CpRow {
            width: parent.width; label: "Only on an empty workspace"
            hint: "they fade in when no window is open and are unloaded otherwise — no memory or CPU while you work"
            CpSwitch { on: Config.widgets.onlyDesktop; onToggled: v => Config.widgets.onlyDesktop = v }
        }
        CpRow { width: parent.width; label: "Fade in and out"; CpSwitch { on: Config.widgets.fade; onToggled: v => Config.widgets.fade = v } }
        Flow {
            width: parent.width
            spacing: 6
            CpChip { label: "Arrange on screen"; icon: 0xf01be; on: true; onClicked: Quickshell.execDetached(["kusanagi", "msg", "widgets", "arrange"]) }
            CpText { text: "drag them where you want, then Done (or Esc)"; font.pixelSize: 10; color: Theme.textDim; height: 28 }
        }
    }

    SpGroup {
        title: "Add a widget"
        SpStylePicker {
            kind: "widget"
            cardW: 128
            options: page.kinds
            current: ""
            onPicked: v => page.edit(l => l.push({ type: v, x: 0.5, y: 0.5, scale: 1, card: v !== "clock" && v !== "greeting" && v !== "text",
                                                   style: v === "clock" ? "big" : undefined }))
        }
    }

    SpGroup {
        title: "Your widgets"
        hint: page.items.length ? "Each one's look; drag them into place with Arrange on screen." : "None yet — add one above."
        Repeater {
            model: page.items.length
            Rectangle {
                id: rowCard
                required property int index
                readonly property var it: page.items[index] ?? ({})
                width: parent.width
                height: col.implicitHeight + 24
                radius: 12
                color: Theme.alpha(Theme.text, 0.04)
                Column {
                    id: col
                    x: 14; y: 12
                    width: parent.width - 28
                    spacing: 8
                    Item {
                        width: parent.width; height: 30
                        Row {
                            spacing: 10
                            anchors.verticalCenter: parent.verticalCenter
                            Rectangle {
                                width: 30; height: 30; radius: 9
                                color: Theme.alpha(Theme.accent, 0.14)
                                CpIcon { anchors.centerIn: parent; cp: page.kind(rowCard.it.type).icon; color: Theme.accent; font.pixelSize: 15 }
                            }
                            CpText { anchors.verticalCenter: parent.verticalCenter; text: page.kind(rowCard.it.type).label; font.pixelSize: 13; font.bold: true }
                        }
                        CpIconButton {
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            icon: 0xf0a7a; tip: "remove"
                            onClicked: page.edit(l => l.splice(rowCard.index, 1))
                        }
                    }
                    CpSegmented {
                        visible: rowCard.it.type === "clock"
                        width: Math.min(parent.width, 420)
                        current: rowCard.it.style || "big"
                        options: [{ label: "Big", value: "big" }, { label: "Stacked", value: "stacked" }, { label: "Minimal", value: "minimal" }, { label: "Analog", value: "analog" }]
                        onPicked: v => page.set(rowCard.index, "style", v)
                    }
                    CpRow {
                        visible: rowCard.it.type === "clock" && rowCard.it.style === "analog"
                        width: parent.width; label: "Second hand"; hint: "redraws once a second (a little CPU)"
                        CpSwitch { on: !!rowCard.it.seconds; onToggled: v => page.set(rowCard.index, "seconds", v) }
                    }
                    CpRow {
                        visible: rowCard.it.type === "stats"
                        width: parent.width; label: "Third ring"
                        CpSegmented {
                            width: 180; current: rowCard.it.third || "gpu"
                            options: [{ label: "GPU", value: "gpu" }, { label: "Temp", value: "temp" }]
                            onPicked: v => page.set(rowCard.index, "third", v)
                        }
                    }
                    CpRow {
                        visible: rowCard.it.type === "text"
                        width: parent.width; label: "Text"
                        CpField { width: 300; text: rowCard.it.text || ""; placeholder: "⏎ applies"; onAccepted: t => page.set(rowCard.index, "text", t) }
                    }
                    CpRow {
                        visible: rowCard.it.type === "greeting"
                        width: parent.width; label: "Call me"
                        CpField { width: 200; text: rowCard.it.name || ""; placeholder: Quickshell.env("USER"); onAccepted: t => page.set(rowCard.index, "name", t || undefined) }
                    }
                    CpRow {
                        width: parent.width; label: "On a card"; hint: "off: straight on the wallpaper, with a soft shadow"
                        CpSwitch { on: !!rowCard.it.card; onToggled: v => page.set(rowCard.index, "card", v) }
                    }
                    CpSlider {
                        width: parent.width; height: 30
                        icon: 0xf0349; label: "Size"
                        value: ((rowCard.it.scale ?? 1) - 0.4) / 2.1
                        valueText: Math.round((rowCard.it.scale ?? 1) * 100) + "%"
                        onMoved: v => page.set(rowCard.index, "scale", Math.round((0.4 + v * 2.1) * 20) / 20)
                    }
                }
            }
        }
    }
}
