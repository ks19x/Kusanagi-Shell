// SpPresets.qml — whole looks in one click, plus saving your own
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

        PresetPreview {
            x: 10; y: 10
            width: parent.width - 20
            height: 62
            preset: pc.preset
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
}
