// SpStylePicker.qml — pick a design by looking at it: one card per option with a tiny drawing of a
// screen showing that style (plain rectangles, drawn only while Settings is open).
//   kind: launcher · panel · notifications · osd · corner (notification spot) · edge (OSD spot)
//   options: [{ value, label, note }]   current   picked(value)
import QtQuick

Flow {
    id: root
    property string kind: ""
    property var options: []
    property var current
    property int cardW: 168
    signal picked(var value)
    width: parent ? parent.width : 600
    spacing: 10

    Repeater {
        model: root.options
        Rectangle {
            id: card
            required property var modelData
            readonly property bool on: root.current === modelData.value
            width: root.cardW
            height: modelData.note ? 132 : 116
            radius: 12
            color: hov.hovered ? Theme.alpha(Theme.text, 0.08) : Theme.alpha(Theme.text, 0.04)
            border.width: on ? 2 : 1
            border.color: on ? Theme.accent : Theme.alpha(Theme.text, 0.07)
            scale: tap.pressed ? 0.97 : 1
            Behavior on color { ColorAnimation { duration: Config.ms(120) } }
            Behavior on border.color { ColorAnimation { duration: Config.ms(160) } }
            Behavior on scale { NumberAnimation { duration: Config.ms(140); easing.type: Easing.OutBack } }

            // the little screen
            Rectangle {
                id: scr
                x: 9; y: 9
                width: parent.width - 18
                height: 74
                radius: 6
                clip: true
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.darker(Theme.accent, 3.2) }
                    GradientStop { position: 1; color: Qt.darker(Theme.accent2, 4.2) }
                }
                Loader {
                    anchors.fill: parent
                    sourceComponent: ({ launcher: launcherC, panel: panelC, notifications: notifC, osd: osdC, corner: cornerC, edge: edgeC, widget: widgetC, tiles: tilesC, panelLook: lookC })[root.kind] ?? null
                    property var v: card.modelData.value
                }
            }
            Column {
                x: 12; y: 90
                width: parent.width - 24
                spacing: 1
                CpText { text: card.modelData.label; font.pixelSize: 12; font.bold: true; color: card.on ? Theme.accent : Theme.text }
                CpText { visible: !!card.modelData.note; width: parent.width; elide: Text.ElideRight; text: card.modelData.note || ""; font.pixelSize: 9; color: Theme.textDim }
            }
            HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
            TapHandler { id: tap; onTapped: root.picked(card.modelData.value) }
        }
    }

    // ---- drawings (parent = the little screen; `v` = the option's value) ----
    readonly property color surf: Theme.alpha(Theme.bgPanel, 0.92)
    readonly property color line: Theme.alpha(Theme.text, 0.35)
    readonly property color acc: Theme.accent

    // a few text lines (inline components don't see this file's ids: own colour)
    component Lines: Column {
        id: ls
        property int n: 3
        spacing: 3
        Repeater {
            model: ls.n
            Rectangle {
                required property int index
                width: ls.width * (index === 0 ? 0.8 : 0.55)
                height: 2; radius: 1
                color: Theme.alpha(Theme.text, 0.35)
            }
        }
    }

    Component {
        id: launcherC
        Item {
            readonly property string v: parent.v
            // card · spotlight · fullscreen · side
            Rectangle {
                visible: v === "card"
                x: parent.width * 0.22; y: parent.height * 0.16; width: parent.width * 0.56; height: parent.height * 0.62; radius: 4; color: root.surf
                Rectangle { x: 5; y: 5; width: parent.width - 10; height: 6; radius: 3; color: Theme.alpha(Theme.text, 0.15) }
                Lines { x: 6; y: 17; width: parent.width - 12; n: 4 }
            }
            Rectangle {
                visible: v === "spotlight"
                x: parent.width * 0.2; y: parent.height * 0.32; width: parent.width * 0.6; height: 12; radius: 6; color: root.surf
                Rectangle { x: 6; y: 5; width: 3; height: 3; radius: 1.5; color: root.acc }
                Rectangle { x: 13; y: 5; width: parent.width * 0.4; height: 2; radius: 1; color: root.line }
            }
            Rectangle {
                visible: v === "fullscreen"
                anchors.fill: parent; color: root.surf
                Rectangle { x: parent.width * 0.3; y: 6; width: parent.width * 0.4; height: 6; radius: 3; color: Theme.alpha(Theme.text, 0.15) }
                Grid {
                    x: parent.width * 0.14; y: 20; columns: 6; spacing: 7
                    Repeater { model: 18; Rectangle { width: 8; height: 8; radius: 2; color: index === 0 ? root.acc : Theme.alpha(Theme.text, 0.3); required property int index } }
                }
            }
            Rectangle {
                visible: v === "side"
                width: parent.width * 0.36; height: parent.height; color: root.surf
                Rectangle { x: 5; y: 5; width: parent.width - 10; height: 6; radius: 3; color: Theme.alpha(Theme.text, 0.15) }
                Lines { x: 6; y: 17; width: parent.width - 12; n: 6 }
            }
        }
    }
    Component {
        id: panelC
        Item {
            readonly property string v: parent.v
            // a top bar with the clock in the middle
            Rectangle { width: parent.width; height: 6; color: Theme.alpha(Theme.bgPanel, 0.6) }
            Rectangle { x: parent.width / 2 - 8; y: 1; width: 16; height: 4; radius: 2; color: v === "island" ? root.acc : root.line; visible: v !== "sheet" }
            Rectangle {
                visible: v === "island" || v === "fade"
                x: parent.width * 0.27; y: v === "island" ? 8 : 12; width: parent.width * 0.46; height: parent.height * 0.62; radius: 5
                color: root.surf; opacity: v === "fade" ? 0.75 : 1
                Rectangle { visible: v === "island"; x: parent.width / 2 - 8; y: -3; width: 16; height: 4; radius: 2; color: root.acc }
                Grid { x: 6; y: 7; columns: 3; spacing: 4; Repeater { model: 6; Rectangle { width: 14; height: 9; radius: 2; color: Theme.alpha(Theme.text, 0.18) } } }
            }
            Rectangle {
                visible: v === "drop"
                x: 0; y: 6; width: parent.width; height: parent.height * 0.5; color: root.surf
                Grid { x: parent.width * 0.3; y: 6; columns: 4; spacing: 4; Repeater { model: 8; Rectangle { width: 10; height: 7; radius: 2; color: Theme.alpha(Theme.text, 0.18) } } }
            }
            Rectangle {
                visible: v === "sheet"
                x: parent.width * 0.6; y: 8; width: parent.width * 0.37; height: parent.height - 12; radius: 4; color: root.surf
                Grid { x: 5; y: 6; columns: 2; spacing: 4; Repeater { model: 6; Rectangle { width: 16; height: 8; radius: 2; color: Theme.alpha(Theme.text, 0.18) } } }
            }
        }
    }
    Component {
        id: notifC
        Item {
            readonly property string v: parent.v
            // comfortable · compact · minimal · accent: two toasts top-right
            Column {
                x: parent.width * 0.42; y: 6; width: parent.width * 0.54
                spacing: v === "comfortable" || v === "accent" ? 5 : 4
                Repeater {
                    model: 2
                    Rectangle {
                        width: parent.width
                        height: v === "comfortable" || v === "accent" ? 22 : v === "compact" ? 15 : 11
                        radius: v === "minimal" ? height / 2 : 4
                        color: root.surf
                        Rectangle { visible: v === "accent"; x: 3; y: 4; width: 2; height: parent.height - 8; radius: 1; color: root.acc }
                        Rectangle { x: v === "accent" ? 9 : 6; y: v === "minimal" ? 4 : 4; width: 14; height: 2; radius: 1; color: v === "minimal" ? Theme.text : root.acc }
                        Rectangle { visible: v !== "minimal"; x: v === "accent" ? 9 : 6; y: 9; width: parent.width * 0.6; height: 2; radius: 1; color: root.line }
                        Rectangle { visible: v === "comfortable" || v === "accent"; x: v === "accent" ? 9 : 6; y: 14; width: parent.width * 0.45; height: 2; radius: 1; color: root.line }
                        Rectangle { visible: v === "minimal"; x: 24; y: 5; width: parent.width * 0.45; height: 2; radius: 1; color: root.line }
                    }
                }
            }
        }
    }
    Component {
        id: osdC
        Item {
            readonly property string v: parent.v
            Rectangle {
                visible: v === "pill"
                x: parent.width * 0.25; y: 10; width: parent.width * 0.5; height: 12; radius: 6; color: root.surf
                Rectangle { x: 3; y: 2; width: 8; height: 8; radius: 4; color: root.acc }
                Rectangle { x: 15; y: 5; width: parent.width - 22; height: 2; radius: 1; color: Theme.alpha(Theme.text, 0.2)
                    Rectangle { width: parent.width * 0.6; height: 2; radius: 1; color: root.acc } }
            }
            Rectangle {
                visible: v === "minimal"
                x: parent.width * 0.22; y: 12; width: parent.width * 0.56; height: 7; radius: 3.5; color: root.surf
                Rectangle { x: 6; y: 2.5; width: parent.width - 12; height: 2; radius: 1; color: Theme.alpha(Theme.text, 0.2)
                    Rectangle { width: parent.width * 0.6; height: 2; radius: 1; color: root.acc } }
            }
            Rectangle {
                visible: v === "box"
                x: parent.width / 2 - 18; y: parent.height * 0.42; width: 36; height: 30; radius: 6; color: root.surf
                Rectangle { x: 12; y: 5; width: 12; height: 10; radius: 2; color: Theme.text }
                Row { x: 5; y: 22; spacing: 1; Repeater { model: 8; Rectangle { width: 2.6; height: 3; color: index < 5 ? root.acc : Theme.alpha(Theme.text, 0.2); required property int index } } }
            }
        }
    }
    Component {
        id: tilesC
        Item {
            readonly property string v: parent.v
            Grid {
                anchors.centerIn: parent
                columns: v === "icons" ? 5 : v === "pills" ? 2 : 4
                spacing: 4
                Repeater {
                    model: v === "icons" ? 10 : v === "pills" ? 6 : 8
                    Rectangle {
                        required property int index
                        width: v === "icons" ? 16 : v === "pills" ? 50 : 24
                        height: v === "icons" ? 16 : v === "pills" ? 12 : 18
                        radius: v === "cards" ? 3 : height / 2
                        color: index === 0 ? root.acc : Theme.alpha(Theme.text, 0.2)
                        Rectangle { visible: v === "pills"; x: 4; y: 5; width: 3; height: 3; radius: 1.5; color: Theme.text }
                        Rectangle { visible: v === "pills"; x: 10; y: 5; width: 20; height: 2; radius: 1; color: root.line }
                        Rectangle { visible: v === "cards"; x: 3; y: 3; width: 4; height: 4; radius: 1; color: Theme.text }
                        Rectangle { visible: v === "cards"; x: 3; y: 12; width: 14; height: 2; radius: 1; color: root.line }
                    }
                }
            }
        }
    }
    Component {
        id: lookC
        Item {
            readonly property string v: parent.v
            Rectangle {
                anchors.centerIn: parent; width: parent.width * 0.62; height: parent.height * 0.86; radius: 5; color: root.surf
                Column {
                    x: 5; y: 5; width: parent.width - 10; spacing: 3
                    Rectangle { visible: v !== "icons"; width: v === "compact" ? 16 : 22; height: v === "compact" ? 3 : 5; radius: 1; color: Theme.text }
                    Rectangle { visible: v === "dashboard"; width: parent.width; height: 14; radius: 3; color: Theme.alpha(Theme.accent, 0.35) }
                    Grid {
                        columns: v === "icons" ? 4 : v === "compact" ? 2 : v === "dashboard" ? 3 : 4; spacing: 2
                        Repeater { model: v === "icons" ? 8 : v === "compact" ? 4 : 6
                            Rectangle { width: v === "icons" ? 10 : v === "compact" ? 30 : v === "dashboard" ? 20 : 14; height: v === "icons" ? 10 : v === "compact" ? 7 : 10
                                        radius: v === "icons" || v === "compact" ? height / 2 : 2; color: Theme.alpha(Theme.text, 0.22) } }
                    }
                    Rectangle { width: parent.width; height: v === "compact" || v === "icons" ? 2 : 6; radius: 3; color: root.acc }
                }
            }
        }
    }
    Component {
        id: widgetC
        Item {
            readonly property string v: parent.v
            // a hint of the widget on a little desktop
            Rectangle {
                visible: v !== "clock" && v !== "greeting" && v !== "text"
                anchors.centerIn: parent; width: parent.width * 0.62; height: parent.height * 0.62; radius: 6
                color: root.surf
            }
            CpText {
                visible: v === "clock" || v === "greeting" || v === "text"
                anchors.centerIn: parent
                text: v === "clock" ? "19:00" : v === "greeting" ? "Hi, you." : "“words”"
                font.pixelSize: v === "clock" ? 22 : 14; font.bold: v !== "clock"; font.weight: v === "clock" ? Font.Light : Font.Bold
                color: Theme.text
            }
            CpIcon {
                visible: v !== "clock" && v !== "greeting" && v !== "text"
                anchors.centerIn: parent
                cp: ({ media: 0xf075a, stats: 0xf029a, weather: 0xf0599, calendar: 0xf00ed })[v] ?? 0xf072e
                font.pixelSize: 24; color: root.acc
            }
        }
    }
    Component {
        id: cornerC
        Item {
            readonly property string v: parent.v
            Rectangle {
                width: parent.width * 0.38; height: 14; radius: 4; color: root.surf
                x: v.endsWith("left") ? 5 : v.endsWith("right") ? parent.width - width - 5 : (parent.width - width) / 2
                y: v.startsWith("top") ? 5 : parent.height - height - 5
                Rectangle { x: 5; y: 4; width: 12; height: 2; radius: 1; color: root.acc }
                Rectangle { x: 5; y: 8; width: parent.width * 0.6; height: 2; radius: 1; color: root.line }
            }
        }
    }
    Component {
        id: edgeC
        Item {
            readonly property string v: parent.v
            readonly property bool side: v === "left" || v === "right"
            Rectangle {
                width: parent.side ? 9 : parent.width * 0.42; height: parent.side ? parent.height * 0.6 : 9; radius: 4.5; color: root.surf
                x: v === "left" ? 5 : v === "right" ? parent.width - width - 5 : (parent.width - width) / 2
                y: v === "top" ? 6 : v === "bottom" ? parent.height - height - 6 : (parent.height - height) / 2
                Rectangle {
                    color: root.acc; radius: 1
                    x: parent.parent.side ? 3.5 : 4; y: parent.parent.side ? parent.height * 0.35 : 3.5
                    width: parent.parent.side ? 2 : parent.width * 0.55; height: parent.parent.side ? parent.height * 0.6 : 2
                }
            }
        }
    }
}
