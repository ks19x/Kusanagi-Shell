// Wallpaper.qml — Kusanagi draws the wallpaper itself (replaces awww) on every monitor.
// Follows ~/.config/kusanagi/wallpaper (written by `kusanagi wallpaper <file>`, which also re-colours
// everything), so picking one anywhere animates to it here. Config.wallpaper: transition (fade | blur | wipe |
// grow | slide | zoom | blinds | random), duration, fill, parallax on workspace change, dim, slideshow.
//
// Two slots take turns: the new picture decodes into the free slot, is revealed over the old one,
// and then simply stays — nothing reloads at the end, so there's no flash. Once a picture is fully
// shown it's kept as a GPU snapshot and its decoded copy in RAM is dropped (~16 MB at 1440p).
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Widgets
import QtQuick
import QtQuick.Effects
import Qt.labs.folderlistmodel

Scope {
    id: root

    readonly property bool enabled: Config.wallpaper.renderer === "kusanagi"

    FileView {
        id: state
        path: Quickshell.env("HOME") + "/.config/kusanagi/wallpaper"
        watchChanges: true
        onFileChanged: reload()
        printErrors: false
    }
    readonly property string current: state.loaded ? state.text().trim() : ""

    // awww handed over: once we've drawn, stop it (it'd sit underneath otherwise)
    property bool tookOver: false
    onEnabledChanged: tookOver = false     // switching back from awww: take over again
    function takeOver() {
        if (tookOver || !enabled) return
        tookOver = true
        Quickshell.execDetached(["sh", "-c", "pkill -x awww-daemon; pkill -x swaybg"])
    }

    readonly property int wsIndex: Wm.activeIndex     // parallax pans with it

    // ---- slideshow: a random wallpaper every N minutes, re-themed like a manual pick ----
    FolderListModel {
        id: folder
        folder: "file://" + Config.wallpaper.folder.replace(/^~/, Quickshell.env("HOME"))
        nameFilters: ["*.jpg", "*.jpeg", "*.png", "*.webp", "*.JPG", "*.PNG"]
        showDirs: false
    }
    Timer {
        interval: Math.max(1, Config.wallpaper.slideshow) * 60000
        running: root.enabled && Config.wallpaper.slideshow > 0 && folder.count > 1 && !GameMode.quiet
        repeat: true
        onTriggered: {
            let f = root.current
            while (f === root.current) f = folder.get(Math.floor(Math.random() * folder.count), "filePath")
            Quickshell.execDetached(["kusanagi", "wallpaper", f])
        }
    }

    Variants {
        model: root.enabled ? Quickshell.screens : []

        PanelWindow {
            id: win
            required property var modelData
            screen: modelData

            anchors { top: true; bottom: true; left: true; right: true }
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.layer: WlrLayer.Background
            WlrLayershell.namespace: "kusanagi-wallpaper"
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
            color: Theme.bgPanel
            mask: Region {}

            readonly property real par: Config.wallpaper.parallax
            readonly property size decode: Qt.size(Math.round(width * (1 + par)), Math.round(height * (1 + par)))
            readonly property int fill: ({ fill: Image.PreserveAspectCrop, fit: Image.PreserveAspectFit, stretch: Image.Stretch,
                                           center: Image.Pad, tile: Image.Tile })[Config.wallpaper.fill] ?? Image.PreserveAspectCrop

            // ---- slots ----
            property int top: 0                // slot that holds (or is revealing) the newest picture
            property bool busy: false          // a transition is running
            property string kind: "fade"
            property real p: 1                 // transition progress 0 → 1
            property int dir: 0                // wipe direction 0 → 1 ↓ 2 ← 3 ↑
            readonly property Item incoming: top === 0 ? slotA : slotB
            readonly property Item outgoing: top === 0 ? slotB : slotA

            readonly property string target: root.current
            onTargetChanged: begin()
            Component.onCompleted: begin()
            // decode size / fit changed: reload what's shown
            onDecodeChanged: incoming.reload()
            onFillChanged: incoming.reload()

            function begin() {
                if (!target || target === incoming.file) return
                if (!incoming.file) { incoming.load(target); return }     // first picture: just show it
                const kinds = ["fade", "blur", "wipe", "grow", "slide", "zoom", "blinds"]
                kind = Config.wallpaper.transition === "random" ? kinds[Math.floor(Math.random() * kinds.length)] : Config.wallpaper.transition
                dir = Math.floor(Math.random() * 4)
                anim.stop()
                if (busy) outgoing.clear()                  // a pick during a transition: drop the oldest
                top = 1 - top
                p = 0
                busy = true
                incoming.load(target)                       // anim starts once it's decoded (slot.shown)
            }
            function finish() {
                busy = false
                p = 1
                outgoing.clear()
            }

            NumberAnimation {
                id: anim
                target: win; property: "p"; from: 0; to: 1
                duration: Config.look.animSpeed === 0 ? 1 : Config.wallpaper.duration
                easing.type: Easing.BezierSpline
                easing.bezierCurve: [0.65, 0.0, 0.35, 1.0, 1, 1]   // ease-in-out: calm start and landing
                onFinished: win.finish()
            }

            // one picture: decoded Image (hidden) → texture; frozen once shown so the RAM copy can go
            component Slot: Item {
                id: slot
                property string file: ""
                property bool frozen: false
                readonly property alias texture: tex
                signal shown()

                function load(f) { freeze.stop(); frozen = false; file = f }
                function reload() { if (file) { freeze.stop(); frozen = false } }
                function clear() { freeze.stop(); frozen = false; file = "" }

                width: stage.width; height: stage.height

                Image {
                    id: img
                    visible: false
                    anchors.fill: parent
                    source: slot.file && !slot.frozen ? "file://" + slot.file : ""
                    sourceSize: win.decode
                    fillMode: win.fill
                    asynchronous: true
                    cache: false
                    onStatusChanged: {
                        if (status === Image.Ready) { root.takeOver(); slot.shown() }
                        else if (status === Image.Error && slot === win.incoming && win.busy) win.finish()
                    }
                }
                ShaderEffectSource {
                    id: tex
                    anchors.fill: parent
                    sourceItem: img
                    live: !slot.frozen
                }
                // a moment after it's fully on screen, keep only the GPU texture
                Timer {
                    id: freeze
                    interval: 1500
                    onTriggered: {
                        if (win.busy && slot === win.incoming) { restart(); return }
                        if (img.status === Image.Ready) slot.frozen = true
                    }
                }
                onShown: {
                    if (slot === win.incoming && win.busy) anim.restart()
                    freeze.restart()
                }
            }

            // parallax: the picture is (1 + par) wide and pans as you move across workspaces
            Item {
                id: stage
                width: win.width * (1 + win.par)
                height: win.height * (1 + win.par)
                y: -win.height * win.par / 2
                x: -win.width * win.par * (Math.min(9, root.wsIndex) - 1) / 8
                Behavior on x { enabled: win.par > 0; NumberAnimation { duration: Config.ms(700); easing.type: Easing.OutQuint } }

                // the outgoing slot is the backdrop; the incoming one is revealed over it
                Slot {
                    id: slotA
                    z: win.top === 0 ? 1 : 0
                    readonly property bool inc: win.top === 0
                    opacity: !inc || !win.busy ? 1 : win.kind === "fade" || win.kind === "zoom" ? win.p : win.kind === "slide" ? 1 : 0
                    scale: inc && win.busy && win.kind === "zoom" ? 1.12 - 0.12 * win.p : 1
                    x: !win.busy || win.kind !== "slide" ? 0 : inc ? win.width * (1 - win.p) : -win.width * 0.25 * win.p
                }
                Slot {
                    id: slotB
                    z: win.top === 1 ? 1 : 0
                    readonly property bool inc: win.top === 1
                    opacity: !inc || !win.busy ? 1 : win.kind === "fade" || win.kind === "zoom" ? win.p : win.kind === "slide" ? 1 : 0
                    scale: inc && win.busy && win.kind === "zoom" ? 1.12 - 0.12 * win.p : 1
                    x: !win.busy || win.kind !== "slide" ? 0 : inc ? win.width * (1 - win.p) : -win.width * 0.25 * win.p
                }

                // blur: comes into focus while fading in
                MultiEffect {
                    z: 2
                    visible: win.busy && win.kind === "blur"
                    anchors.fill: parent
                    source: win.incoming.texture
                    blurEnabled: true
                    blurMax: 64
                    blur: 1 - win.p
                    opacity: Math.min(1, win.p * 1.6)
                }

                // wipe: a hard edge sweeping across in one of four directions
                Item {
                    z: 2
                    visible: win.busy && win.kind === "wipe"
                    clip: true
                    readonly property real e: win.p
                    x: win.dir === 2 ? stage.width * (1 - e) : 0
                    y: win.dir === 3 ? stage.height * (1 - e) : 0
                    width: win.dir === 0 || win.dir === 2 ? stage.width * e : stage.width
                    height: win.dir === 1 || win.dir === 3 ? stage.height * e : stage.height
                    ShaderEffectSource { x: -parent.x; y: -parent.y; width: stage.width; height: stage.height; sourceItem: win.incoming.texture }
                }

                // grow: a circle opening from the middle
                ClippingRectangle {
                    z: 2
                    visible: win.busy && win.kind === "grow"
                    readonly property real d: Math.hypot(stage.width, stage.height) * win.p
                    width: d; height: d; radius: d / 2
                    x: (stage.width - d) / 2; y: (stage.height - d) / 2
                    color: "transparent"
                    ShaderEffectSource { x: -parent.x; y: -parent.y; width: stage.width; height: stage.height; sourceItem: win.incoming.texture }
                }

                // blinds: twelve slats turning one after another
                Repeater {
                    model: win.busy && win.kind === "blinds" ? 12 : 0
                    Item {
                        required property int index
                        z: 2
                        readonly property real slat: stage.width / 12
                        readonly property real e: Math.max(0, Math.min(1, win.p * 1.7 - index * 0.06))
                        clip: true
                        x: index * slat
                        width: slat * e
                        height: stage.height
                        ShaderEffectSource { x: -parent.x; width: stage.width; height: stage.height; sourceItem: win.incoming.texture }
                    }
                }
            }

            // optional dim layer
            Rectangle { anchors.fill: parent; color: "black"; opacity: Config.wallpaper.dim; visible: opacity > 0 }
        }
    }
}
