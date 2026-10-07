// Widgets.qml — desktop widgets (Config.widgets): a click-through layer between the wallpaper and
// your windows. With "only on an empty desktop" they fade in when the focused workspace has no
// windows and fade out (then unload: no windows, no timers, no memory) when one opens.
// Arrange mode (`kusanagi msg widgets arrange`, Settings → Desktop widgets): drag them around.
import Quickshell
import Quickshell.Wayland
import QtQuick

Scope {
    id: root

    property bool arranging: false
    readonly property bool emptyDesktop: Wm.desktopEmpty
    readonly property bool wanted: Config.widgets.enabled && (arranging || (!GameMode.active && (!Config.widgets.onlyDesktop || emptyDesktop)))

    // stay loaded through the fade-out, then let go of everything
    property bool loaded: wanted
    onWantedChanged: if (wanted) { unload.stop(); loaded = true } else unload.restart()
    Timer { id: unload; interval: (Config.widgets.fade ? Config.ms(420) : 0) + 150; onTriggered: if (!root.wanted) root.loaded = false }

    function items() { try { return JSON.parse(JSON.stringify(Config.widgets.items || [])) } catch (e) { return [] } }
    function setItem(i, patch) { const l = items(); if (!l[i]) return; Object.assign(l[i], patch); Config.widgets.items = l }

    Variants {
        model: root.loaded ? Quickshell.screens : []
        PanelWindow {
            id: wnd     // not "win": WidgetItem has a property by that name
            required property var modelData
            screen: modelData
            anchors { top: true; bottom: true; left: true; right: true }
            color: "transparent"
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.namespace: "kusanagi-widgets"
            WlrLayershell.layer: root.arranging ? WlrLayer.Overlay : WlrLayer.Bottom
            WlrLayershell.keyboardFocus: root.arranging ? WlrKeyboardFocus.OnDemand : WlrKeyboardFocus.None
            // only the widgets take clicks (no mask while arranging: the whole screen is the stage)
            mask: root.arranging ? null : region
            Region { id: region; regions: wnd.regions }
            property var regions: []
            function addRegion(r) { regions = regions.concat([r]) }
            function dropRegion(r) { regions = regions.filter(x => x !== r) }

            // arrange mode: dim the desktop, say what to do
            Rectangle {
                anchors.fill: parent
                color: Theme.alpha("#000000", root.arranging ? 0.35 : 0)
                Behavior on color { ColorAnimation { duration: Config.ms(200) } }
                focus: root.arranging
                Keys.onEscapePressed: root.arranging = false
            }
            Rectangle {
                visible: root.arranging
                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 64 }
                width: hintRow.implicitWidth + 36; height: 44; radius: 22
                color: Theme.alpha(Theme.bgPanel, 0.92)
                border.width: 1; border.color: Theme.alpha(Theme.accent, 0.5)
                Row {
                    id: hintRow
                    anchors.centerIn: parent
                    spacing: 14
                    CpText { text: "Drag the widgets where you want them"; font.pixelSize: 12; anchors.verticalCenter: parent.verticalCenter }
                    CpChip { label: "Done"; icon: 0xf012c; on: true; onClicked: root.arranging = false }
                }
            }

            Item {
                id: stage
                anchors.fill: parent
                opacity: root.wanted ? 1 : 0
                scale: root.wanted ? 1 : 0.985
                Behavior on opacity { enabled: Config.widgets.fade; NumberAnimation { duration: Config.ms(420); easing.type: Easing.OutCubic } }
                Behavior on scale { enabled: Config.widgets.fade; NumberAnimation { duration: Config.ms(480); easing.type: Easing.OutCubic } }
                Repeater {
                    model: (Config.widgets.items || []).length
                    WidgetItem {
                        required property int index
                        idx: index
                        spec: Config.widgets.items[index] ?? ({})
                        host: root
                        win: wnd
                    }
                }
            }
        }
    }
}
