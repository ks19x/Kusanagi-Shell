// CpRow.qml — "label ............ control" line for the Customize tab
import QtQuick

Item {
    id: root
    property string label: ""
    property string hint: ""
    default property alias control: slot.data

    implicitHeight: Math.max(34, slot.childrenRect.height)

    Column {
        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
        width: parent.width - slot.width - 16
        CpText { text: root.label; font.pixelSize: 12 }
        CpText { visible: root.hint !== ""; text: root.hint; font.pixelSize: 10; color: Theme.textDim; width: parent.width; wrapMode: Text.WordWrap }
    }

    Item {
        id: slot
        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
        width: childrenRect.width
        height: childrenRect.height
    }
}
