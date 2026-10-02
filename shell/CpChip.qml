// CpChip.qml — small toggle chip (bar modules, workspace styles, fonts…)
import QtQuick

Rectangle {
    id: root
    property string label: ""
    property int icon: 0
    property bool on: false
    property string fontFamily: ""
    signal clicked()

    implicitWidth: row.implicitWidth + 24
    implicitHeight: 28
    radius: height / 2
    color: on ? Theme.alpha(Theme.accent, area.containsMouse ? 0.32 : 0.22)
              : Theme.alpha(Theme.text, area.containsMouse ? 0.09 : 0.05)
    border.width: 1
    border.color: on ? Theme.alpha(Theme.accent, 0.8) : Theme.alpha(Theme.text, 0.06)
    scale: area.pressed ? 0.94 : 1
    Behavior on color { ColorAnimation { duration: Config.ms(180) } }
    Behavior on border.color { ColorAnimation { duration: Config.ms(180) } }
    Behavior on scale { NumberAnimation { duration: Config.ms(160); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(2.5) } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        CpIcon { visible: root.icon !== 0; cp: root.icon; font.pixelSize: 13; color: root.on ? Theme.accent : Theme.textDim }
        CpText {
            text: root.label
            font.pixelSize: 11
            font.family: root.fontFamily || Theme.fontFamily
            color: root.on ? Theme.text : Theme.textDim
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
