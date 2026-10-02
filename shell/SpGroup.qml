// SpGroup.qml — a titled card of settings rows for the settings app
import QtQuick

Column {
    id: root
    property string title: ""
    property string hint: ""
    default property alias rows: body.data

    width: parent ? parent.width : 600
    spacing: 8

    Column {
        visible: root.title !== ""
        spacing: 2
        CpText { text: root.title.toUpperCase(); font.pixelSize: 10; font.bold: true; font.letterSpacing: 2; color: Theme.textDim }
        CpText { visible: root.hint !== ""; text: root.hint; font.pixelSize: 11; color: Theme.textDim; width: root.width; wrapMode: Text.WordWrap }
    }

    CpCard {
        width: parent.width
        height: body.implicitHeight + 28
        Column {
            id: body
            x: 16; y: 14
            width: parent.width - 32
            spacing: 12
        }
    }
}
