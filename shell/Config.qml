pragma Singleton
// Config.qml — every user-facing Kusanagi setting, stored in ~/.config/kusanagi/settings.json.
// Change a value anywhere (Config.bar.style = "solid") and it is saved and applied live;
// editing settings.json by hand works too (watched). Missing keys fall back to the defaults below.
import Quickshell
import Quickshell.Io
import QtQuick

Singleton {
    id: root

    readonly property alias look: adapter.look
    readonly property alias bar: adapter.bar
    readonly property alias workspaces: adapter.workspaces
    readonly property alias panel: adapter.panel
    readonly property alias osd: adapter.osd
    readonly property alias notifications: adapter.notifications
    readonly property alias launcher: adapter.launcher
    readonly property alias wallpaper: adapter.wallpaper
    readonly property alias lock: adapter.lock
    readonly property alias display: adapter.display
    readonly property alias windows: adapter.windows
    readonly property alias gamemode: adapter.gamemode
    readonly property alias screenshot: adapter.screenshot
    readonly property alias weather: adapter.weather
    readonly property alias dock: adapter.dock
    readonly property alias power: adapter.power
    // the bar layout engine (BarSpec.qml, docs/bar.md): [] = the classic bar from the options above
    property alias bars: adapter.bars

    // animation durations scaled by the chosen speed (off 0 / snappy 0.7 / smooth 1 / relaxed 1.4)
    function ms(base) { return Math.max(1, Math.round(base * adapter.look.animSpeed)) }
    // springiness: every OutBack overshoot is scaled by look.bounce (0 = no overshoot, 2 = very bouncy)
    function bounce(base) { return base * adapter.look.bounce }

    // every tile the control panel knows; panel.tiles picks which show and in what order
    readonly property var allTiles: ["nightlight", "dnd", "mic", "gamemode", "screenshot", "record", "colorpicker",
                                     "wallpaper", "clipboard", "lock", "settings", "launcher", "caffeine"]

    // restore one section (or everything) to the defaults
    function reset(section) {
        const sections = section ? [section] : Object.keys(defaults)
        for (const s of sections) {
            const d = defaults[s], target = adapter[s]
            for (const k in d) {
                if (d[k] !== null && typeof d[k] === "object" && !Array.isArray(d[k]))
                    for (const m in d[k]) target[k][m] = d[k][m]
                else target[k] = d[k]
            }
        }
    }

    readonly property var defaults: ({
        look: { font: "JetBrainsMono Nerd Font", radius: 16, accent: "", palette: "wallpaper", animSpeed: 1.0, shadows: true, preload: true,
                bounce: 1.0, backdrop: 0.25, borders: true, borderAccent: false },
        bar: { style: "islands", position: "top", height: 28, layout: "classic", accentLabels: false,
               opacity: 0.5, radius: 10, fontSize: 11, outline: false, clock: "HH:mm", clockBold: true, trayIconSize: 14,
               hoverGrow: true, scrollClock: "volume", scrollStats: "volume", volumeStep: 5,
               mediaPopup: true, marquee: true, mediaWidth: 20, titleWidth: 60,
               modules: { title: false, media: true, cpu: true, ram: true, gpu: false, temp: false,
                          volume: true, network: true, tray: true, power: true } },
        workspaces: { style: "pills", shown: 5, glow: true, icons: "", activeColor: "accent" },
        panel: { opacity: 0.95, width: 560, defaultTab: 0, showMedia: true, showStats: true, showWeather: true, morph: "island",
                 tileStyle: "cards", tileColumns: 4, sliderStyle: "thick", header: "big", tabs: true, order: ["tiles", "sliders", "media", "weather", "stats"],
                 tiles: ["nightlight", "dnd", "mic", "gamemode", "screenshot", "record", "colorpicker", "wallpaper"] },
        osd: { position: "top", timeout: 1400, volume: true, mic: true, gamemode: true, style: "pill", showValue: true },
        notifications: { position: "top-right", timeout: 5000, max: 5, style: "comfortable", progress: true, images: true },
        launcher: { style: "card", position: "upper", width: 640, rows: 7, descriptions: true, sortByUsage: true, terminal: "foot", layout: "list", iconSize: 32 },
        wallpaper: { folder: "~/Pictures/Wallpapers", columns: 4, renderer: "kusanagi", transition: "random", duration: 1100,
                     fill: "fill", parallax: 0.04, dim: 0, slideshow: 0 },
        lock: { engine: "hyprlock", style: "center", blur: 0.8, dim: 0.35, clock: "HH:mm", avatar: true, media: true, greeting: "" },
        power: { style: "row" },
        display: { nightTemp: 4000 },
        windows: { override: false, gapsIn: 8, gapsOut: 8, border: 2 },
        gamemode: { auto: true, effects: true, feral: true, quiet: true, dnd: true, grace: 800, announce: "manual" },
        screenshot: { position: "bottom-right", timeout: 6000, editor: "swappy -f" },
        weather: { location: "", units: "metric" },
        dock: { pinned: [], indicator: "dot", magnify: 1.35, grouped: true }
    })

    FileView {
        id: file
        path: Quickshell.env("HOME") + "/.config/kusanagi/settings.json"
        watchChanges: true
        blockLoading: true
        printErrors: false

        property bool selfWrite: false
        onFileChanged: { if (selfWrite) selfWrite = false; else reload() }
        // first run: write the defaults out so the file exists to hand-edit
        onLoadFailed: err => { if (err === FileViewError.FileNotFound) save.restart() }
        onAdapterUpdated: save.restart()

        JsonAdapter {
            id: adapter

            property var bars: []

            property JsonObject look: JsonObject {
                property string font: "JetBrainsMono Nerd Font"
                property int radius: 16               // panels, launcher, popups
                property string accent: ""            // "" = from the palette, else "#rrggbb"
                property string palette: "wallpaper"  // wallpaper | catppuccin-mocha | catppuccin-latte | gruvbox | nord | rose-pine | tokyo-night | everforest | kanagawa | mono
                property real animSpeed: 1.0          // 0 = off, 0.7 snappy, 1 smooth, 1.4 relaxed
                property bool shadows: true
                property bool preload: true           // keep launcher / panel / pickers ready → they open instantly
                property real bounce: 1.0             // springiness of every animation: 0 none … 2 very bouncy
                property real backdrop: 0.25          // how much the screen dims behind the panel / launcher / pickers
                property bool borders: true           // hairline outline on panels, cards and popups
                property bool borderAccent: false     // …in the accent colour
            }

            property JsonObject bar: JsonObject {
                property string style: "islands"      // islands | solid | floating | clear
                property string position: "top"       // top | bottom
                property int height: 28
                property string layout: "classic"     // classic (workspaces · clock · stats) | centered (clock · workspaces · stats)
                property bool accentLabels: false     // CPU / RAM labels and icons in the accent colour
                property real opacity: 0.5
                property int radius: 10
                property int fontSize: 11
                property bool outline: false
                property bool clockBold: true
                property int trayIconSize: 14
                property string clock: "HH:mm"
                property bool hoverGrow: true
                property string scrollClock: "volume"  // scroll on the clock: volume | workspaces | none
                property string scrollStats: "volume"  // scroll on the right island: volume | none
                property int volumeStep: 5
                property bool mediaPopup: true         // hover the song for controls
                property bool marquee: true            // scroll long song titles
                property int mediaWidth: 20            // characters
                property int titleWidth: 60            // window title, characters
                property JsonObject modules: JsonObject {
                    property bool title: false         // focused window's title, after the workspaces
                    property bool media: true
                    property bool cpu: true
                    property bool ram: true
                    property bool gpu: false
                    property bool temp: false
                    property bool volume: true
                    property bool network: true
                    property bool tray: true
                    property bool power: true
                }
            }

            property JsonObject workspaces: JsonObject {
                property string style: "pills"        // pills | dots | numbers | roman | kanji | custom | dwl
                property int shown: 5                 // always-visible workspaces / tags
                property bool glow: true
                property string icons: ""             // custom: one icon per workspace, space separated
                property string activeColor: "accent" // accent | accent2 | text
            }

            property JsonObject panel: JsonObject {
                property real opacity: 0.95
                property int width: 560
                property int defaultTab: 0            // 0 home · 1 system · 2 inbox · 3 quick · 4 sound · 5 network
                property bool showMedia: true
                property bool showStats: true
                property bool showWeather: true
                property string tileStyle: "cards"    // cards | pills | icons
                property int tileColumns: 4
                property string sliderStyle: "thick"  // thick | slim
                property string header: "big"         // big | compact | hidden
                property bool tabs: true              // the tab row (Home, Sound, Net, …)
                property var order: ["tiles", "sliders", "media", "weather", "stats"]   // Home, top to bottom
                property string morph: "island"       // island (grows out of the clock) | drop (slides down) | fade | sheet (right edge, full height)
                property var tiles: ["nightlight", "dnd", "mic", "gamemode", "screenshot", "record", "colorpicker", "wallpaper"]
            }

            property JsonObject osd: JsonObject {
                property string position: "top"       // top | bottom | left | right
                property int timeout: 1400
                property bool volume: true
                property bool mic: true
                property bool gamemode: true
                property string style: "pill"         // pill | minimal (a thin bar) | box (a square, lower middle)
                property bool showValue: true
            }

            property JsonObject notifications: JsonObject {
                property string position: "top-right" // top-right | top-center | top-left | bottom-right | bottom-center | bottom-left
                property int timeout: 5000
                property int max: 5
                property string style: "comfortable"  // comfortable | compact | minimal (one-line pills) | accent (coloured edge)
                property bool progress: true          // countdown line on popups
                property bool images: true            // app pictures (album art, avatars)
            }

            property JsonObject launcher: JsonObject {
                property string style: "card"         // card | spotlight (search first) | fullscreen (app grid) | side (panel at the left)
                property string position: "upper"     // upper | center
                property int width: 640
                property int rows: 7                  // visible results
                property bool descriptions: true
                property bool sortByUsage: true
                property string terminal: "foot"
                property string layout: "list"        // list | grid
                property int iconSize: 32
            }

            property JsonObject wallpaper: JsonObject {
                property string folder: "~/Pictures/Wallpapers"
                property int columns: 4
                property string renderer: "kusanagi"  // kusanagi (draws it, with transitions) | awww
                property string transition: "random"  // fade | blur | wipe | grow | slide | zoom | blinds | random
                property int duration: 1100           // ms
                property string fill: "fill"          // fill | fit | stretch | center | tile
                property real parallax: 0.04          // 0 = off; how far it pans across workspaces
                property real dim: 0                  // 0..0.6
                property int slideshow: 0             // minutes between random wallpapers; 0 = off
            }

            property JsonObject lock: JsonObject {
                property string engine: "hyprlock"    // hyprlock | kusanagi (try Settings → Lock screen → Test first)
                property string style: "center"       // center | card | split | minimal | stacked | terminal
                property real blur: 0.8
                property real dim: 0.35
                property string clock: "HH:mm"
                property bool avatar: true
                property bool media: true
                property string greeting: ""          // empty = your user name
            }

            property JsonObject gamemode: JsonObject {
                property bool auto: true              // turn on when the focused window goes fullscreen
                property bool effects: true           // blur / shadows / animations off
                property bool feral: true             // feral gamemode (performance governor)
                property bool quiet: true             // pause Kusanagi's stats, marquee, slideshow
                property bool dnd: true               // hold notification popups (critical still shows)
                property int grace: 800               // ms out of fullscreen before it switches off
                property string announce: "manual"    // show the on/off pill: manual | always | never
            }

            property JsonObject screenshot: JsonObject {
                property string position: "bottom-right" // bottom-right | bottom-left | top-right | top-left
                property int timeout: 6000
                property string editor: "swappy -f"   // command; the file is added at the end
            }

            property JsonObject display: JsonObject {
                property int nightTemp: 4000          // night light colour temperature (K)
            }

            property JsonObject windows: JsonObject {
                property bool override: false         // false = the compositor config decides gaps / borders
                property int gapsIn: 8                // px between windows
                property int gapsOut: 8               // px from the screen edges
                property int border: 2                // window border width
            }

            property JsonObject power: JsonObject {
                property string style: "row"          // row | tiles | list | fullscreen | pill
            }

            property JsonObject dock: JsonObject {
                property var pinned: []               // desktop entry ids, in order (the taskbar / dock module)
                property string indicator: "dot"      // running apps: dot | line | none
                property real magnify: 1.35           // icon scale on hover (1 = off)
                property bool grouped: true           // one icon per app (dock) instead of one per window
            }

            property JsonObject weather: JsonObject {
                property string location: ""          // "" = guessed from your IP; or a city ("Berlin")
                property string units: "metric"       // metric | imperial
            }
        }
    }

    // coalesce bursts (slider drags) into one write
    Timer {
        id: save
        interval: 250
        onTriggered: { file.selfWrite = true; file.writeAdapter() }
    }
}
