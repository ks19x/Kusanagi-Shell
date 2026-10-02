// CpIconButton.qml — round icon button (header actions, media controls)
import QtQuick

Rectangle {
    id: root
    property int icon: 0
    property int iconSize: 16
    property bool filled: false
    property string tip: ""
    signal clicked()

    implicitWidth: 34
    implicitHeight: 34
    radius: width / 2
    color: filled ? Theme.accent : Theme.alpha(Theme.text, area.containsMouse ? 0.1 : 0)
    scale: area.pressed ? 0.9 : 1
    Behavior on color { ColorAnimation { duration: Config.ms(160) } }
    Behavior on scale { NumberAnimation { duration: Config.ms(160); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(2.5) } }

    CpIcon {
        anchors.centerIn: parent
        cp: root.icon
        font.pixelSize: root.iconSize
        color: root.filled ? Theme.bgPanel : (area.containsMouse ? Theme.text : Theme.textDim)
        Behavior on color { ColorAnimation { duration: Config.ms(160) } }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
