// BmMic.qml — microphone. Options: step. Vars: volume, icon. Level = volume. Status: muted.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    readonly property bool ready: m ? m.host.micReady : false
    readonly property var vars: ({ volume: m ? m.host.micVolume : 0 })
    readonly property real level: m ? m.host.micVolume : 0
    readonly property string status: ready && m.host.source.audio.muted ? "muted" : ""
    readonly property var icons: ({ muted: String.fromCodePoint(0xf036d), "default": String.fromCodePoint(0xf036c) })
    readonly property string format: "{icon} {volume}%"
    readonly property bool shown: ready
    readonly property var actions: ({ click: "mic:mute", scrollUp: "mic:up", scrollDown: "mic:down" })
}
