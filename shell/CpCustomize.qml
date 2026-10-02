// CpCustomize.qml — the control panel's Quick tab: the settings you'd change often, plus a door into
// the full settings app. Everything writes Config and applies live.
import QtQuick

Column {
    id: root
    required property var panel
    spacing: 12

    CpSection { text: "PRESET" }
    Flow {
        width: parent.width
        spacing: 6
        Repeater {
            model: Presets.builtin.concat(Presets.saved)
            CpChip {
                required property var modelData
                label: modelData.name
                on: Presets.lastApplied === (modelData.id || modelData.name)
                onClicked: Presets.apply(modelData)
            }
        }
    }

    CpSection { text: "ACCENT" }
    Flow {
        width: parent.width
        spacing: 8
        CpChip { label: "Wallpaper"; icon: 0xf0e09; on: Config.look.accent === ""; onClicked: Config.look.accent = "" }
        Repeater {
            model: ["#f5a3b5", "#f5b38a", "#e9d27c", "#8fd6b0", "#8cc8f0", "#b9a6f2", "#e8505b", "#e8e8e8"]
            Rectangle {
                required property string modelData
                width: 28; height: 28; radius: 14
                color: modelData
                border.width: Config.look.accent.toLowerCase() === modelData ? 3 : 0
                border.color: Theme.text
                scale: sw.containsMouse ? 1.12 : 1
                Behavior on scale { NumberAnimation { duration: Config.ms(160); easing.type: Easing.OutBack } }
                MouseArea { id: sw; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: Config.look.accent = parent.modelData }
            }
        }
    }

    CpSection { text: "BAR" }
    CpSegmented {
        width: parent.width
        current: Config.bar.style
        options: [{ label: "Islands", value: "islands" }, { label: "Solid", value: "solid" },
                  { label: "Floating", value: "floating" }, { label: "Clear", value: "clear" }]
        onPicked: v => Config.bar.style = v
    }

    CpSection { text: "WORKSPACES" }
    Flow {
        width: parent.width
        spacing: 6
        Repeater {
            model: [{ value: "pills", label: "Pills" }, { value: "dots", label: "Dots" }, { value: "numbers", label: "1 2 3" },
                    { value: "roman", label: "I II III" }, { value: "kanji", label: "一 二 三" }, { value: "dwl", label: "dwl" }, { value: "custom", label: "Custom" }]
            CpChip {
                required property var modelData
                label: modelData.label
                on: Config.workspaces.style === modelData.value
                onClicked: Config.workspaces.style = modelData.value
            }
        }
    }

    CpSection { text: "MOTION" }
    CpSegmented {
        width: parent.width
        current: Config.look.animSpeed
        options: [{ label: "Off", value: 0 }, { label: "Snappy", value: 0.7 }, { label: "Smooth", value: 1.0 }, { label: "Relaxed", value: 1.4 }]
        onPicked: v => Config.look.animSpeed = v
    }

    Item { width: 1; height: 4 }
    Rectangle {
        width: parent.width
        height: 44
        radius: height / 2
        color: allArea.containsMouse ? Theme.accent : Theme.alpha(Theme.accent, 0.18)
        Behavior on color { ColorAnimation { duration: Config.ms(160) } }
        Row {
            anchors.centerIn: parent
            spacing: 10
            CpIcon { cp: 0xf0493; font.pixelSize: 16; color: allArea.containsMouse ? Theme.bgPanel : Theme.accent }
            CpText { text: "All settings"; font.pixelSize: 13; font.bold: true; color: allArea.containsMouse ? Theme.bgPanel : Theme.text }
        }
        MouseArea {
            id: allArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: { root.panel.close(); root.panel.shellRef.openSettings() }
        }
    }
}
