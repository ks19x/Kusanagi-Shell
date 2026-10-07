// WdgCalendar.qml — this month, today ringed in the accent.
import Quickshell
import QtQuick

Column {
    id: cal
    property var w
    spacing: 10
    SystemClock { id: clock; precision: SystemClock.Minutes }
    readonly property var d: clock.date
    readonly property int first: Qt.locale().firstDayOfWeek % 7
    readonly property int lead: (new Date(d.getFullYear(), d.getMonth(), 1).getDay() - first + 7) % 7
    readonly property int days: new Date(d.getFullYear(), d.getMonth() + 1, 0).getDate()
    CpText { text: Qt.locale().standaloneMonthName(cal.d.getMonth()) + " " + cal.d.getFullYear(); font.pixelSize: 18; font.bold: true }
    Grid {
        columns: 7
        spacing: 4
        Repeater {
            model: 7
            CpText {
                required property int index
                width: 34; horizontalAlignment: Text.AlignHCenter
                text: Qt.locale().dayName((cal.first + index) % 7, Locale.ShortFormat).slice(0, 2)
                font.pixelSize: 11; font.bold: true; color: Theme.textDim
            }
        }
        Repeater {
            model: cal.lead + cal.days
            Rectangle {
                required property int index
                readonly property int day: index - cal.lead + 1
                readonly property bool today: day === cal.d.getDate()
                width: 34; height: 30; radius: 15
                color: today ? Theme.accent : "transparent"
                CpText {
                    anchors.centerIn: parent
                    visible: parent.day > 0
                    text: parent.day
                    font.pixelSize: 13
                    font.bold: parent.today
                    color: parent.today ? Theme.bgPanel : Theme.text
                }
            }
        }
    }
}
