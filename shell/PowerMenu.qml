// PowerMenu.qml — lock · log out · suspend · reboot · shut down (replaces wlogout).
// The risky three ask for a second click within 3 s. Arrows / Tab to move, Enter to pick, Esc to close.
// Config.power.style: row (round buttons in a card) · tiles (big squares) · list (a menu in the corner)
// · fullscreen (huge buttons over a dark screen) · pill (a slim icon bar at the bottom).
// Works on any init: loginctl (systemd-logind or elogind), falling back to systemctl.
import Quickshell
import Quickshell.Wayland
import QtQuick
import QtQuick.Effects

PanelWindow {
    id: root

    property bool showing: false
    function open() { armed = -1; sel = 0; showing = true }
    function close() { showing = false; armed = -1 }
    function toggle() { showing ? close() : open() }

    anchors { top: true; left: true; right: true; bottom: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "quickshell-power"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: showing ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    visible: showing || card.opacity > 0.01
    mask: Region { item: root.showing ? backdrop : null }

    readonly property string style: Config.power.style
    readonly property bool list: style === "list"
    readonly property bool tiles: style === "tiles"
    readonly property bool full: style === "fullscreen"
    readonly property bool pill: style === "pill"

    readonly property var items: [
        { icon: 0xf033e, label: "Lock", hint: "L", confirm: false, run: () => Quickshell.execDetached(["kusanagi", "msg", "lock", "lock"]) },
        { icon: 0xf0343, label: "Log out", hint: "E", confirm: true, run: () => Wm.quit() },
        { icon: 0xf04b2, label: "Suspend", hint: "S", confirm: false, run: () => Quickshell.execDetached(["sh", "-c", "loginctl suspend || systemctl suspend"]) },
        { icon: 0xf0709, label: "Reboot", hint: "R", confirm: true, run: () => Quickshell.execDetached(["sh", "-c", "loginctl reboot || systemctl reboot"]) },
        { icon: 0xf0425, label: "Shut down", hint: "P", confirm: true, run: () => Quickshell.execDetached(["sh", "-c", "loginctl poweroff || systemctl poweroff"]) }
    ]
    property int sel: 0
    property int armed: -1               // index waiting for its second click
    Timer { id: disarm; interval: 3000; onTriggered: root.armed = -1 }

    function pick(i) {
        const it = items[i]
        if (it.confirm && armed !== i) { armed = i; disarm.restart(); return }
        close()
        it.run()
    }
    function step(d) { sel = (sel + d + items.length) % items.length }

    Rectangle {
        id: backdrop
        anchors.fill: parent
        color: Theme.alpha("#000000", (root.full ? 0.78 : Math.min(0.6, Config.look.backdrop * 1.6)) * card.opacity)
        focus: root.showing
        MouseArea { anchors.fill: parent; onClicked: root.close() }
        Keys.onPressed: e => {
            const next = root.list ? Qt.Key_Down : Qt.Key_Right, prev = root.list ? Qt.Key_Up : Qt.Key_Left
            if (e.key === Qt.Key_Escape) root.close()
            else if (e.key === next || e.key === Qt.Key_Tab) root.step(1)
            else if (e.key === prev || e.key === Qt.Key_Backtab) root.step(-1)
            else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space) root.pick(root.sel)
            else {
                // the letter of an item picks it (L, E, S, R, P)
                const i = root.items.findIndex(it => it.hint === e.text.toUpperCase())
                if (i < 0) return
                root.sel = i; root.pick(i)
            }
            e.accepted = true
        }
    }

    RectangularShadow {
        visible: Config.look.shadows && !root.full
        anchors.fill: card
        opacity: card.opacity
        scale: card.scale
        offset.y: 14
        blur: 40
        radius: card.radius
        color: Theme.alpha("#000000", 0.5)
    }

    Rectangle {
        id: card
        readonly property real padH: root.list ? 8 : root.pill ? 10 : root.full ? 0 : 24
        readonly property real padV: root.list ? 8 : root.pill ? 8 : root.full ? 0 : 22
        width: grid.implicitWidth + 2 * padH
        height: grid.implicitHeight + 2 * padV
        // list: under the bar at the right · pill: low in the middle · the rest: centred
        x: root.list ? root.width - width - 14 : Math.round((root.width - width) / 2)
        y: root.list ? (BarSpec.edge === "top" ? BarSpec.thickness + 8 : 14)
         : root.pill ? root.height - height - 64 : Math.round((root.height - height) / 2)
        radius: root.pill ? height / 2 : root.list ? Math.max(10, Config.look.radius - 2) : Config.look.radius
        color: root.full ? "transparent" : Theme.alpha(Theme.bgPanel, Config.panel.opacity)
        border.width: root.full ? 0 : Theme.surfaceBorderWidth
        border.color: Theme.surfaceBorder
        opacity: root.showing ? 1 : 0
        scale: root.showing ? 1 : root.full ? 1.06 : 0.92
        transformOrigin: root.list ? Item.TopRight : root.pill ? Item.Bottom : Item.Center
        Behavior on opacity { NumberAnimation { duration: Config.ms(root.showing ? 200 : 140) } }
        Behavior on scale { NumberAnimation { duration: Config.ms(root.showing ? 420 : 160); easing.type: root.showing ? Easing.OutBack : Easing.InCubic; easing.overshoot: Config.bounce(1.4) } }
        MouseArea { anchors.fill: parent }

        // the pill names what's under the pointer above itself
        CpText {
            visible: root.pill
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.top; bottomMargin: 10 }
            text: root.armed >= 0 ? "Click again to " + root.items[root.armed].label.toLowerCase() : root.items[root.sel].label
            font.pixelSize: 13
            font.bold: true
            color: root.armed >= 0 ? Theme.danger : Theme.text
        }

        Grid {
            id: grid
            x: card.padH; y: card.padV
            columns: root.list ? 1 : root.items.length
            spacing: root.list ? 2 : root.full ? 44 : root.pill ? 6 : root.tiles ? 10 : 14
            Repeater {
                model: root.items
                Item {
                    id: btn
                    required property var modelData
                    required property int index
                    readonly property bool current: root.sel === index
                    readonly property bool armed: root.armed === index
                    readonly property color face: armed ? Theme.danger : current ? Theme.accent : Theme.alpha(Theme.text, area.containsMouse ? 0.1 : 0.06)
                    readonly property color ink: current || armed ? Theme.bgPanel : Theme.text
                    width: root.list ? 236 : root.tiles ? 118 : root.full ? 150 : root.pill ? 48 : 84
                    height: root.list ? 42 : root.tiles ? 118 : root.full ? 190 : root.pill ? 48 : 118

                    // round faces: row, fullscreen, pill
                    Rectangle {
                        visible: !root.list && !root.tiles
                        readonly property real d: root.full ? 150 : root.pill ? 48 : 84
                        width: d; height: d; radius: d / 2
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: btn.face
                        border.width: btn.current && !btn.armed ? 0 : 1
                        border.color: Theme.alpha(Theme.text, root.full ? 0.15 : 0.08)
                        scale: area.pressed ? 0.92 : btn.current ? 1.05 : 1
                        Behavior on color { ColorAnimation { duration: Config.ms(160) } }
                        Behavior on scale { NumberAnimation { duration: Config.ms(220); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(2) } }
                        CpIcon { anchors.centerIn: parent; cp: btn.modelData.icon; font.pixelSize: root.full ? 52 : root.pill ? 20 : 30; color: btn.ink }
                    }
                    CpText {
                        visible: !root.list && !root.tiles && !root.pill
                        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom }
                        text: btn.armed ? "Click again" : btn.modelData.label
                        font.pixelSize: root.full ? 17 : 12
                        font.bold: btn.current || btn.armed
                        color: btn.armed ? Theme.danger : Theme.text
                    }

                    // tiles: a square with the icon over the name
                    Rectangle {
                        visible: root.tiles
                        anchors.fill: parent
                        radius: Math.max(10, Config.look.radius - 4)
                        color: btn.face
                        border.width: btn.current && !btn.armed ? 0 : 1
                        border.color: Theme.alpha(Theme.text, 0.08)
                        scale: area.pressed ? 0.95 : 1
                        Behavior on color { ColorAnimation { duration: Config.ms(160) } }
                        Behavior on scale { NumberAnimation { duration: Config.ms(160) } }
                        CpIcon { anchors.centerIn: parent; anchors.verticalCenterOffset: -12; cp: btn.modelData.icon; font.pixelSize: 34; color: btn.ink }
                        CpText {
                            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 14 }
                            text: btn.armed ? "Again?" : btn.modelData.label
                            font.pixelSize: 12; font.bold: true
                            color: btn.ink
                        }
                    }

                    // list: a menu row with its key on the right
                    Rectangle {
                        visible: root.list
                        anchors.fill: parent
                        radius: 9
                        color: btn.armed ? Theme.danger : btn.current ? Theme.alpha(Theme.accent, 0.9) : area.containsMouse ? Theme.alpha(Theme.text, 0.07) : "transparent"
                        Behavior on color { ColorAnimation { duration: Config.ms(120) } }
                        CpIcon { x: 14; anchors.verticalCenter: parent.verticalCenter; cp: btn.modelData.icon; font.pixelSize: 17; color: btn.ink }
                        CpText {
                            x: 44; anchors.verticalCenter: parent.verticalCenter
                            text: btn.armed ? "Click again to " + btn.modelData.label.toLowerCase() : btn.modelData.label
                            font.pixelSize: 13; font.bold: btn.current
                            color: btn.ink
                        }
                        CpText {
                            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                            text: btn.modelData.hint
                            font.pixelSize: 11
                            color: btn.current || btn.armed ? Theme.alpha(Theme.bgPanel, 0.7) : Theme.textDim
                        }
                    }

                    MouseArea {
                        id: area
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: root.sel = btn.index
                        onClicked: root.pick(btn.index)
                    }
                }
            }
        }
    }
}
