// BtDeviceRow.qml — one Bluetooth device (control panel tab, Settings → Bluetooth): click connects /
// disconnects a paired one, pairs a new one; hover shows forget (paired) or cancel (pairing).
import QtQuick

Rectangle {
    id: row
    required property var dev
    readonly property bool known: dev.paired || dev.bonded
    readonly property bool live: dev.connected

    height: 40
    radius: Math.max(6, Config.look.radius - 8)
    color: live ? Theme.alpha(Theme.accent, hov.hovered ? 0.22 : 0.16) : Theme.alpha(Theme.text, hov.hovered ? 0.06 : 0)
    HoverHandler { id: hov }
    Behavior on color { ColorAnimation { duration: Config.ms(160) } }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (row.known) Bt.connectToggle(row.dev)
            else if (!row.dev.pairing) Bt.pair(row.dev)
        }
    }
    CpIcon {
        id: ic
        x: 10; anchors.verticalCenter: parent.verticalCenter
        cp: Bt.icon(row.dev)
        font.pixelSize: 17
        color: row.live ? Theme.accent : Theme.text
    }
    Column {
        anchors { left: ic.right; leftMargin: 10; right: side.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
        spacing: 1
        CpText { width: parent.width; text: row.dev.name; elide: Text.ElideRight; font.pixelSize: 12; font.bold: row.live }
        CpText { width: parent.width; text: Bt.status(row.dev); elide: Text.ElideRight; font.pixelSize: 10; color: Theme.textDim }
    }
    Row {
        id: side
        anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
        spacing: 2
        CpIconButton {
            // a new device that isn't pairing has nothing to forget
            visible: hov.hovered && (row.known || row.dev.pairing)
            width: 28; height: 28; iconSize: 14
            icon: row.dev.pairing ? 0xf0156 : 0xf0a7a
            tip: row.dev.pairing ? "Cancel" : "Forget"
            onClicked: row.dev.pairing ? Bt.cancelPair(row.dev) : Bt.forget(row.dev)
        }
    }

}
