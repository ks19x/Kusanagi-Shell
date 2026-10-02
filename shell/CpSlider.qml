// CpSlider.qml — chunky pill slider (volume, mic, opacity…): drag, click or scroll.
// value is 0..1; `moved` fires live while dragging/scrolling. Click the icon for `iconClicked`.
import QtQuick

Item {
    id: root

    property real value: 0
    property int icon: 0
    property string label: ""
    property string valueText: Math.round(value * 100) + "%"
    property real step: 0.05
    property bool muted: false
    signal moved(real value)
    signal iconClicked()

    implicitHeight: 36

    readonly property real shown: drag.pressed ? drag.live : value
    function set(v) { root.moved(Math.max(0, Math.min(1, v))) }

    Rectangle {
        id: track
        anchors.fill: parent
        radius: height / 2
        color: Theme.alpha(Theme.text, 0.07)

        Rectangle {
            id: fill
            height: parent.height
            radius: height / 2
            width: Math.max(height, parent.width * root.shown)
            color: root.muted ? Theme.alpha(Theme.text, 0.25) : Theme.accent
            Behavior on width { enabled: !drag.pressed; NumberAnimation { duration: Config.ms(200); easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: Config.ms(180) } }
        }
    }

    MouseArea {
        id: drag
        anchors.fill: parent
        property real live: 0
        cursorShape: Qt.PointingHandCursor
        function at(x) { return Math.max(0, Math.min(1, x / width)) }
        onPressed: e => { live = at(e.x); root.set(live) }
        onPositionChanged: e => { if (pressed) { live = at(e.x); root.set(live) } }
        onWheel: e => root.set(root.value + (e.angleDelta.y > 0 ? root.step : -root.step))
    }

    // icon sits on the fill (always at least one circle wide), so it reads on the accent
    Item {
        width: track.height; height: track.height
        CpIcon {
            anchors.centerIn: parent
            cp: root.icon
            font.pixelSize: 16
            color: Theme.bgPanel
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.iconClicked()
        }
    }

    CpText {
        anchors { left: parent.left; leftMargin: track.height + 2; verticalCenter: parent.verticalCenter }
        text: root.label
        font.pixelSize: 11
        font.bold: true
        // over the fill → dark text, past it → light
        color: fill.width > x + implicitWidth + 4 ? Theme.bgPanel : Theme.text
    }

    CpText {
        anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
        text: root.valueText
        font.pixelSize: 11
        color: fill.width > parent.width - 14 - implicitWidth / 2 ? Theme.bgPanel : Theme.textDim
    }
}
