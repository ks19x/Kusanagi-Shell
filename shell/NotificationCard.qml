// NotificationCard.qml — one notification, in the panel's design language. Used by the popups
// (popup: true — countdown line, entry/exit animations) and by the Inbox list.
// Click = default action · swipe right or the × = dismiss · right click = dismiss.
import Quickshell
import Quickshell.Widgets
import Quickshell.Services.Notifications
import QtQuick
import QtQuick.Effects

Item {
    id: card

    required property var notif
    property bool popup: false
    property real remaining: 1           // popups: 1 → 0 over the timeout (drawn as a line)
    readonly property bool hovered: hover.hovered
    signal done()                        // gone: dismissed, swiped, acted on

    readonly property bool critical: notif && notif.urgency === NotificationUrgency.Critical
    readonly property string image: notif && notif.image ? notif.image : ""
    readonly property string appIcon: {
        if (!notif || !notif.appIcon) return ""
        const i = notif.appIcon
        return i.startsWith("/") || i.startsWith("file:") || i.startsWith("image:") ? i : Quickshell.iconPath(i, true)
    }
    readonly property real r: Math.max(8, Config.look.radius - 2)
    readonly property bool compact: Config.notifications.style === "compact"
    readonly property int pad: compact ? 10 : 14

    width: parent ? parent.width : 380
    implicitHeight: body.implicitHeight + 2 * pad

    // ---- leaving: swipe or click flies the card off to the right ----
    property bool leaving: false
    function dismiss() {
        if (leaving) return
        leaving = true
        out.start()
    }
    SequentialAnimation {
        id: out
        ParallelAnimation {
            NumberAnimation { target: surface; property: "x"; to: card.width + 40; duration: Config.ms(260); easing.type: Easing.InCubic }
            NumberAnimation { target: surface; property: "opacity"; to: 0; duration: Config.ms(240) }
        }
        ScriptAction { script: { if (card.notif) card.notif.dismiss(); card.done() } }
    }

    RectangularShadow {
        visible: Config.look.shadows && card.popup
        anchors.fill: surface
        opacity: surface.opacity
        offset.y: 6
        blur: 22
        radius: surface.radius
        color: Theme.alpha("#000000", 0.38)
    }

    Rectangle {
        id: surface
        width: parent.width
        height: card.implicitHeight
        radius: card.r
        color: card.popup ? Theme.alpha(Theme.bgPanel, Math.max(0.88, Config.panel.opacity))
                          : Theme.alpha(Theme.text, hover.hovered ? 0.07 : 0.045)
        border.width: card.critical ? 1 : Theme.surfaceBorderWidth
        border.color: card.critical ? Theme.alpha(Theme.danger, 0.7)
                    : hover.hovered ? Theme.alpha(Theme.text, 0.14) : Theme.surfaceBorder
        clip: true
        Behavior on color { ColorAnimation { duration: Config.ms(160) } }
        Behavior on border.color { ColorAnimation { duration: Config.ms(160) } }

        // spring back when a swipe isn't far enough
        Behavior on x { enabled: !drag.active && !card.leaving; NumberAnimation { duration: Config.ms(320); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(1.2) } }
        opacity: 1 - Math.max(0, x) / (card.width * 1.2)

        HoverHandler { id: hover }
        DragHandler {
            id: drag
            target: surface
            xAxis.minimum: -20
            yAxis.enabled: false
            onActiveChanged: if (!active) { if (surface.x > card.width * 0.3) card.dismiss(); else surface.x = 0 }
        }

        // urgent: a red edge
        Rectangle {
            visible: card.critical
            width: 3; radius: 1.5
            height: parent.height - 20
            x: 6; anchors.verticalCenter: parent.verticalCenter
            color: Theme.danger
        }

        Row {
            id: body
            x: card.pad; y: card.pad
            width: parent.width - 2 * card.pad
            spacing: 12

            // big picture (album art, avatar…) when the app sent one
            ClippingRectangle {
                visible: Config.notifications.images && card.image !== "" && pic.status !== Image.Error
                width: visible ? (card.compact ? 36 : 48) : 0; height: card.compact ? 36 : 48
                radius: Math.max(6, card.r - 6)
                color: Theme.alpha(Theme.text, 0.06)
                Image {
                    id: pic
                    anchors.fill: parent
                    source: card.image
                    sourceSize: Qt.size(96, 96)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
            }

            Column {
                width: body.width - (Config.notifications.images && card.image !== "" && pic.status !== Image.Error ? (card.compact ? 48 : 60) : 0)
                spacing: 3

                // app · time · ×
                Item {
                    width: parent.width
                    height: 16
                    Row {
                        spacing: 6
                        anchors.verticalCenter: parent.verticalCenter
                        IconImage {
                            visible: card.appIcon !== ""
                            implicitSize: 14
                            source: card.appIcon
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        CpText {
                            text: card.notif ? (card.notif.appName || "Notification") : ""
                            font.pixelSize: 10
                            font.bold: true
                            font.letterSpacing: 1
                            font.capitalization: Font.AllUppercase
                            color: card.critical ? Theme.danger : Theme.accent
                        }
                        CpText {
                            text: Notifs.ago(card.notif) ? "·  " + Notifs.ago(card.notif) : ""
                            font.pixelSize: 10
                            color: Theme.textDim
                        }
                    }
                    CpIconButton {
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        width: 22; height: 22
                        icon: 0xf0156
                        iconSize: 13
                        opacity: hover.hovered ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: Config.ms(140) } }
                        onClicked: card.dismiss()
                    }
                }

                CpText {
                    width: parent.width
                    text: card.notif ? card.notif.summary : ""
                    font.pixelSize: 13
                    font.bold: true
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignTop
                }
                CpText {
                    width: parent.width
                    visible: text !== ""
                    text: card.notif ? card.notif.body : ""
                    textFormat: Text.StyledText
                    font.pixelSize: 12
                    color: Theme.alpha(Theme.text, 0.72)
                    wrapMode: Text.Wrap
                    maximumLineCount: card.compact ? (card.popup ? 1 : 2) : (card.popup ? 3 : 6)
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignTop
                }

                // the app's buttons (not the invisible "default" one)
                Flow {
                    width: parent.width
                    spacing: 6
                    topPadding: 6
                    visible: actions.count > 0
                    Repeater {
                        id: actions
                        model: card.notif ? card.notif.actions.filter(a => a.identifier !== "default") : []
                        CpChip {
                            required property var modelData
                            label: modelData.text
                            onClicked: { modelData.invoke(); card.dismiss() }
                        }
                    }
                }
            }
        }

        // popups: time left, as a hairline along the bottom (pauses while hovered)
        Rectangle {
            visible: card.popup && !card.critical && Config.notifications.progress
            anchors { left: parent.left; bottom: parent.bottom; leftMargin: card.r; bottomMargin: 0 }
            width: (parent.width - 2 * card.r) * card.remaining
            height: 2
            radius: 1
            color: Theme.alpha(Theme.accent, 0.8)
        }

        // click = the app's default action; right click = dismiss
        TapHandler {
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onTapped: (point, button) => {
                if (button === Qt.RightButton) { card.dismiss(); return }
                Notifs.activate(card.notif)
                card.done()
            }
        }
    }
}
