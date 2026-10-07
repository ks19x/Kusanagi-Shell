// BmClock.qml — clock. Options: timeFormat (Qt, default Settings → Bar), dateFormat. Vars: time, date.
// Provider for BarModule (see its header): vars / level / status / defaults for this module type.
import Quickshell
import QtQuick

Item {
    id: p
    property var m
    readonly property var o: m ? m.eff : ({})
    readonly property string timeFormat: o.timeFormat || Config.bar.clock
    readonly property string dateFormat: o.dateFormat || "ddd d MMM"
    SystemClock {
        id: clock
        precision: (p.timeFormat + p.dateFormat + (p.o.format || "")).includes("s") ? SystemClock.Seconds : SystemClock.Minutes
    }
    readonly property var vars: ({ time: Qt.formatDateTime(clock.date, timeFormat), date: Qt.formatDateTime(clock.date, dateFormat) })
    readonly property string format: "{time}"
    readonly property var actions: ({ click: "panel" })
    readonly property bool tooltipRich: true
    readonly property string tooltip: m && m.hovered ? calendar() : ""
    // the control panel grows out of the first clock's island
    onMChanged: if (m && m.group) m.host.registerClock(m.group, m.win)
    Component.onDestruction: if (m && m.group) m.host.unregisterClock(m.group)

    function calendar() {
        const now = clock.date, loc = Qt.locale()
        const y = now.getFullYear(), mo = now.getMonth(), today = now.getDate()
        const first = loc.firstDayOfWeek % 7
        const days = new Date(y, mo + 1, 0).getDate()
        const lead = (new Date(y, mo, 1).getDay() - first + 7) % 7
        const title = loc.standaloneMonthName(mo) + " " + y
        const pad = Math.max(0, Math.floor((20 - title.length) / 2))
        let out = " ".repeat(pad) + title + "\n"
        const names = []
        for (let i = 0; i < 7; i++) names.push(loc.dayName((first + i) % 7, Locale.ShortFormat).slice(0, 2))
        out += names.join(" ") + "\n"
        let line = "   ".repeat(lead)
        for (let d = 1; d <= days; d++) {
            const cell = (d < 10 ? " " : "") + d
            line += d === today ? `<b><u>${cell}</u></b>` : cell
            if ((lead + d) % 7 === 0) { out += line + "\n"; line = "" } else line += " "
        }
        if (line.trim()) out += line.replace(/ $/, "")
        return `<pre style="font-family:'${Theme.fontFamily}'">${out.replace(/\n$/, "")}</pre><small>click: control panel</small>`
    }
}
