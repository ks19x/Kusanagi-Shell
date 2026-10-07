// ControlPanel.qml — drops down out of the bar's clock island: the island widens, then pours
// down into the panel, and the content fades in last. Closing plays it backwards, quicker.
// Tabs: Home (tiles, sliders, media, quick stats) · System · Inbox · Quick (the common settings).
// Loaded only while open (shell.qml LazyLoader); detailed stats run only while it's showing.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Widgets
import QtQuick
import QtQuick.Effects

PanelWindow {
    id: root

    property bool showing: false
    property int tab: 0
    property int lastTab: 0
    function toggle() { showing = !showing }
    function close() { showing = false }
    // run something after the panel is out of the way (screenshots, pickers…)
    function runClosed(cmd) { showing = false; pending = cmd; runLater.restart() }
    property var pending: null
    Timer { id: runLater; interval: Config.ms(300); onTriggered: if (root.pending) Quickshell.execDetached(root.pending) }

    anchors { top: true; left: true; right: true; bottom: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "quickshell-panel"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: showing ? WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.None
    visible: showing || open > 0
    mask: Region { item: root.showing ? backdrop : null }

    property bool tabPicked: false     // openTab() chose one; otherwise open on the default tab
    function openTab(i) { tab = i; tabPicked = true; showing = true }
    onShowingChanged: {
        if (showing && !tabPicked) tab = Config.panel.defaultTab
        tabPicked = false
        if (showing) origin = shellRef && shellRef.barItem ? shellRef.barItem.clockRect() : null
        SysInfo.panelOpen = showing
        closeAnim.stop(); openAnim.stop()
        if (showing) openAnim.start(); else closeAnim.start()
    }
    Component.onCompleted: SysInfo.panelOpen = showing
    Component.onDestruction: SysInfo.panelOpen = false
    onTabChanged: Qt.callLater(() => lastTab = tab)
    // display order of the tabs (values stay put so panel.defaultTab and IPC keep their meaning)
    readonly property var tabOrder: [0, 4, 5, 1, 2, 3]

    // ---- the morph ----
    property real open: 0
    NumberAnimation {
        id: openAnim
        target: root; property: "open"; to: 1
        duration: Config.ms(560)
        easing.type: Easing.BezierSpline
        easing.bezierCurve: [0.05, 0.7, 0.1, 1.0, 1, 1]      // emphasized decelerate
    }
    NumberAnimation {
        id: closeAnim
        target: root; property: "open"; to: 0
        duration: Config.ms(300)
        easing.type: Easing.BezierSpline
        easing.bezierCurve: [0.3, 0.0, 0.8, 0.15, 1, 1]      // emphasized accelerate
    }

    function lerp(a, b, t) { return a + (b - a) * t }
    function clamp01(v) { return Math.max(0, Math.min(1, v)) }

    // starts as the bar's clock island (22px tall at y=4), ends as the panel
    readonly property real targetW: Config.panel.width
    readonly property real wP: clamp01(open * 1.5)                 // widen first…
    readonly property real hP: clamp01((open - 0.08) / 0.92)       // …then pour down
    readonly property real contentP: clamp01((open - 0.42) / 0.58) // content arrives last

    Rectangle {
        id: backdrop
        anchors.fill: parent
        color: Theme.alpha("#000000", Config.look.backdrop * 0.75 * root.open)
        focus: root.showing
        Keys.onEscapePressed: root.close()
        Keys.onTabPressed: root.tab = root.tabOrder[(root.tabOrder.indexOf(root.tab) + 1) % root.tabOrder.length]
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    RectangularShadow {

        visible: Config.look.shadows
        anchors.fill: panel
        offset.y: 10
        blur: 36
        radius: panel.radius
        color: Theme.alpha("#000000", 0.45 * root.open)
    }

    // where the bar's clock island is on screen ({ x, y, w, h, edge }, taken when it opens; null = no clock)
    property var origin: null
    readonly property string barEdge: origin ? origin.edge : BarSpec.edge
    readonly property bool fromBottom: barEdge === "bottom"
    // Config.panel.morph: island = grows out of the clock · drop = full width, slides down · fade = fades + settles
    // (a clock on a side bar, or no clock at all, can't be grown out of: fade there)
    readonly property string morph: Config.panel.morph === "island" && (!origin || barEdge === "left" || barEdge === "right") ? "fade" : Config.panel.morph
    readonly property real originX: morph !== "island" ? targetX : origin.x
    readonly property real originW: morph !== "island" ? targetW : origin.w
    readonly property real originH: morph === "drop" ? 0 : morph === "fade" ? body.implicitHeight + 40 : origin.h
    readonly property real edge: !origin || barEdge === "left" || barEdge === "right" ? 4 : fromBottom ? height - origin.y - origin.h : origin.y
    // final spot: under the clock — centred, or hugging its side when it sits in the outer thirds
    readonly property real targetX: {
        const lo = barEdge === "left" ? BarSpec.thickness + 6 : 6, hi = width - (barEdge === "right" ? BarSpec.thickness + 6 : 6)
        if (!origin || barEdge === "left" || barEdge === "right") return Math.round((lo + hi - targetW) / 2)
        const c = origin.x + origin.w / 2
        return c < width / 3 ? Math.max(lo, origin.x) : c > width * 2 / 3 ? Math.min(hi - targetW, origin.x + origin.w - targetW) : Math.round((width - targetW) / 2)
    }

    Rectangle {
        id: panel
        width: Math.round(root.lerp(root.originW, root.targetW, root.wP))
        height: Math.round(root.lerp(root.originH, body.implicitHeight + 40, root.hP))
        x: Math.round(root.lerp(root.originX, root.targetX, root.wP))
        y: root.fromBottom ? root.height - root.edge - height : root.edge
        opacity: root.morph === "fade" ? Math.min(1, root.open * 1.4) : 1
        scale: root.morph === "fade" ? 0.95 + 0.05 * root.open : 1
        transformOrigin: root.fromBottom ? Item.Bottom : Item.Top
        radius: root.lerp(Config.bar.radius, Config.look.radius, root.wP)
        color: Theme.alpha(Theme.bgPanel, root.lerp(Math.max(Config.bar.opacity, 0.5), Config.panel.opacity, root.wP))
        border.width: Theme.surfaceBorderWidth
        border.color: Theme.alpha(Theme.surfaceBorder, Theme.surfaceBorder.a * root.open)
        clip: true

        MouseArea { anchors.fill: parent }   // clicks inside don't close it

        Column {
            id: body
            x: 20
            y: 20 - 10 * (1 - root.contentP)
            width: root.targetW - 40
            spacing: 16
            opacity: root.contentP

            // ---------- header: time + date, you, actions ----------
            Item {
                width: parent.width
                height: 48

                SystemClock { id: clock; precision: SystemClock.Minutes }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    CpText {
                        text: Qt.formatTime(clock.date, Config.bar.clock.includes("AP") ? "h:mm AP" : "HH:mm")
                        font.pixelSize: 28
                        font.bold: true
                    }
                    CpText {
                        text: Qt.formatDate(clock.date, "dddd, d MMMM")
                        font.pixelSize: 11
                        color: Theme.textDim
                    }
                }

                Row {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    spacing: 4

                    ClippingRectangle {
                        width: 34; height: 34
                        radius: 17
                        color: Theme.alpha(Theme.text, 0.08)
                        anchors.verticalCenter: parent.verticalCenter
                        Image {
                            anchors.fill: parent
                            source: "file://" + Quickshell.env("HOME") + "/.face"
                            fillMode: Image.PreserveAspectCrop
                            sourceSize: Qt.size(68, 68)
                            asynchronous: true
                        }
                    }
                    Item { width: 6; height: 1 }
                    CpIconButton {
                        icon: 0xf0493
                        onClicked: { root.close(); root.shellRef.openSettings() }
                    }
                    CpIconButton {
                        icon: 0xf033e
                        onClicked: root.runClosed(["kusanagi", "msg", "lock", "lock"])
                    }
                    CpIconButton {
                        icon: 0xf0425
                        onClicked: { root.close(); root.shellRef.openPower() }
                    }
                }
            }

            CpSegmented {
                width: parent.width
                height: 34
                current: root.tab
                fontSize: 10
                options: [
                    { label: "Home", value: 0, icon: 0xf02dc },
                    { label: "Sound", value: 4, icon: 0xf057e },
                    { label: "Net", value: 5, icon: 0xf06f3 },
                    { label: "System", value: 1, icon: 0xf012a },
                    { label: Notifs.count ? "Inbox " + Notifs.count : "Inbox", value: 2, icon: 0xf009a },
                    { label: "Quick", value: 3, icon: 0xf0493 }
                ]
                onPicked: v => root.tab = v
            }

            // ---------- page ----------
            Item {
                id: pageBox
                width: parent.width
                height: page.item ? page.item.implicitHeight : 0
                Behavior on height {
                    enabled: root.open === 1
                    NumberAnimation { duration: Config.ms(380); easing.type: Easing.OutQuint }
                }

                Loader {
                    id: page
                    width: parent.width
                    // the page only exists while the panel is on screen (the window itself stays ready)
                    active: root.visible
                    sourceComponent: [home, system, inbox, customize, sound, network][root.tab]
                    onLoaded: {
                        slideIn.from = root.tabOrder.indexOf(root.tab) >= root.tabOrder.indexOf(root.lastTab) ? 28 : -28
                        if (root.tab === root.lastTab) slideIn.from = 0
                        pageIn.restart()
                    }
                    ParallelAnimation {
                        id: pageIn
                        NumberAnimation { id: slideIn; target: page; property: "x"; to: 0; duration: Config.ms(380); easing.type: Easing.OutQuint }
                        NumberAnimation { target: page; property: "opacity"; from: 0; to: 1; duration: Config.ms(260); easing.type: Easing.OutCubic }
                    }
                }
            }
        }
    }

    // ---------- pages ----------
    property var shellRef: null

    Component { id: home; CpHome { panel: root } }
    Component { id: system; CpSystem {} }
    Component { id: inbox; CpInbox {} }
    Component { id: customize; CpCustomize { panel: root } }
    Component { id: sound; CpSound { panel: root } }
    Component { id: network; CpNetwork { panel: root } }
}
