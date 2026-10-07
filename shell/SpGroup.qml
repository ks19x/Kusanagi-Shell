// SpGroup.qml — a titled section of settings rows: a plain title (+ a line of hint) over a soft card.
import QtQuick

Column {
    id: root
    property string title: ""
    property string hint: ""
    property int icon: 0
    default property alias rows: body.data

    width: parent ? parent.width : 600
    spacing: 10

    Row {
        visible: root.title !== ""
        spacing: 10
        Rectangle {
            visible: root.icon !== 0
            width: 28; height: 28; radius: 9
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.alpha(Theme.accent, 0.14)
            CpIcon { anchors.centerIn: parent; cp: root.icon; font.pixelSize: 15; color: Theme.accent }
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            CpText { text: root.title; font.pixelSize: 14; font.bold: true }
            CpText { visible: root.hint !== ""; text: root.hint; font.pixelSize: 11; color: Theme.textDim; width: root.width - (root.icon ? 38 : 0); wrapMode: Text.WordWrap }
        }
    }

    Rectangle {
        width: parent.width
        height: body.implicitHeight + 32
        radius: Math.max(10, Config.look.radius - 2)
        color: Theme.alpha(Theme.text, 0.035)
        border.width: Config.look.borders ? 1 : 0
        border.color: Theme.alpha(Theme.text, 0.045)
        Column {
            id: body
            x: 18; y: 16
            width: parent.width - 36
            spacing: 14
        }
    }
}
