// ScreenshotOsd.qml — after a screenshot: a preview card in a corner (Config.screenshot.position) with
// Copy · Edit (swappy) · Folder · Delete. Click the picture to open it, swipe it away, hover pauses the
// countdown. Called by the screenshot script: `kusanagi msg screenshot notify <file>`.
// Unmapped and the preview freed while idle.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Widgets
import QtQuick
import QtQuick.Effects

PanelWindow {
    id: root

    readonly property string pos: Config.screenshot.position
    readonly property bool onRight: pos.endsWith("right")
    readonly property bool onBottom: pos.startsWith("bottom")

    anchors { top: !onBottom; bottom: onBottom; left: !onRight; right: onRight }
    margins { top: Config.bar.position === "top" ? 6 : 14; bottom: Config.bar.position === "bottom" ? 6 : 14; left: 14; right: 14 }
    implicitWidth: 360
    implicitHeight: 300
    color: "transparent"
    exclusionMode: ExclusionMode.Normal
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.namespace: "quickshell-screenshot"
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
    visible: showing || card.opacity > 0.01
    mask: Region { item: root.showing ? card : null }

    property bool showing: false
    property string file: ""
    readonly property string name: file.split("/").pop()
    readonly property string dir: file.substring(0, file.lastIndexOf("/"))

    function show(path) {
        file = path
        card.x = 0
        countdown.restart()
        showing = true
    }
    function hide() { showing = false; countdown.stop() }
    function run(cmd) { Quickshell.execDetached(cmd) }

    property real remaining: 1
    NumberAnimation on remaining {
        id: countdown
        running: false
        from: 1; to: 0
        duration: Config.screenshot.timeout
        paused: running && hover.hovered
        onFinished: root.hide()
    }

    // free the preview once it's gone
    onVisibleChanged: if (!visible) file = ""

    RectangularShadow {
        visible: Config.look.shadows
        anchors.fill: card
        opacity: card.opacity
        scale: card.scale
        offset.y: 8
        blur: 26
        radius: card.radius
        color: Theme.alpha("#000000", 0.42)
    }

    Rectangle {
        id: card
        width: 340
        height: 268
        y: root.onBottom ? parent.height - height - 4 : 4
        radius: Math.max(10, Config.look.radius - 2)
        color: Theme.alpha(Theme.bgPanel, Math.max(0.9, Config.panel.opacity))
        border.width: Theme.surfaceBorderWidth
        border.color: Theme.surfaceBorder

        // springs in from its own edge
        transformOrigin: root.onBottom ? (root.onRight ? Item.BottomRight : Item.BottomLeft) : (root.onRight ? Item.TopRight : Item.TopLeft)
        opacity: root.showing ? 1 : 0
        scale: root.showing ? 1 : 0.86
        Behavior on opacity { NumberAnimation { duration: Config.ms(root.showing ? 200 : 160) } }
        Behavior on scale { NumberAnimation { duration: Config.ms(root.showing ? 460 : 180); easing.type: root.showing ? Easing.OutBack : Easing.InCubic; easing.overshoot: Config.bounce(1.3) } }
        Behavior on x { enabled: !drag.active; NumberAnimation { duration: Config.ms(320); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(1.2) } }

        HoverHandler { id: hover }
        // swipe toward the screen edge to dismiss
        DragHandler {
            id: drag
            target: card
            yAxis.enabled: false
            onActiveChanged: if (!active) {
                if (Math.abs(card.x) > card.width * 0.3) { card.x = (root.onRight ? 1 : -1) * (card.width + 40); root.hide() }
                else card.x = 0
            }
        }

        // ---- preview ----
        ClippingRectangle {
            id: shot
            x: 10; y: 10
            width: parent.width - 20
            height: 172
            radius: Math.max(6, card.radius - 6)
            color: Theme.alpha(Theme.text, 0.06)
            Image {
                anchors.fill: parent
                source: root.file ? "file://" + root.file : ""
                sourceSize: Qt.size(640, 344)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: { root.run(["xdg-open", root.file]); root.hide() }
            }
        }

        CpText {
            anchors { left: shot.left; right: shot.right; top: shot.bottom; topMargin: 8 }
            text: root.name
            font.pixelSize: 11
            color: Theme.textDim
            elide: Text.ElideMiddle
        }

        // ---- actions ----
        Row {
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 12 }
            spacing: 6
            CpChip { label: "Copy"; icon: 0xf018f; onClicked: { root.run(["sh", "-c", "wl-copy --type image/png < \"$1\"", "sh", root.file]); root.hide() } }
            CpChip { label: "Edit"; icon: 0xf03eb; onClicked: { root.run(["sh", "-c", Config.screenshot.editor + " \"$1\"", "sh", root.file]); root.hide() } }
            CpChip { label: "Folder"; icon: 0xf024b; onClicked: { root.run(["xdg-open", root.dir]); root.hide() } }
            CpChip { label: "Delete"; icon: 0xf0a7a; onClicked: { root.run(["rm", "-f", root.file]); root.hide() } }
        }

        // time left
        Rectangle {
            anchors { left: parent.left; bottom: parent.bottom; leftMargin: card.radius }
            width: (parent.width - 2 * card.radius) * root.remaining
            height: 2; radius: 1
            color: Theme.alpha(Theme.accent, 0.8)
        }
    }

    IpcHandler {
        target: "screenshot"
        function notify(path: string): void { root.show(path) }
    }
}
