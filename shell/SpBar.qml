// SpBar.qml — Settings → Bar: classic options, or a custom layout (Config.bars) built from templates
// and edited piece by piece in SpBarEditor. Everything applies live.
import QtQuick

Column {
    id: page
    spacing: 22

    readonly property bool custom: !!Config.bars && Config.bars.length > 0
    // the custom layout you left for the classic one (this session), so switching back loses nothing
    property var stash: null
    property bool showTemplates: !custom

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

    SpGroup {
        title: "Layout"
        hint: page.custom ? "Your own layout: every bar, group and module is editable below (or in settings.json → \"bars\")."
                          : "The classic Kusanagi bar, set up with the options below. Pick a template or go custom to build any bar."
        CpSegmented {
            width: parent.width
            current: page.custom ? "custom" : "classic"
            options: [{ label: "Classic options", value: "classic" }, { label: "Custom layout", value: "custom" }]
            onPicked: v => v === "custom" ? page.goCustom() : page.goClassic()
        }
    }

    SpGroup {
        title: "Templates"
        hint: "Start from one of these; then change anything. Also: kusanagi msg bar template <name>."
        CpChip {
            label: page.showTemplates ? "Hide templates" : "Show " + BarTemplates.list.length + " templates"
            icon: page.showTemplates ? 0xf0143 : 0xf0140
            onClicked: page.showTemplates = !page.showTemplates
        }
        Grid {
            id: tpl
            visible: page.showTemplates
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
                    TapHandler { onTapped: { page.stash = null; Config.bars = BarTemplates.bars(card.modelData.id); page.showTemplates = false } }
                }
            }
        }
    }

    Loader {
        width: parent.width
        source: page.custom ? "SpBarEditor.qml" : "SpBarClassic.qml"
    }
}
