// PowerMenu.qml — lock · log out · suspend · reboot · shut down (replaces wlogout).
// The risky three ask for a second click within 3 s. ←/→ to move, Enter to pick, Esc to close.
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

    readonly property var items: [
        { icon: 0xf033e, label: "Lock", confirm: false, run: () => Quickshell.execDetached(["kusanagi", "msg", "lock", "lock"]) },
        { icon: 0xf0343, label: "Log out", confirm: true, run: () => Wm.quit() },
        { icon: 0xf04b2, label: "Suspend", confirm: false, run: () => Quickshell.execDetached(["sh", "-c", "loginctl suspend || systemctl suspend"]) },
        { icon: 0xf0709, label: "Reboot", confirm: true, run: () => Quickshell.execDetached(["sh", "-c", "loginctl reboot || systemctl reboot"]) },
        { icon: 0xf0425, label: "Shut down", confirm: true, run: () => Quickshell.execDetached(["sh", "-c", "loginctl poweroff || systemctl poweroff"]) }
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

    Rectangle {
        id: backdrop
        anchors.fill: parent
        color: Theme.alpha("#000000", Math.min(0.6, Config.look.backdrop * 1.6) * card.opacity)
        focus: root.showing
        MouseArea { anchors.fill: parent; onClicked: root.close() }
        Keys.onPressed: e => {
            if (e.key === Qt.Key_Escape) root.close()
            else if (e.key === Qt.Key_Right || e.key === Qt.Key_Tab) root.sel = (root.sel + 1) % root.items.length
            else if (e.key === Qt.Key_Left || e.key === Qt.Key_Backtab) root.sel = (root.sel - 1 + root.items.length) % root.items.length
            else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Space) root.pick(root.sel)
            else return
            e.accepted = true
        }
    }

    RectangularShadow {
        visible: Config.look.shadows
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
        width: row.implicitWidth + 48
        height: 172
        anchors.centerIn: parent
        radius: Config.look.radius
        color: Theme.alpha(Theme.bgPanel, Config.panel.opacity)
        border.width: Theme.surfaceBorderWidth
        border.color: Theme.surfaceBorder
        opacity: root.showing ? 1 : 0
        scale: root.showing ? 1 : 0.92
        Behavior on opacity { NumberAnimation { duration: Config.ms(root.showing ? 200 : 140) } }
        Behavior on scale { NumberAnimation { duration: Config.ms(root.showing ? 420 : 160); easing.type: root.showing ? Easing.OutBack : Easing.InCubic; easing.overshoot: Config.bounce(1.4) } }
        MouseArea { anchors.fill: parent }

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 14
            Repeater {
                model: root.items
                Column {
                    id: btn
                    required property var modelData
                    required property int index
                    readonly property bool current: root.sel === index
                    readonly property bool armed: root.armed === index
                    spacing: 10
                    Rectangle {
                        width: 84; height: 84
                        radius: 42
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: btn.armed ? Theme.danger : btn.current ? Theme.accent : Theme.alpha(Theme.text, area.containsMouse ? 0.1 : 0.06)
                        border.width: btn.current && !btn.armed ? 0 : 1
                        border.color: Theme.alpha(Theme.text, 0.08)
                        scale: area.pressed ? 0.92 : btn.current ? 1.05 : 1
                        Behavior on color { ColorAnimation { duration: Config.ms(160) } }
                        Behavior on scale { NumberAnimation { duration: Config.ms(220); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(2) } }
                        CpIcon {
                            anchors.centerIn: parent
                            cp: btn.modelData.icon
                            font.pixelSize: 30
                            color: btn.current || btn.armed ? Theme.bgPanel : Theme.text
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
                    CpText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: btn.armed ? "Click again" : btn.modelData.label
                        font.pixelSize: 12
                        font.bold: btn.current || btn.armed
                        color: btn.armed ? Theme.danger : Theme.text
                    }
                }
            }
        }
    }
}
