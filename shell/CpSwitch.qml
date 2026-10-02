// CpSwitch.qml — on/off switch with a springy knob
import QtQuick

Rectangle {
    id: root
    property bool on: false
    signal toggled(bool on)

    implicitWidth: 38
    implicitHeight: 22
    radius: height / 2
    color: on ? Theme.accent : Theme.alpha(Theme.text, 0.12)
    Behavior on color { ColorAnimation { duration: Config.ms(200) } }

    Rectangle {
        width: parent.height - 6; height: width
        radius: width / 2
        y: 3
        x: root.on ? root.width - width - 3 : 3
        color: root.on ? Theme.bgPanel : Theme.text
        Behavior on x { NumberAnimation { duration: Config.ms(260); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(1.6) } }
        Behavior on color { ColorAnimation { duration: Config.ms(200) } }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled(!root.on)
    }
}
