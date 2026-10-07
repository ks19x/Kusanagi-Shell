// WidgetItem.qml — one desktop widget: placed by its centre (spec.x / spec.y, fractions of the
// screen), sized by spec.scale, on a card or straight on the wallpaper (with a soft shadow so it
// stays readable). The content is Wdg<Type>.qml, loaded only for the widgets you have.
// Arrange mode: drag it; the new spot is saved when you let go.
import Quickshell
import QtQuick
import QtQuick.Effects

Item {
    id: wi

    required property int idx
    required property var spec
    required property var host          // Widgets.qml
    required property var win           // its window (click regions)

    readonly property string type: spec.type || "clock"
    readonly property real k: Math.max(0.4, Math.min(3, spec.scale ?? 1))
    readonly property bool card: spec.card === true
    readonly property real pad: card ? 22 : 0

    // a widget with nothing to show (no player, no weather yet) steps out entirely
    visible: !content.item || content.item.shown !== false
    width: Math.round((content.width + 2 * pad) * k)
    height: Math.round((content.height + 2 * pad) * k)
    // where the spec says; a drag moves it freely, letting go saves the spot and binds it again
    readonly property bool dragging: drag.active
    function place() {
        x = Qt.binding(() => Math.round((spec.x ?? 0.5) * parent.width - width / 2))
        y = Qt.binding(() => Math.round((spec.y ?? 0.5) * parent.height - height / 2))
    }

    // its part of the window's click mask
    property Region region: Region { item: wi }
    Component.onCompleted: { place(); win.addRegion(region) }
    Component.onDestruction: win.dropRegion(region)

    Item {
        id: body
        width: content.width + 2 * wi.pad
        height: content.height + 2 * wi.pad
        scale: wi.k
        transformOrigin: Item.TopLeft

        Rectangle {
            visible: wi.card
            anchors.fill: parent
            radius: Math.max(14, Config.look.radius + 4)
            color: Theme.alpha(Theme.bgPanel, Math.max(0.55, Config.panel.opacity - 0.25))
            border.width: Theme.surfaceBorderWidth
            border.color: Theme.surfaceBorder
        }

        Loader {
            id: content
            x: wi.pad; y: wi.pad
            source: "Wdg" + wi.type.charAt(0).toUpperCase() + wi.type.slice(1) + ".qml"
            onLoaded: item.w = wi
            // on the wallpaper: a soft shadow keeps light text readable on light pictures
            layer.enabled: !wi.card && status === Loader.Ready
            layer.effect: MultiEffect { shadowEnabled: true; shadowBlur: 0.7; shadowOpacity: 0.55; shadowVerticalOffset: 2 }
        }
    }

    // arrange mode: an outline, and the item follows the pointer
    Rectangle {
        visible: wi.host.arranging
        anchors.fill: parent
        anchors.margins: -6
        radius: 14
        color: Theme.alpha(Theme.accent, dragHov.hovered || wi.dragging ? 0.12 : 0.04)
        border.width: 2
        border.color: Theme.alpha(Theme.accent, wi.dragging ? 1 : 0.6)
        HoverHandler { id: dragHov; cursorShape: Qt.SizeAllCursor }
    }
    DragHandler {
        id: drag
        enabled: wi.host.arranging
        onActiveChanged: if (!active) {
            wi.host.setItem(wi.idx, { x: Math.round((wi.x + wi.width / 2) / wi.parent.width * 1000) / 1000,
                                       y: Math.round((wi.y + wi.height / 2) / wi.parent.height * 1000) / 1000 })
            wi.place()
        }
    }
}
