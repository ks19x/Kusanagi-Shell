// WdgClock.qml — desktop clock. style: big (thin time + date) · stacked (hours over minutes) ·
// minimal (one line) · analog (hands; seconds: true for a second hand). Content for WidgetItem.
import Quickshell
import QtQuick

Item {
    id: c
    property var w
    readonly property var o: w ? w.spec : ({})
    readonly property string style: o.style || "big"
    readonly property string fam: o.font || Theme.fontFamily
    readonly property bool ampm: Config.bar.clock.includes("AP")
    SystemClock { id: clock; precision: c.style === "analog" && c.o.seconds ? SystemClock.Seconds : SystemClock.Minutes }
    readonly property var d: clock.date

    implicitWidth: style === "analog" ? 220 : style === "stacked" ? stacked.implicitWidth : style === "minimal" ? minimal.implicitWidth : big.implicitWidth
    implicitHeight: style === "analog" ? 220 : style === "stacked" ? stacked.implicitHeight : style === "minimal" ? minimal.implicitHeight : big.implicitHeight
    width: implicitWidth
    height: implicitHeight

    Column {
        id: big
        visible: c.style === "big"
        spacing: -4
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatTime(c.d, c.ampm ? "h:mm" : "HH:mm")
            font.family: c.fam; font.pixelSize: 112; font.weight: Font.Light
            color: Theme.text
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatDate(c.d, "dddd · d MMMM").toUpperCase()
            font.family: c.fam; font.pixelSize: 15; font.letterSpacing: 4; font.bold: true
            color: Theme.alpha(Theme.text, 0.75)
        }
    }
    Column {
        id: stacked
        visible: c.style === "stacked"
        spacing: -34
        Text { text: Qt.formatTime(c.d, c.ampm ? "hh" : "HH"); font.family: c.fam; font.pixelSize: 132; font.bold: true; color: Theme.accent }
        Text { text: Qt.formatTime(c.d, "mm"); font.family: c.fam; font.pixelSize: 132; font.bold: true; color: Theme.text }
        Text { topPadding: 30; text: Qt.formatDate(c.d, "ddd d MMM"); font.family: c.fam; font.pixelSize: 16; color: Theme.alpha(Theme.text, 0.7) }
    }
    Row {
        id: minimal
        visible: c.style === "minimal"
        spacing: 14
        Text { text: Qt.formatTime(c.d, c.ampm ? "h:mm AP" : "HH:mm"); font.family: c.fam; font.pixelSize: 44; font.bold: true; color: Theme.text }
        Rectangle { width: 2; height: 40; radius: 1; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            Text { text: Qt.formatDate(c.d, "dddd"); font.family: c.fam; font.pixelSize: 16; font.bold: true; color: Theme.text }
            Text { text: Qt.formatDate(c.d, "d MMMM yyyy"); font.family: c.fam; font.pixelSize: 13; color: Theme.alpha(Theme.text, 0.7) }
        }
    }
    Item {
        id: analog
        visible: c.style === "analog"
        anchors.fill: parent
        Rectangle { anchors.fill: parent; radius: width / 2; color: Theme.alpha(Theme.bgPanel, c.w && c.w.card ? 0 : 0.35); border.width: 2; border.color: Theme.alpha(Theme.text, 0.25) }
        Repeater {
            model: 12
            Rectangle {
                required property int index
                width: index % 3 === 0 ? 4 : 2; height: index % 3 === 0 ? 16 : 9; radius: 1
                color: index % 3 === 0 ? Theme.text : Theme.alpha(Theme.text, 0.5)
                x: analog.width / 2 - width / 2; y: 10
                transform: Rotation { origin.x: width / 2; origin.y: analog.height / 2 - 10; angle: index * 30 }
            }
        }
        // a hand from the centre outwards (inline components can't see ids: parent = the dial)
        component Hand: Rectangle {
            property real angle: 0
            property real len: 60
            width: 5; height: len; radius: width / 2
            x: parent ? parent.width / 2 - width / 2 : 0
            y: parent ? parent.height / 2 - len + width / 2 : 0
            antialiasing: true
            transformOrigin: Item.Bottom
            rotation: angle
        }
        Hand { len: 58; width: 6; color: Theme.text; angle: (c.d.getHours() % 12 + c.d.getMinutes() / 60) * 30 }
        Hand { len: 84; width: 4; color: Theme.text; angle: (c.d.getMinutes() + c.d.getSeconds() / 60) * 6 }
        Hand { visible: !!c.o.seconds; len: 92; width: 2; color: Theme.accent; angle: c.d.getSeconds() * 6 }
        Rectangle { width: 12; height: 12; radius: 6; color: Theme.accent; anchors.centerIn: parent }
    }
}
