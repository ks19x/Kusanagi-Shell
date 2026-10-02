// NotificationPopups.qml — toasts under the bar (Config.notifications: position, timeout, max).
// New ones drop in with a little spring, the rest glide down to make room, timed-out ones slide away
// and the stack closes up smoothly. Hover pauses the countdown; swipe right to dismiss.
import Quickshell
import Quickshell.Wayland
import QtQuick

PanelWindow {
    id: root

    readonly property string pos: Config.notifications.position
    readonly property bool barBottom: Config.bar.position === "bottom"
    anchors { top: true; right: pos === "top-right"; left: pos === "top-left" }
    margins { top: barBottom ? 10 : 6; right: 10; left: 10 }
    implicitWidth: 400
    implicitHeight: Math.max(1, column.implicitHeight + 24)     // room for shadows
    visible: Notifs.popups.length > 0 || column.children.length > 1
    color: "transparent"
    exclusionMode: ExclusionMode.Normal
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "notifications"
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    // only the cards take clicks
    mask: Region { item: column }

    Column {
        id: column
        width: parent.width
        spacing: Config.notifications.style === "compact" ? 6 : 10

        // others glide to their new place when one arrives or leaves
        move: Transition { NumberAnimation { properties: "y"; duration: Config.ms(380); easing.type: Easing.OutQuint } }

        Repeater {
            model: Notifs.popups
            delegate: NotificationCard {
                id: toast
                required property var modelData
                notif: modelData
                popup: true
                onDone: Notifs.removePopup(modelData)

                // ---- arrival: drop in from the edge it lives on ----
                readonly property real fromX: root.pos === "top-left" ? -60 : root.pos === "top-center" ? 0 : 60
                transform: Translate { id: shift; x: toast.fromX; y: root.pos === "top-center" ? -24 : 0 }
                opacity: 0
                scale: 0.94
                Component.onCompleted: arrive.start()
                ParallelAnimation {
                    id: arrive
                    NumberAnimation { target: shift; property: "x"; to: 0; duration: Config.ms(480); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(1.1) }
                    NumberAnimation { target: shift; property: "y"; to: 0; duration: Config.ms(480); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(1.1) }
                    NumberAnimation { target: toast; property: "opacity"; to: 1; duration: Config.ms(220) }
                    NumberAnimation { target: toast; property: "scale"; to: 1; duration: Config.ms(480); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(1.4) }
                }

                // ---- countdown: pauses while hovered, critical ones stay ----
                NumberAnimation on remaining {
                    id: countdown
                    from: 1; to: 0
                    duration: Config.notifications.timeout
                    running: !toast.critical
                    paused: running && toast.hovered
                    onFinished: toast.dismiss()
                }
            }
        }
    }
}
