// WsIndicator.qml — workspace/tag indicator in the style picked in Config.workspaces:
//   pills   hollow ring = empty, solid dot = has windows, wide accent pill with its number = active
//   dots    same, but the active one stays a (glowing) dot
//   numbers / roman / kanji / custom   glyphs; active = accent + underline, empty = dim
//   dwl     flat numbered blocks: active = filled with the accent, a small square marks tags with windows
// Used by the bar and by the Customize tab's live preview.
// entries: [{ n, active, occupied, urgent }]
import QtQuick
import QtQuick.Effects

Row {
    id: root

    property var entries: []
    property string style: Config.workspaces.style
    property bool glow: Config.workspaces.glow
    property int slotHeight: 22
    readonly property color activeColor: Config.workspaces.activeColor === "accent2" ? Theme.accent2 : Config.workspaces.activeColor === "text" ? Theme.text : Theme.accent
    signal activated(var entry)

    readonly property bool dwl: style === "dwl"
    readonly property bool textStyle: ["numbers", "roman", "kanji", "custom", "dwl"].includes(style)
    readonly property var roman: ["I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"]
    readonly property var kanji: ["一", "二", "三", "四", "五", "六", "七", "八", "九", "十"]
    readonly property var customIcons: Config.workspaces.icons.trim().split(/\s+/).filter(s => s)

    function glyph(n) {
        if (style === "roman") return roman[n - 1] ?? String(n)
        if (style === "kanji") return kanji[n - 1] ?? String(n)
        if (style === "custom") return customIcons[n - 1] ?? String(n)
        return String(n)
    }

    Repeater {
        model: root.entries

        Item {
            id: slot
            required property var modelData
            readonly property bool active: modelData.active
            readonly property bool empty: !modelData.occupied
            readonly property bool urgent: !!modelData.urgent
            width: root.dwl ? Math.max(slotHeight, label.implicitWidth + 14) : (root.textStyle ? label.implicitWidth + 4 : dot.width) + 8
            height: root.slotHeight
            Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }

            // ---- dot styles (pills / dots): margin 6px 4px around an 8px dot / 26px pill ----
            RectangularShadow {
                visible: !root.textStyle
                anchors.fill: dot
                radius: dot.radius
                blur: slot.urgent ? 8 : 10
                color: slot.urgent ? Theme.alpha(Theme.danger, 0.5) : Theme.alpha(root.activeColor, 0.55)
                opacity: (slot.active && root.glow) || slot.urgent ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
            }

            Rectangle {
                id: dot
                visible: !root.textStyle
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                readonly property bool pill: slot.active && root.style === "pills"
                width: pill ? 26 : 10
                height: pill ? 14 : 10
                radius: 6
                // waybar precedence: urgent > hover > active > empty > has windows
                color: slot.urgent ? Theme.danger
                     : area.containsMouse ? Theme.alpha(Theme.text, 0.55)
                     : slot.active ? root.activeColor
                     : slot.empty ? "transparent"
                     : Theme.alpha(Theme.text, 0.85)
                border.width: slot.active ? 0 : 1
                border.color: slot.empty ? Theme.alpha(Theme.text, 0.35) : "transparent"
                Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
                Behavior on height { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: 250; easing.type: Easing.OutCubic } }

                Text {
                    anchors.centerIn: parent
                    visible: dot.pill
                    text: slot.modelData.n
                    color: Theme.bgPanel
                    font.family: Theme.fontFamily
                    font.pixelSize: 10
                    font.bold: true
                    font.hintingPreference: Font.PreferFullHinting
                    renderType: Text.NativeRendering
                }
            }

            // ---- dwl: a flat block per tag ----
            Rectangle {
                visible: root.dwl
                anchors.fill: parent
                color: slot.urgent ? Theme.danger : slot.active ? root.activeColor
                     : area.containsMouse ? Theme.alpha(Theme.text, 0.1) : "transparent"
                Behavior on color { ColorAnimation { duration: 160 } }
                // tag has windows: a small square in the corner (filled on the active tag)
                Rectangle {
                    visible: !slot.empty
                    x: 3; y: 3
                    width: 4; height: 4
                    color: slot.active ? Theme.bgPanel : Theme.text
                    opacity: slot.active ? 1 : 0.8
                }
            }

            // ---- glyph styles ----
            Text {
                id: label
                visible: root.textStyle
                x: root.dwl ? (slot.width - implicitWidth) / 2 : 6
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: root.dwl ? 0 : -1
                text: root.glyph(slot.modelData.n)
                color: root.dwl && (slot.active || slot.urgent) ? Theme.bgPanel
                     : slot.urgent ? Theme.danger
                     : slot.active ? root.activeColor
                     : area.containsMouse ? Theme.text
                     : slot.empty ? Theme.alpha(Theme.text, 0.35)
                     : Theme.alpha(Theme.text, 0.85)
                font.family: Theme.fontFamily
                font.pixelSize: root.dwl ? Config.bar.fontSize : 12
                font.bold: slot.active && !root.dwl
                font.hintingPreference: Font.PreferFullHinting
                renderType: Text.NativeRendering
                Behavior on color { ColorAnimation { duration: 250; easing.type: Easing.OutCubic } }
            }
            Rectangle {
                visible: root.textStyle && !root.dwl
                anchors { horizontalCenter: label.horizontalCenter; top: label.bottom; topMargin: 1 }
                height: 2
                radius: 1
                width: slot.active ? Math.max(8, label.implicitWidth) : 0
                color: root.activeColor
                Behavior on width { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
            }

            MouseArea {
                id: area
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.activated(slot.modelData)
            }
        }
    }
}
