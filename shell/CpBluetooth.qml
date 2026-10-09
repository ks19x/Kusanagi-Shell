// CpBluetooth.qml — control panel Bluetooth tab (only offered when there's an adapter): on/off, your
// devices (click to connect / disconnect, battery when they report it), and new ones nearby to pair —
// scanning runs only while this tab is open (Config.bluetooth.autoScan).
import QtQuick

Column {
    id: root
    required property var panel
    spacing: 12

    Component.onCompleted: Bt.hold()
    Component.onDestruction: Bt.release()

    // ---------------------------------------------------------------- adapter
    CpCard {
        width: parent.width
        height: 64
        CpIcon {
            id: bi
            x: 14; anchors.verticalCenter: parent.verticalCenter
            cp: !Bt.on ? 0xf00b2 : Bt.connected.length ? 0xf00b1 : 0xf00af
            font.pixelSize: 24
            color: Bt.on ? Theme.accent : Theme.textDim
        }
        Column {
            anchors { left: bi.right; leftMargin: 12; right: ctl.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
            spacing: 2
            CpText { text: "Bluetooth"; font.pixelSize: 13; font.bold: true }
            CpText {
                width: parent.width
                elide: Text.ElideRight
                text: Bt.busy ? "…" : Bt.blocked ? "Blocked — switch it on to unblock" : !Bt.on ? "Off"
                    : (Bt.connected.length ? Bt.connected.map(d => d.name).join(", ") : "On, nothing connected")
                    + (Bt.scanning ? "  ·  looking for devices" : "")
                font.pixelSize: 11
                color: Theme.textDim
            }
        }
        Row {
            id: ctl
            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
            spacing: 6
            CpIconButton {
                visible: Bt.on
                icon: 0xf0450
                tip: Bt.scanning ? "Stop looking" : "Look for devices"
                onClicked: Bt.scan(!Bt.scanning)
                RotationAnimation on rotation {
                    running: Bt.scanning; loops: Animation.Infinite; from: 0; to: 360; duration: 1400
                    onStopped: parent.rotation = 0
                }
            }
            CpSwitch {
                anchors.verticalCenter: parent.verticalCenter
                on: Bt.on
                onToggled: v => Bt.setOn(v)
            }
        }
    }

    BtRequest { width: parent.width }

    // ---------------------------------------------------------------- devices
    component DeviceCard: CpCard {
        id: dc
        property string title
        property var model: []
        property string empty: ""
        width: parent ? parent.width : 0
        height: dcol.implicitHeight + 24
        Column {
            id: dcol
            x: 12; y: 12
            width: parent.width - 24
            spacing: 4
            CpText { text: dc.title; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1; color: Theme.textDim; bottomPadding: 4 }
            Repeater {
                model: dc.model
                BtDeviceRow { required property var modelData; dev: modelData; width: dcol.width }
            }
            CpText { visible: dc.model.length === 0 && dc.empty !== ""; text: dc.empty; font.pixelSize: 12; color: Theme.textDim }
        }
    }

    DeviceCard {
        visible: Bt.on
        title: "MY DEVICES"
        model: Bt.paired
        empty: "Nothing paired yet — pick one below."
    }
    DeviceCard {
        visible: Bt.on && (Bt.nearby.length > 0 || Bt.scanning)
        title: "NEARBY"
        model: Bt.nearby
        empty: "Looking… put the device in pairing mode."
    }

    Row {
        spacing: 8
        CpChip { label: "Bluetooth settings"; icon: 0xf0493; onClicked: { root.panel.close(); root.panel.shellRef.openSettings("bluetooth") } }
    }
}
