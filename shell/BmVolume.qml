// BmVolume.qml — output volume. Options: step. Vars: volume, icon. Level = volume. Status: muted.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property bool ready: m ? m.host.audioReady : false
    readonly property var vars: ({ volume: m ? m.host.volume : 0 })
    readonly property real level: m ? m.host.volume : 0
    readonly property string status: ready && m.host.sink.audio.muted ? "muted" : ""
    readonly property var icons: ["\uf026", "\uf027", "\uf028"]
    readonly property string format: status === "muted" ? "Muted" : "{icon}  {volume}%"
    readonly property bool shown: ready
    readonly property var actions: ({ click: "panel:sound", rightClick: "volume:mute", middleClick: "settings:sound", scrollUp: "volume:up", scrollDown: "volume:down" })
}
