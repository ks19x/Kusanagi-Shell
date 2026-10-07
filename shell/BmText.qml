// BmText.qml — static text / icon: put it in format (or text).
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property var vars: ({})
    readonly property string format: m && m.eff.text !== undefined ? String(m.eff.text) : ""
}
