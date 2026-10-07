// BmWeather.qml — wttr.in (Settings → Panel location/units). Vars: temp, unit, feels, desc, place, icon.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import QtQuick

Item {
    id: p
    property var m
    Component.onCompleted: Weather.wanted = true
    readonly property var vars: ({ temp: Weather.temp, unit: Weather.unit, feels: Weather.feels, desc: Weather.desc, place: Weather.place })
    readonly property string icon: String.fromCodePoint(Weather.icon)
    readonly property string format: "{icon} {temp}{unit}"
    readonly property bool shown: Weather.current !== null
    readonly property var actions: ({ click: "panel" })
    readonly property string tooltip: `${Weather.place}: ${Weather.desc}, feels ${Weather.feels}${Weather.unit}`
}
