// SpWorkspaces.qml — workspace / tag indicator style with a live preview
import QtQuick

Column {
    spacing: 22

    SpGroup {
        title: "Preview"
        Rectangle {
            width: parent.width
            height: 64
            radius: Math.max(6, Config.look.radius - 8)
            color: Theme.alpha(Theme.bgPanel, 0.6)
            Rectangle {
                anchors.centerIn: parent
                height: 22
                width: preview.implicitWidth + 16
                radius: Config.bar.radius
                color: Theme.alpha(Theme.text, 0.06)
                WsIndicator {
                    id: preview
                    x: 8
                    // 2 is active, 1 and 3 have windows
                    entries: Array.from({ length: Config.workspaces.shown }, (_, i) => ({
                        n: i + 1, active: i === 1, occupied: i === 0 || i === 2, urgent: false }))
                }
            }
        }
    }

    SpGroup {
        title: "Style"
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: [
                    { value: "pills", label: "Pills" }, { value: "dots", label: "Dots" },
                    { value: "numbers", label: "1 2 3" }, { value: "roman", label: "I II III" },
                    { value: "kanji", label: "一 二 三" }, { value: "dwl", label: "dwl blocks" },
                    { value: "custom", label: "Custom icons" }
                ]
                CpChip {
                    required property var modelData
                    label: modelData.label
                    on: Config.workspaces.style === modelData.value
                    onClicked: Config.workspaces.style = modelData.value
                }
            }
        }
        CpRow {
            width: parent.width
            visible: Config.workspaces.style === "custom"
            label: "Icons"
            hint: "one per workspace, separated by spaces — any text or Nerd Font glyph"
            CpField {
                width: 300
                text: Config.workspaces.icons
                placeholder: "♠ ♣ ♥ ♦ ★"
                onEdited: t => Config.workspaces.icons = t
            }
        }
    }

    SpGroup {
        title: "Behaviour"
        CpRow {
            width: parent.width; label: "Always shown"; hint: "more appear while they have windows"
            CpStepper { value: Config.workspaces.shown; from: 1; to: 9; onChanged: v => Config.workspaces.shown = v }
        }
        CpRow { width: parent.width; label: "Glow on the active one"; CpSwitch { on: Config.workspaces.glow; onToggled: v => Config.workspaces.glow = v } }
        CpRow {
            width: parent.width; label: "Active colour"
            CpSegmented {
                width: 300; current: Config.workspaces.activeColor
                options: [{ label: "Accent", value: "accent" }, { label: "Second accent", value: "accent2" }, { label: "Text", value: "text" }]
                onPicked: v => Config.workspaces.activeColor = v
            }
        }
    }

    CpChip { label: "Reset workspaces"; icon: 0xf0709; onClicked: Config.reset("workspaces") }
}
