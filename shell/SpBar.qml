// SpBar.qml — Settings → Bar: classic options, or a custom layout (Config.bars) built from templates
// and edited piece by piece in SpBarEditor. Everything applies live.
import Quickshell
import Quickshell.Widgets
import QtQuick

Column {
    id: page
    spacing: 22

    readonly property bool custom: !!Config.bars && Config.bars.length > 0
    // the custom layout you left for the classic one (this session), so switching back loses nothing
    property var stash: null
    // Layout · Templates · Dock & taskbar
    property string tab: "layout"

    function goCustom() {
        if (custom) return
        // start from what is on screen now: the classic bar, written out as a layout
        Config.bars = stash ?? [JSON.parse(JSON.stringify(BarSpec.legacy()))]
    }
    function goClassic() {
        if (!custom) return
        stash = JSON.parse(JSON.stringify(Config.bars))
        Config.bars = []
    }

    CpSegmented {
        width: parent.width
        current: page.tab
        options: [{ label: "Layout", value: "layout" }, { label: "Templates", value: "templates" }, { label: "Dock & taskbar", value: "dock" }]
        onPicked: v => page.tab = v
    }

    // one tab at a time (only the shown one exists)
    Loader {
        width: parent.width
        active: page.tab === "layout"
        visible: active
        sourceComponent: Column {
            spacing: 22
            SpGroup {
                title: "Mode"
                hint: page.custom ? "Your own layout: every bar, group and module is editable below (or in settings.json → \"bars\")."
                                  : "The classic Kusanagi bar, set up with the options below. Pick a template or go custom to build any bar."
                CpSegmented {
                    width: parent.width
                    current: page.custom ? "custom" : "classic"
                    options: [{ label: "Classic options", value: "classic" }, { label: "Custom layout", value: "custom" }]
                    onPicked: v => v === "custom" ? page.goCustom() : page.goClassic()
                }
            }

            Loader {
                width: parent.width
                source: page.custom ? "SpBarEditor.qml" : "SpBarClassic.qml"
            }
        }
    }
    Loader {
        width: parent.width
        active: page.tab === "templates"
        visible: active
        sourceComponent: Column {
            spacing: 22
            SpGroup {
                title: "Templates"
                hint: "Start from one of these; then change anything. Also: kusanagi msg bar template <name>."
                Grid {
                    id: tpl
                    width: parent.width
                    columns: 1
                    spacing: 8
                    readonly property real cell: width
                    Repeater {
                        model: BarTemplates.list
                        Rectangle {
                            id: card
                            required property var modelData
                            width: tpl.cell
                            height: card.vert ? 120 : 82
                            readonly property var b0: modelData.bars[0]
                            readonly property bool vert: b0.position === "left" || b0.position === "right"
                            radius: 12
                            color: hov.hovered ? Theme.alpha(Theme.text, 0.08) : Theme.alpha(Theme.text, 0.04)
                            border.width: 1
                            border.color: Theme.alpha(Theme.text, 0.06)
                            // every bar of the template, stacked (dock = top strip + dock)
                            Column {
                                x: 8; y: 8
                                width: parent.width - 16
                                spacing: 2
                                Repeater {
                                    model: card.modelData.bars
                                    BarPreview {
                                        required property var modelData
                                        width: parent.width
                                        height: card.vert ? 84 : (card.height - 36) / card.modelData.bars.length - 2
                                        bar: modelData
                                    }
                                }
                            }
                            Row {
                                x: 12; y: parent.height - 24
                                spacing: 10
                                CpText { text: card.modelData.name; font.pixelSize: 12; font.bold: true }
                                CpText { text: card.modelData.note; font.pixelSize: 10; color: Theme.textDim; anchors.verticalCenter: parent.verticalCenter }
                            }
                            HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: { page.stash = null; Config.bars = BarTemplates.bars(card.modelData.id); page.tab = "layout" } }
                        }
                    }
                }
            }

        }
    }
    Loader {
        width: parent.width
        active: page.tab === "dock"
        visible: active
        sourceComponent: Column {
            spacing: 22
            SpGroup {
                title: "Dock & taskbar"
                hint: BarSpec.uses.taskbar ? "Apps pinned here stay in the dock even when closed. Right-click any dock icon to pin or unpin it."
                    : "Pinned apps for the Taskbar module — the Dock and Taskbar templates use it. Right-click a dock icon to pin it."

                // pinned apps, in dock order
                Flow {
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: Dock.pinned
                        Rectangle {
                            id: pinTile
                            required property string modelData
                            required property int index
                            width: 92; height: 92
                            radius: 14
                            color: tileHov.hovered ? Theme.alpha(Theme.text, 0.09) : Theme.alpha(Theme.text, 0.05)
                            Behavior on color { ColorAnimation { duration: Config.ms(120) } }
                            HoverHandler { id: tileHov }
                            IconImage {
                                id: pinIcon
                                visible: source != ""
                                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 12 }
                                implicitSize: 36
                                source: Dock.icon(pinTile.modelData)
                            }
                            Rectangle {
                                visible: !pinIcon.visible
                                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 12 }
                                width: 36; height: 36; radius: 9
                                color: Theme.alpha(Theme.accent, 0.3)
                                CpText { anchors.centerIn: parent; text: Dock.name(pinTile.modelData).charAt(0).toUpperCase(); font.pixelSize: 18; font.bold: true }
                            }
                            CpText {
                                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 54 }
                                width: parent.width - 10
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                text: Dock.name(pinTile.modelData)
                                font.pixelSize: 10
                            }
                            // move / remove, shown on hover
                            Row {
                                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 2 }
                                opacity: tileHov.hovered ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: Config.ms(120) } }
                                CpIconButton { width: 22; height: 22; iconSize: 13; icon: 0xf004d; visible: pinTile.index > 0; onClicked: Dock.move(pinTile.modelData, -1) }
                                CpIconButton { width: 22; height: 22; iconSize: 13; icon: 0xf0156; onClicked: Dock.unpin(pinTile.modelData) }
                                CpIconButton { width: 22; height: 22; iconSize: 13; icon: 0xf0054; visible: pinTile.index < Dock.pinned.length - 1; onClicked: Dock.move(pinTile.modelData, 1) }
                            }
                        }
                    }
                    CpText {
                        visible: Dock.pinned.length === 0
                        text: "Nothing pinned yet — search below, or pin your most used apps in one go."
                        font.pixelSize: 11; color: Theme.textDim
                        height: 34; verticalAlignment: Text.AlignVCenter
                    }
                }
                SpAppPicker {
                    width: parent.width
                    exclude: Dock.pinned
                    onPicked: id => Dock.pin(id)
                }
                Flow {
                    width: parent.width
                    spacing: 6
                    CpChip {
                        label: "Pin my 6 most used apps"; icon: 0xf0403
                        onClicked: { for (const id of Dock.topUsed(6)) Dock.pin(id) }
                    }
                    CpChip { visible: Dock.pinned.length > 0; label: "Unpin all"; icon: 0xf0a7a; onClicked: Config.dock.pinned = [] }
                }
                CpRow {
                    width: parent.width; label: "Running apps"; hint: "how an open app is marked"
                    CpSegmented {
                        width: 240; current: Config.dock.indicator
                        options: [{ label: "Dots", value: "dot" }, { label: "Line", value: "line" }, { label: "None", value: "none" }]
                        onPicked: v => Config.dock.indicator = v
                    }
                }
                CpRow {
                    width: parent.width; label: "One icon per app"; hint: "off: one per window, like a classic taskbar"
                    CpSwitch { on: Config.dock.grouped; onToggled: v => Config.dock.grouped = v }
                }
                CpSlider {
                    width: parent.width; height: 30
                    icon: 0xf0349; label: "Grow on hover"
                    value: (Config.dock.magnify - 1) / 0.8
                    valueText: Config.dock.magnify <= 1.001 ? "off" : "×" + Config.dock.magnify.toFixed(2)
                    onMoved: v => Config.dock.magnify = Math.round((1 + v * 0.8) * 20) / 20
                }
            }

        }
    }
}
