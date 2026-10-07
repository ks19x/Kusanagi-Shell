// Settings.qml — Kusanagi settings (Super+I): a real window (floats on Mango/Hyprland), sidebar on the
// left, one page per topic on the right. Every control writes Config, so changes apply as you make them.
// Loaded only while open (shell.qml).
import Quickshell
import QtQuick

FloatingWindow {
    id: root

    property bool showing: false
    property string page: "presets"
    function show(p) { if (p) page = p; showing = true }
    function hide() { showing = false }
    function toggle() { showing = !showing }

    title: "Kusanagi Settings"
    implicitWidth: 1080
    implicitHeight: 720
    minimumSize: Qt.size(820, 520)
    color: Theme.alpha(Theme.bgPanel, Math.max(0.9, Config.panel.opacity))
    visible: showing
    onVisibleChanged: if (!visible) showing = false      // closed by the compositor

    readonly property var pages: [
        { group: "PERSONALIZE", id: "presets", name: "Presets", icon: 0xf0e09, page: "SpPresets", keys: "looks themes minimal dwl glass zen terminal save" },
        { id: "appearance", name: "Appearance", icon: 0xf03d8, page: "SpAppearance", keys: "theme colour accent font corners shadow animation speed" },
        { id: "bar", name: "Bar", icon: 0xf04e9, page: "SpBar", keys: "style islands solid floating modules clock scroll media marquee" },
        { id: "workspaces", name: "Workspaces", icon: 0xf0570, page: "SpWorkspaces", keys: "tags icons pills dots roman kanji glow" },
        { id: "panel", name: "Control panel", icon: 0xf056e, page: "SpPanel", keys: "tiles width tab media stats" },
        { id: "wallpaper", name: "Wallpaper", icon: 0xf0e09, page: "SpWallpaper", keys: "transition parallax slideshow fill dim awww picker" },
        { id: "launcher", name: "Launcher & clipboard", icon: 0xf003b, page: "SpLauncher", keys: "apps search clipboard terminal calculator" },
        { id: "lock", name: "Lock screen", icon: 0xf033e, page: "SpLock", keys: "hyprlock blur password test" },
        { id: "notifications", name: "Notifications & OSD", icon: 0xf009a, page: "SpNotifications", keys: "popups dnd osd volume timeout position" },
        { id: "gamemode", name: "Game mode", icon: 0xf0297, page: "SpGameMode", keys: "games fullscreen performance governor feral blur" },
        { group: "SYSTEM", id: "sound", name: "Sound", icon: 0xf057e, page: "SpSound", keys: "audio volume output input microphone apps" },
        { id: "display", name: "Display", icon: 0xf0379, page: "SpDisplay", keys: "monitor gaps borders windows night light gammastep resolution" },
        { id: "network", name: "Network", icon: 0xf06f3, page: "SpNetwork", keys: "ethernet wifi ip speed" },
        { id: "storage", name: "Storage", icon: 0xf02ca, page: "SpStorage", keys: "disk xbps cache cleanup orphans" },
        { id: "about", name: "About", icon: 0xf02fd, page: "SpAbout", keys: "system kusanagi version memory" }
    ]
    readonly property var current: pages.find(p => p.id === page) ?? pages[0]
    readonly property var shownPages: {
        const q = search.text.trim().toLowerCase()
        return q ? pages.filter(p => (p.name + " " + p.keys).toLowerCase().includes(q)) : pages
    }

    Item {
        anchors.fill: parent
        focus: true
        Keys.onEscapePressed: root.hide()

        // ---------------- sidebar ----------------
        Rectangle {
            id: sidebar
            width: 250
            height: parent.height
            color: Theme.alpha(Theme.text, 0.03)

            Column {
                x: 22; y: 26
                spacing: 2
                Row {
                    spacing: 12
                    Image {
                        width: 38; height: 38
                        anchors.verticalCenter: parent.verticalCenter
                        source: Qt.resolvedUrl("logo.svg")
                        sourceSize: Qt.size(76, 76)
                        smooth: true
                        mipmap: true
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        CpText { text: "KUSANAGI"; font.pixelSize: 15; font.bold: true; font.letterSpacing: 4 }
                        CpText { text: "settings"; font.pixelSize: 10; color: Theme.textDim; font.letterSpacing: 1 }
                    }
                }
            }

            CpField {
                id: search
                x: 16; y: 84
                width: parent.width - 32
                icon: 0xf0349
                placeholder: "Search settings"
                onAccepted: if (root.shownPages.length) root.page = root.shownPages[0].id
            }

            ListView {
                id: nav
                anchors { top: search.bottom; topMargin: 12; left: parent.left; right: parent.right; bottom: parent.bottom; margins: 10; bottomMargin: 14 }
                clip: true
                model: root.shownPages
                spacing: 2
                currentIndex: root.shownPages.findIndex(p => p.id === root.page)
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: Config.ms(220)
                highlightMoveVelocity: -1
                highlightResizeDuration: Config.ms(220)
                highlight: Item {
                    Rectangle {
                        anchors { fill: parent; topMargin: nav.currentItem && nav.currentItem.hasGroup ? 26 : 0 }
                        radius: 10
                        color: Theme.alpha(Theme.accent, 0.16)
                        Rectangle { x: 0; width: 3; radius: 1.5; height: parent.height - 16; anchors.verticalCenter: parent.verticalCenter; color: Theme.accent }
                    }
                }

                delegate: Item {
                    id: item
                    required property var modelData
                    required property int index
                    readonly property bool hasGroup: !!modelData.group && !search.text
                    readonly property bool active: modelData.id === root.page
                    width: nav.width
                    height: 38 + (hasGroup ? 26 : 0)

                    CpText {
                        visible: item.hasGroup
                        x: 12; y: item.index === 0 ? 4 : 8
                        text: item.modelData.group ?? ""
                        font.pixelSize: 9
                        font.bold: true
                        font.letterSpacing: 2
                        color: Theme.textDim
                    }
                    Row {
                        x: 14
                        y: (item.hasGroup ? 26 : 0) + 10
                        spacing: 12
                        CpIcon {
                            cp: item.modelData.icon
                            font.pixelSize: 16
                            color: item.active ? Theme.accent : (area.containsMouse ? Theme.text : Theme.textDim)
                            Behavior on color { ColorAnimation { duration: Config.ms(160) } }
                        }
                        CpText {
                            text: item.modelData.name
                            font.pixelSize: 12
                            font.bold: item.active
                            color: item.active || area.containsMouse ? Theme.text : Theme.alpha(Theme.text, 0.75)
                        }
                    }
                    MouseArea {
                        id: area
                        anchors { fill: parent; topMargin: item.hasGroup ? 26 : 0 }
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.page = item.modelData.id
                    }
                }
            }
        }

        Rectangle { x: sidebar.width; width: 1; height: parent.height; color: Theme.alpha(Theme.text, 0.06) }

        // ---------------- page ----------------
        Item {
            id: main
            anchors { left: sidebar.right; leftMargin: 1; right: parent.right; top: parent.top; bottom: parent.bottom }

            Column {
                id: heading
                x: 36; y: 28
                spacing: 4
                CpText { text: root.current.name; font.pixelSize: 24; font.bold: true }
                CpText { text: root.current.keys.split(" ").slice(0, 4).join(" · "); font.pixelSize: 11; color: Theme.textDim }
            }

            Flickable {
                id: scroller
                anchors { top: heading.bottom; topMargin: 18; left: parent.left; right: parent.right; bottom: parent.bottom }
                contentHeight: loader.height + 40
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Loader {
                    id: loader
                    x: 36
                    width: Math.min(760, scroller.width - 72)
                    source: root.current.page + ".qml"
                    onLoaded: { scroller.contentY = 0; pageIn.restart() }
                    ParallelAnimation {
                        id: pageIn
                        NumberAnimation { target: loader; property: "opacity"; from: 0; to: 1; duration: Config.ms(240); easing.type: Easing.OutCubic }
                        NumberAnimation { target: loader; property: "y"; from: 14; to: 0; duration: Config.ms(340); easing.type: Easing.OutQuint }
                    }
                }
            }

            // slim scroll indicator
            Rectangle {
                visible: scroller.contentHeight > scroller.height
                x: parent.width - 6
                y: scroller.y + scroller.visibleArea.yPosition * scroller.height
                width: 3; radius: 1.5
                height: scroller.visibleArea.heightRatio * scroller.height
                color: Theme.alpha(Theme.text, 0.2)
            }
        }
    }
}
