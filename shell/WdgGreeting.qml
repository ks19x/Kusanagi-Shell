// WdgGreeting.qml — "Good evening, sig" and today's date. name: what to call you.
import Quickshell
import QtQuick

Column {
    id: g
    property var w
    readonly property var o: w ? w.spec : ({})
    spacing: 6
    SystemClock { id: clock; precision: SystemClock.Minutes }
    readonly property int h: clock.date.getHours()
    readonly property string part: h < 5 ? "Good night" : h < 12 ? "Good morning" : h < 18 ? "Good afternoon" : "Good evening"
    Text {
        text: g.part + ", " + (g.o.name || Quickshell.env("USER")) + "."
        font.family: Theme.fontFamily; font.pixelSize: 40; font.bold: true
        color: Theme.text
    }
    Text {
        text: Qt.formatDate(clock.date, "dddd, d MMMM")
        font.family: Theme.fontFamily; font.pixelSize: 16
        color: Theme.accent
    }
}
