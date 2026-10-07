// SpFold.qml — a settings section that folds open and closed (smooth height + fade, nothing else moves).
import QtQuick

Column {
    id: root
    property string title: ""
    property string hint: ""
    property bool open: false
    default property alias content: body.data
    width: parent ? parent.width : 600

    Item {
        width: parent.width
        height: 38
        Rectangle {
            anchors.fill: parent
            radius: 9
            color: hov.hovered ? Theme.alpha(Theme.text, 0.05) : "transparent"
            Behavior on color { ColorAnimation { duration: Config.ms(120) } }
        }
        CpIcon {
            id: chev
            x: 8; anchors.verticalCenter: parent.verticalCenter
            cp: 0xf0142
            font.pixelSize: 16
            color: root.open ? Theme.accent : Theme.textDim
            rotation: root.open ? 90 : 0
            Behavior on rotation { NumberAnimation { duration: Config.ms(200); easing.type: Easing.OutCubic } }
        }
        CpText {
            x: 32; anchors.verticalCenter: parent.verticalCenter
            text: root.title
            font.pixelSize: 12
            font.bold: true
        }
        CpText {
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            width: parent.width * 0.55
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            text: root.hint
            font.pixelSize: 10
            color: Theme.textDim
        }
        HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: root.open = !root.open }
    }
    Item {
        width: parent.width
        height: root.open ? body.implicitHeight + 10 : 0
        clip: true
        Behavior on height { NumberAnimation { duration: Config.ms(240); easing.type: Easing.OutCubic } }
        Column {
            id: body
            x: 8; y: 4
            width: parent.width - 8
            spacing: 6
            opacity: root.open ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Config.ms(180) } }
        }
    }
}
