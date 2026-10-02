// CpTile.qml — quick toggle / action tile: icon over label (+ small state line).
// `on` fills it with the accent; actions just flash on press.
import QtQuick

Rectangle {
    id: tile

    property int icon: 0
    property string label: ""
    property string sub: ""
    property bool on: false
    signal clicked()

    implicitHeight: 72
    radius: Math.max(6, Config.look.radius - 6)
    color: on ? Theme.accent : Theme.alpha(Theme.text, area.containsMouse ? 0.085 : 0.045)
    border.width: 1
    border.color: on ? "transparent" : Theme.alpha(Theme.text, 0.06)
    scale: area.pressed ? 0.95 : 1

    Behavior on color { ColorAnimation { duration: Config.ms(180); easing.type: Easing.OutCubic } }
    Behavior on scale { NumberAnimation { duration: Config.ms(160); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(2.5) } }

    CpIcon {
        x: 12; y: 11
        cp: tile.icon
        font.pixelSize: 19
        color: tile.on ? Theme.bgPanel : Theme.text
        Behavior on color { ColorAnimation { duration: Config.ms(180) } }
    }

    Column {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 12; bottomMargin: 10 }
        spacing: 1
        CpText {
            width: parent.width
            text: tile.label
            elide: Text.ElideRight
            font.pixelSize: 11
            font.bold: true
            color: tile.on ? Theme.bgPanel : Theme.text
        }
        CpText {
            width: parent.width
            visible: text !== ""
            text: tile.sub
            elide: Text.ElideRight
            font.pixelSize: 10
            color: tile.on ? Theme.alpha(Theme.bgPanel, 0.7) : Theme.textDim
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: tile.clicked()
    }
}
