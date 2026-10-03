pragma Singleton
// Weather.qml — current conditions + 3-day forecast from wttr.in (no API key).
// Fetched only while something shows it (`wanted`, i.e. the control panel), at most every
// 30 min; the last result is cached in ~/.cache/kusanagi/weather.json so it shows instantly.
// Config.weather.location: "" = wttr.in guesses from your IP; units: metric | imperial.
import Quickshell
import Quickshell.Io
import QtQuick

Singleton {
    id: root

    property bool wanted: false
    readonly property bool imperial: Config.weather.units === "imperial"

    property var data: null           // wttr.in j1 JSON
    property double fetchedAt: 0      // ms since epoch
    property bool loading: false
    property string error: ""

    readonly property var current: data && data.current_condition ? data.current_condition[0] : null
    readonly property string place: {
        if (Config.weather.location) return Config.weather.location
        const a = data && data.nearest_area ? data.nearest_area[0] : null
        return a ? a.areaName[0].value : ""
    }
    readonly property int temp: current ? +(imperial ? current.temp_F : current.temp_C) : 0
    readonly property int feels: current ? +(imperial ? current.FeelsLikeF : current.FeelsLikeC) : 0
    readonly property string desc: current ? current.weatherDesc[0].value.trim() : ""
    readonly property int humidity: current ? +current.humidity : 0
    readonly property string wind: current ? (imperial ? current.windspeedMiles + " mph" : current.windspeedKmph + " km/h") : ""
    readonly property string unit: imperial ? "°F" : "°C"
    readonly property int icon: current ? glyph(+current.weatherCode, isNight()) : 0xf0590
    // today + the next two days: [{ day, hi, lo, icon }]
    readonly property var days: {
        if (!data || !data.weather) return []
        return data.weather.slice(0, 3).map((d, i) => ({
            day: i === 0 ? "Today" : Qt.formatDate(new Date(d.date + "T12:00:00"), "ddd"),
            hi: +(imperial ? d.maxtempF : d.maxtempC),
            lo: +(imperial ? d.mintempF : d.mintempC),
            icon: glyph(+(d.hourly && d.hourly[4] ? d.hourly[4].weatherCode : 113), false)
        }))
    }

    function isNight() {
        const a = data && data.weather ? data.weather[0].astronomy[0] : null
        if (!a) return false
        const t = n => { const m = n.match(/(\d+):(\d+) (AM|PM)/); return m ? (+m[1] % 12 + (m[3] === "PM" ? 12 : 0)) * 60 + +m[2] : 0 }
        const now = new Date(), mins = now.getHours() * 60 + now.getMinutes()
        return mins < t(a.sunrise) || mins > t(a.sunset)
    }

    // wttr.in / WWO condition codes → Material Design weather glyphs (Nerd Font)
    function glyph(code, night) {
        if (code === 113) return night ? 0xf0594 : 0xf0599                         // clear
        if (code === 116) return night ? 0xf0f31 : 0xf0595                         // partly cloudy
        if (code === 119 || code === 122) return 0xf0590                            // cloudy
        if ([143, 248, 260].includes(code)) return 0xf0591                          // fog
        if ([200, 386, 389, 392, 395].includes(code)) return 0xf0593                // thunder
        if ([179, 182, 185, 227, 230, 281, 284, 311, 314, 317, 320, 323, 326, 329, 332, 335, 338,
             350, 362, 365, 368, 371, 374, 377].includes(code)) return 0xf0598      // snow / sleet
        if ([299, 302, 305, 308, 356, 359].includes(code)) return 0xf0596           // heavy rain
        return 0xf0597                                                              // rain / drizzle
    }

    function refresh(force) {
        if (loading) return
        if (!force && fetchedAt && Date.now() - fetchedAt < 30 * 60 * 1000) return
        loading = true
        const loc = encodeURIComponent(Config.weather.location || "")
        fetch.command = ["curl", "-sf", "--max-time", "10", `https://wttr.in/${loc}?format=j1`]
        fetch.running = true
    }

    onWantedChanged: if (wanted) refresh(false)
    Connections {
        target: Config.weather
        function onLocationChanged() { root.fetchedAt = 0; if (root.wanted) root.refresh(true) }
    }
    Timer { interval: 30 * 60 * 1000; repeat: true; running: root.wanted; onTriggered: root.refresh(false) }

    Process {
        id: fetch
        stdout: StdioCollector {
            onStreamFinished: {
                root.loading = false
                try {
                    const d = JSON.parse(text)
                    if (!d.current_condition) throw new Error("no data")
                    root.data = d
                    root.fetchedAt = Date.now()
                    root.error = ""
                    cache.setText(JSON.stringify({ at: root.fetchedAt, location: Config.weather.location, data: d }))
                } catch (e) {
                    root.error = "Weather unavailable"
                }
            }
        }
        onExited: code => { if (code !== 0) { root.loading = false; root.error = "Weather unavailable" } }
    }

    FileView {
        id: cache
        path: Quickshell.env("HOME") + "/.cache/kusanagi/weather.json"
        printErrors: false
        onLoaded: {
            try {
                const c = JSON.parse(text())
                if ((c.location || "") === (Config.weather.location || "")) { root.data = c.data; root.fetchedAt = c.at }
            } catch (e) {}
        }
    }
}
