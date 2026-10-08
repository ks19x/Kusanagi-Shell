// SpPresets.qml — whole looks in one click, saving your own, and sharing them as files or text
import Quickshell
import Quickshell.Io
import QtQuick

Column {
    id: page
    spacing: 22

    component PresetCard: Rectangle {
        id: pc
        required property var preset
        property bool own: false
        readonly property bool on: Presets.lastApplied === (preset.id || preset.name)
        width: parent ? Math.floor((parent.width - 12) / 2) : 300
        height: 132
        radius: Math.max(8, Config.look.radius - 4)
        color: Theme.alpha(Theme.text, area.containsMouse ? 0.07 : 0.045)
        border.width: on ? 2 : 1
        border.color: on ? Theme.accent : Theme.alpha(Theme.text, 0.08)
        scale: area.pressed ? 0.98 : 1
        Behavior on color { ColorAnimation { duration: Config.ms(140) } }
        Behavior on scale { NumberAnimation { duration: Config.ms(160); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(2) } }

        // a layout preset shows its real first bar; classic ones the drawing of their options
        readonly property var layoutBars: Presets.barsOf(pc.preset)
        PresetPreview {
            visible: pc.layoutBars.length === 0
            x: 10; y: 10
            width: parent.width - 20
            height: 62
            preset: pc.preset
        }
        Loader {
            active: pc.layoutBars.length > 0
            x: 10; y: 10
            width: parent.width - 20
            height: 62
            sourceComponent: BarPreview {
                snapshot: true
                // same scale as the drawn previews (a narrower virtual screen); side bars show their top
                screenW: Math.round(width / 0.6)
                fixedScale: vertical ? 0.6 : 0
                // a dock template shows its dock
                bar: pc.layoutBars[pc.layoutBars.length > 1 && pc.layoutBars[1].length === "auto" ? 1 : 0]
            }
        }
        Column {
            x: 12; y: 80
            width: parent.width - 24 - (pc.own ? 30 : 0)
            spacing: 2
            CpText { text: pc.preset.name; font.pixelSize: 13; font.bold: true }
            CpText { width: parent.width; text: pc.preset.note || ""; font.pixelSize: 10; color: Theme.textDim; elide: Text.ElideRight }
        }
        MouseArea { id: area; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Presets.apply(pc.preset) }
        CpIconButton {
            visible: pc.own
            anchors { right: parent.right; bottom: parent.bottom; margins: 8 }
            width: 26; height: 26; icon: 0xf0a7a; iconSize: 13
            onClicked: Presets.remove(pc.preset.name)
        }
    }

    SpGroup {
        title: "Presets"
        hint: "Tap one to apply it. They change the look — bar, workspaces, motion, surfaces, popups — and leave your wallpaper, lock and the rest alone. Fine-tune anything afterwards."
        Flow {
            width: parent.width
            spacing: 12
            Repeater {
                model: Presets.builtin
                PresetCard { required property var modelData; preset: modelData }
            }
        }
    }

    SpGroup {
        title: "Your looks"
        hint: Presets.saved.length ? "" : "Save how things look right now, and come back to it any time."
        Flow {
            width: parent.width
            spacing: 12
            visible: Presets.saved.length > 0
            Repeater {
                model: Presets.saved
                PresetCard { required property var modelData; preset: modelData; own: true }
            }
        }
        Row {
            spacing: 8
            CpField { id: nameField; width: 220; placeholder: "Name this look"; onAccepted: t => { Presets.save(t); text = "" } }
            CpChip { label: "Save current look"; icon: 0xf0415; onClicked: { Presets.save(nameField.text); nameField.text = "" } }
        }
    }

    // ---------------- share ----------------
    property var pending: null                 // { look, commands } read but not applied yet
    property string shareError: ""
    property var found: []                     // .kusanagi files in ~/kusanagi-looks and ~/Downloads
    function scan() { finder.running = true }
    Component.onCompleted: scan()
    Process {
        id: finder
        command: ["sh", "-c", "ls -1t \"$HOME\"/kusanagi-looks/*.kusanagi \"$HOME\"/Downloads/*.kusanagi 2>/dev/null; true"]
        stdout: StdioCollector { onStreamFinished: page.found = text.split("\n").filter(l => l.trim()) }
    }
    function open(r) { if (r.error) { shareError = r.error; pending = null } else { shareError = ""; pending = r } }

    SpGroup {
        title: "Share a look"
        icon: 0xf0497
        hint: "A look is one small file: bars, styles, colours, motion, font, panel, lock and power menu — not your wallpaper or pinned apps."

        CpText { text: "Export what you have now"; font.pixelSize: 11; font.bold: true; color: Theme.textDim }
        Row {
            spacing: 8
            CpField { id: exportName; width: 220; placeholder: "Name it" }
            CpChip { label: "Save file"; icon: 0xf0193; onClicked: { Presets.exportLook(exportName.text, false); scanLater.restart() } }
            CpChip { label: "Copy as text"; icon: 0xf018f; onClicked: Presets.exportLook(exportName.text, true) }
        }
        Row {
            visible: Presets.lastExport !== ""
            spacing: 8
            CpText {
                anchors.verticalCenter: parent.verticalCenter
                text: Presets.lastExport === "clipboard" ? "Copied — paste it anywhere (Discord, a gist…)." : "Saved " + Presets.lastExport.replace(Quickshell.env("HOME"), "~")
                font.pixelSize: 11; color: Theme.ok
            }
            CpChip { visible: Presets.lastExport !== "clipboard"; label: "Open folder"; icon: 0xf024b; onClicked: Quickshell.execDetached(["xdg-open", Presets.looksDir]) }
        }
        Timer { id: scanLater; interval: 600; onTriggered: page.scan() }

        CpText { text: "Import one"; font.pixelSize: 11; font.bold: true; color: Theme.textDim; topPadding: 8 }
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: page.found
                CpChip {
                    required property string modelData
                    label: modelData.split("/").pop().replace(/\.kusanagi$/, "")
                    icon: 0xf0214
                    onClicked: page.open(Presets.readLookFile(modelData))
                }
            }
            CpChip { label: "Look again"; icon: 0xf0450; onClicked: page.scan() }
        }
        CpField {
            width: parent.width
            mono: true
            placeholder: "…or paste a look's text here and press Enter"
            onAccepted: t => { page.open(Presets.readLook(t)); text = "" }
        }
        CpText { visible: page.shareError !== ""; text: page.shareError; color: Theme.danger; font.pixelSize: 11 }

        // what's about to come in, before anything changes
        Rectangle {
            visible: page.pending !== null
            width: parent.width
            height: pend.implicitHeight + 28
            radius: 12
            color: Theme.alpha(Theme.accent, 0.08)
            border.width: 1
            border.color: Theme.alpha(Theme.accent, 0.4)
            Column {
                id: pend
                x: 14; y: 14
                width: parent.width - 28
                spacing: 10
                CpText { text: page.pending ? page.pending.look.name : ""; font.pixelSize: 14; font.bold: true }
                CpText { text: page.pending ? page.pending.look.note || "" : ""; font.pixelSize: 11; color: Theme.textDim; width: parent.width; wrapMode: Text.WordWrap }
                Loader {
                    active: !!page.pending && (page.pending.look.bars || []).length > 0
                    width: parent.width; height: active ? 56 : 0
                    sourceComponent: BarPreview { bar: page.pending.look.bars[0] }
                }
                Column {
                    visible: !!page.pending && page.pending.commands.length > 0
                    width: parent.width
                    spacing: 4
                    CpText {
                        width: parent.width; wrapMode: Text.WordWrap
                        text: "⚠ This look runs commands on your machine (bar modules / clicks). Only apply it if you trust where it came from:"
                        font.pixelSize: 11; font.bold: true; color: Theme.danger
                    }
                    Repeater {
                        model: page.pending ? page.pending.commands : []
                        CpText { required property string modelData; width: parent.width; elide: Text.ElideRight; text: "  $ " + modelData; font.pixelSize: 11; font.family: "JetBrainsMono Nerd Font" }
                    }
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    CpChip {
                        label: page.pending && page.pending.commands.length ? "Apply with its commands" : "Apply"
                        icon: 0xf012c; on: true
                        onClicked: { Presets.keep(page.pending.look); Presets.apply(page.pending.look); page.pending = null }
                    }
                    CpChip {
                        visible: !!page.pending && page.pending.commands.length > 0
                        label: "Apply without its commands"; icon: 0xf0a7a
                        onClicked: { const l = Presets.withoutCommands(page.pending.look); Presets.keep(l); Presets.apply(l); page.pending = null }
                    }
                    CpChip { label: page.pending && page.pending.commands.length ? "Just save it (commands included)" : "Just save it to my looks"; icon: 0xf0415; onClicked: { Presets.keep(page.pending.look); page.pending = null } }
                    CpChip { label: "Cancel"; icon: 0xf0156; onClicked: page.pending = null }
                }
            }
        }
    }
}
