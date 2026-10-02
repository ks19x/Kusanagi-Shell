// TrayMenu.qml — a tray app's menu (Vesktop, Mullvad…) drawn by Kusanagi instead of Qt's native popup,
// which doesn't open reliably from a layer-shell bar. Reads the app's DBusMenu through QsMenuOpener:
// entries, separators, checkboxes / radio buttons, icons, and submenus (they expand in place).
// Opens under the icon (above it for a bottom bar); click outside or Esc closes.
import Quickshell
import Quickshell.Wayland
import Quickshell.Widgets
import QtQuick
import QtQuick.Effects

PanelWindow {
    id: root

    required property var trayItem            // SystemTrayItem
    required property real anchorX            // icon centre, screen x
    signal dismissed()

    property bool showing: true
    function close() { showing = false; closeTimer.restart() }
    Timer { id: closeTimer; interval: Config.ms(180); onTriggered: root.dismissed() }

    readonly property bool bottom: Config.bar.position === "bottom"

    anchors { top: true; left: true; right: true; bottom: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "quickshell-traymenu"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand

    // click anywhere else closes
    MouseArea { anchors.fill: parent; onClicked: root.close() }
    Item { focus: true; Keys.onEscapePressed: root.close() }

    QsMenuOpener { id: top; menu: root.trayItem ? root.trayItem.menu : null }

    RectangularShadow {
        visible: Config.look.shadows
        anchors.fill: card
        opacity: card.opacity
        scale: card.scale
        offset.y: 6
        blur: 22
        radius: card.radius
        color: Theme.alpha("#000000", 0.4)
    }

    Rectangle {
        id: card
        width: 240
        height: Math.min(list.implicitHeight + 12, root.height - 80)
        x: Math.max(8, Math.min(root.width - width - 8, root.anchorX - width / 2))
        y: root.bottom ? root.height - Config.bar.height - 6 - height : Config.bar.height + 6
        radius: Math.max(8, Config.look.radius - 4)
        color: Theme.alpha(Theme.bgPanel, Math.max(0.9, Config.panel.opacity))
        border.width: Theme.surfaceBorderWidth
        border.color: Theme.surfaceBorder
        clip: true

        // springs out of the bar
        transformOrigin: root.bottom ? Item.Bottom : Item.Top
        opacity: root.showing && shown ? 1 : 0
        scale: root.showing && shown ? 1 : 0.92
        property bool shown: false
        Component.onCompleted: shown = true
        Behavior on opacity { NumberAnimation { duration: Config.ms(160) } }
        Behavior on scale { NumberAnimation { duration: Config.ms(root.showing ? 300 : 160); easing.type: root.showing ? Easing.OutBack : Easing.InCubic; easing.overshoot: Config.bounce(1.6) } }

        MouseArea { anchors.fill: parent }       // clicks inside don't close it

        Flickable {
            anchors { fill: parent; margins: 6 }
            contentHeight: list.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: list
                width: parent.width
                Repeater { model: top.children; delegate: entryComp }
            }
        }
    }

    // one menu line; submenus open inline underneath
    Component {
        id: entryComp
        Column {
            id: entry
            required property var modelData
            property bool expanded: false
            readonly property var e: modelData
            width: parent ? parent.width - (parent.leftPadding || 0) : 228
            visible: e.visible !== false

            Rectangle {
                visible: entry.e.isSeparator
                width: parent.width; height: 9
                color: "transparent"
                Rectangle { anchors.centerIn: parent; width: parent.width - 16; height: 1; color: Theme.alpha(Theme.text, 0.08) }
            }

            Rectangle {
                visible: !entry.e.isSeparator
                width: parent.width
                height: 32
                radius: 8
                color: area.containsMouse && entry.e.enabled ? Theme.alpha(Theme.accent, 0.16) : "transparent"
                Behavior on color { ColorAnimation { duration: Config.ms(100) } }
                opacity: entry.e.enabled ? 1 : 0.4

                Row {
                    anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                    spacing: 10

                    // checkbox / radio, else the entry's icon (or a gap to keep text aligned)
                    Item {
                        width: 16; height: 16
                        anchors.verticalCenter: parent.verticalCenter
                        Rectangle {
                            visible: entry.e.buttonType !== QsMenuButtonType.None
                            anchors.fill: parent
                            radius: entry.e.buttonType === QsMenuButtonType.RadioButton ? 8 : 4
                            color: entry.e.checkState === Qt.Checked ? Theme.accent : "transparent"
                            border.width: entry.e.checkState === Qt.Checked ? 0 : 1.5
                            border.color: Theme.alpha(Theme.text, 0.4)
                            CpIcon {
                                anchors.centerIn: parent
                                visible: entry.e.checkState === Qt.Checked
                                cp: entry.e.buttonType === QsMenuButtonType.RadioButton ? 0xf0765 : 0xf012c
                                font.pixelSize: entry.e.buttonType === QsMenuButtonType.RadioButton ? 7 : 12
                                color: Theme.bgPanel
                            }
                        }
                        IconImage {
                            visible: entry.e.buttonType === QsMenuButtonType.None && entry.e.icon !== ""
                            anchors.fill: parent
                            source: entry.e.icon
                        }
                    }
                    CpText {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 228 - 10 - 16 - 10 - 30
                        text: entry.e.text.replace(/_(?!_)/g, "")      // strip mnemonic underscores
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
                CpIcon {
                    visible: entry.e.hasChildren
                    anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                    cp: 0xf0140
                    font.pixelSize: 14
                    color: Theme.textDim
                    rotation: entry.expanded ? 0 : -90
                    Behavior on rotation { NumberAnimation { duration: Config.ms(180); easing.type: Easing.OutCubic } }
                }

                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: entry.e.enabled
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (entry.e.hasChildren) { entry.expanded = !entry.expanded; return }
                        entry.e.triggered()
                        root.close()
                    }
                }
            }

            // submenu, indented
            Loader {
                active: entry.e.hasChildren && entry.expanded
                width: parent.width
                sourceComponent: Column {
                    width: parent ? parent.width : 228
                    leftPadding: 12
                    QsMenuOpener { id: sub; menu: entry.e }
                    Repeater { model: sub.children; delegate: entryComp }
                }
            }
        }
    }
}
