// CpField.qml — single-line text input pill; `edited` fires on every change, `accepted` on Enter
import QtQuick

Rectangle {
    id: root
    property alias text: input.text
    property string placeholder: ""
    property int icon: 0
    property bool mono: false
    signal edited(string text)
    signal accepted(string text)

    implicitWidth: 260
    implicitHeight: 34
    radius: height / 2
    color: Theme.alpha(Theme.text, 0.06)
    border.width: 1
    border.color: input.activeFocus ? Theme.alpha(Theme.accent, 0.7) : Theme.alpha(Theme.text, 0.08)
    Behavior on border.color { ColorAnimation { duration: Config.ms(160) } }

    CpIcon { id: ic; visible: root.icon !== 0; x: 12; anchors.verticalCenter: parent.verticalCenter; cp: root.icon; font.pixelSize: 14; color: Theme.textDim }
    TextInput {
        id: input
        anchors { left: parent.left; leftMargin: root.icon ? 34 : 14; right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
        color: Theme.text
        font.family: Theme.fontFamily
        font.pixelSize: 12
        selectByMouse: true
        selectionColor: Theme.alpha(Theme.accent, 0.4)
        clip: true
        onTextEdited: root.edited(text)
        onAccepted: root.accepted(text)
    }
    CpText {
        anchors { left: input.left; verticalCenter: parent.verticalCenter }
        visible: !input.text && !input.activeFocus
        text: root.placeholder
        font.pixelSize: 12
        color: Theme.textDim
    }
}
