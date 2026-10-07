// BmNotifications.qml — notification count / do not disturb. Vars: count, icon. Status: dnd | unread | none.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property var vars: ({ count: Notifs.count })
    readonly property string status: Notifs.dnd ? "dnd" : Notifs.count > 0 ? "unread" : "none"
    readonly property var icons: ({ dnd: String.fromCodePoint(0xf009b), unread: String.fromCodePoint(0xf116b), none: String.fromCodePoint(0xf009a) })
    readonly property string format: "{icon}"
    readonly property var actions: ({ click: "notifs", rightClick: "dnd" })
    readonly property string tooltip: (Notifs.dnd ? "Do not disturb · " : "") + Notifs.count + " notification" + (Notifs.count === 1 ? "" : "s")
}
