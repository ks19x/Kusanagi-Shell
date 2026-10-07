// CpTile.qml — quick toggle / action tile. `on` fills it with the accent; actions just flash on press.
// look (Config.panel.tileStyle): cards (icon over label + state) · pills (icon beside the label) ·
// icons (just a round icon button)
import QtQuick

Rectangle {
    id: tile

    property int icon: 0
    property string label: ""
    property string sub: ""
    property bool on: false
    property string look: Config.panel.tileStyle
    signal clicked()

    readonly property bool pills: look === "pills"
    readonly property bool icons: look === "icons"
    implicitHeight: icons ? 58 : pills ? 48 : 72
    radius: icons || pills ? height / 2 : Math.max(6, Config.look.radius - 6)
    color: on ? Theme.accent : Theme.alpha(Theme.text, area.containsMouse ? 0.085 : 0.045)
    border.width: 1
    border.color: on ? "transparent" : Theme.alpha(Theme.text, 0.06)
    scale: area.pressed ? 0.95 : 1

    Behavior on color { ColorAnimation { duration: Config.ms(180); easing.type: Easing.OutCubic } }
    Behavior on scale { NumberAnimation { duration: Config.ms(160); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(2.5) } }

    CpIcon {
        x: tile.icons ? (tile.width - width) / 2 : tile.pills ? 16 : 12
        y: tile.icons || tile.pills ? (tile.height - height) / 2 : 11
        cp: tile.icon
        font.pixelSize: tile.icons ? 22 : 19
        color: tile.on ? Theme.bgPanel : Theme.text
        Behavior on color { ColorAnimation { duration: Config.ms(180) } }
    }

    Column {
        visible: !tile.icons
        anchors {
            left: parent.left; right: parent.right
            leftMargin: tile.pills ? 46 : 12; rightMargin: tile.pills ? 14 : 12
        }
        y: tile.pills ? (tile.height - height) / 2 : tile.height - height - 10
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
