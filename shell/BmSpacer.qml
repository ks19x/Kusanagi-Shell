// BmSpacer.qml — empty room. Options: size (px).
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property bool visual: true
    readonly property var vars: ({})
    readonly property string format: ""
    readonly property real n: m && m.eff.size !== undefined ? m.eff.size : 8
    implicitWidth: m && m.vertical ? 1 : n
    implicitHeight: m && m.vertical ? n : 1
    width: implicitWidth
    height: implicitHeight
}
