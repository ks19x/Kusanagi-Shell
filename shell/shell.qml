// Kusanagi — sig's desktop shell (~/kusanagi; this is its Quickshell/QML part). Start it with the
// `kusanagi` command (lean memory settings, single instance). Entry point; Quickshell loads this first.
//
// Always on: wallpaper, bar, OSD, notification popups, screenshot flyout, lock (idle until used).
// Panels: control panel, launcher, wallpaper picker, clipboard are kept ready (Config.look.preload)
// so they open on the first frame; settings loads on demand.
//
// IPC (kusanagi msg <target> <fn>):
//   panel     toggle | home | sound | network | system | inbox | quick        launcher  toggle | open | close | search <text> | actions
//   settings  toggle | open | page <name> | sound | network wallpaper toggle      clipboard toggle
//   notifs    toggle | open | close | dnd | clear            lock      lock | test
//   preset    apply <name> | next
//   power     toggle | open
//   osd       preview <volume|mic|game>                      gamemode  toggle
//   bar       media | traymenu <n> | template <name> | templates | classic
//   setup     open                                           idle      status | toggle
//   record    replay | save | record | stream | stop | status  updates   check | count | upgrade
import Quickshell
import Quickshell.Io
import QtQuick

ShellRoot {
    id: shell

    Wallpaper {}
    Bar { id: bar; shell: shell }
    readonly property var barItem: bar     // panel morphs out of its clock island
    Osd { id: osd }
    Lock { id: lock }
    Component.onCompleted: { Idle.lock = lock; Polkit.start() }
    ScreenshotOsd {}
    GreeterSync {}          // the login screen follows your looks (only when it's installed)
    NotificationPopups {}
    // Hyprland's global shortcuts (kusanagi:mediaToggle …); Mango has no such protocol
    LazyLoader { active: Wm.kind === "hyprland"; Shortcuts { shell: shell } }

    // ---------- on-demand surfaces ----------

    // Config.look.preload (default): created at startup and kept, so they open on the very first frame.
    // Off: loaded on first use and dropped 600ms after closing (saves memory, small hitch on open).
    component OnDemand: LazyLoader {
        id: od
        property bool keep: Config.look.preload
        active: keep
        onKeepChanged: if (keep) active = true; else if (item && !item.showing) unload.restart()
        function get() { unload.stop(); active = true; return item }
        property Connections watch: Connections {
            target: od.item
            function onShowingChanged() { if (!od.item.showing && !od.keep) unload.restart() }
        }
        property Timer unload: Timer {
            interval: 600
            onTriggered: if (!od.keep && od.item && !od.item.showing) { od.active = false; gcLater.restart() }
        }
    }
    // hand freed memory back once something is unloaded
    Timer { id: gcLater; interval: 300; onTriggered: gc() }

    OnDemand { id: panel; ControlPanel { shellRef: shell } }
    OnDemand { id: launcher; Launcher {} }
    OnDemand { id: wallpapers; WallpaperPicker {} }
    OnDemand { id: clipboard; Clipboard {} }
    OnDemand { id: power; PowerMenu {} }
    OnDemand { id: settings; keep: false; Settings {} }     // a normal window, opened rarely
    OnDemand { id: setup; keep: false; Setup {} }           // the first-run wizard
    function openSetup() { setup.get().show() }
    // no settings.json at start = a new user: say hello once everything has settled
    Timer { running: Config.firstRun; interval: 2500; onTriggered: shell.openSetup() }

    // settings remembers its page across being unloaded
    property string settingsPage: "presets"
    Connections {
        target: settings.item
        function onPageChanged() { shell.settingsPage = settings.item.page }
    }
    function openSettings(page) {
        const w = settings.get()
        w.show(page || settingsPage)
    }

    function openPower() { power.get().open() }

    function panelTab(i) {
        const p = panel.get()
        if (p.showing && p.tab === i) p.close(); else p.openTab(i)
    }
    // kept for older callers (bar clock, Shortcuts)
    function notifsPanel() { return panel.get() }

    // ---------- IPC ----------

    IpcHandler {
        target: "panel"
        function toggle(): void { const p = panel.get(); p.showing = !p.showing }
        function home(): void { shell.panelTab(0) }
        function system(): void { shell.panelTab(1) }
        function inbox(): void { shell.panelTab(2) }
        function quick(): void { shell.panelTab(3) }
        function sound(): void { shell.panelTab(4) }
        function network(): void { shell.panelTab(5) }
    }
    IpcHandler {
        target: "caffeine"
        function toggle(): void { Caffeine.toggle() }
        function on(): void { Caffeine.active = true }
        function off(): void { Caffeine.active = false }
    }
    IpcHandler {
        target: "notifs"
        // Super+N: straight to the Inbox tab of the control panel
        function toggle(): void { shell.panelTab(2) }
        function open(): void { panel.get().openTab(2) }
        function close(): void { if (panel.item) panel.item.close() }
        function dnd(): void { Notifs.dnd = !Notifs.dnd }
        function clear(): void { Notifs.clearAll() }
        function test(): void { Notifs.test() }
    }
    IpcHandler {
        target: "settings"
        function toggle(): void { if (settings.item && settings.item.showing) settings.item.hide(); else shell.openSettings() }
        function open(): void { shell.openSettings() }
        function show(): void { shell.openSettings() }
        function hide(): void { if (settings.item) settings.item.hide() }
        function page(name: string): void { shell.openSettings(name) }
        function sound(): void { shell.openSettings("sound") }
        function network(): void { shell.openSettings("network") }
    }
    IpcHandler {
        target: "launcher"
        function toggle(): void { launcher.get().toggle() }
        function open(): void { launcher.get().open() }
        function close(): void { if (launcher.item) launcher.item.close() }
        function search(text: string): void { const l = launcher.get(); l.open(); l.searchFor(text) }
        function actions(): void { if (launcher.item) launcher.item.actionsOfSelected() }
    }
    IpcHandler {
        target: "wallpaper"
        function toggle(): void { wallpapers.get().toggle() }
    }
    IpcHandler {
        target: "power"
        function toggle(): void { power.get().toggle() }
        function open(): void { power.get().open() }
    }
    IpcHandler {
        target: "clipboard"
        function toggle(): void { clipboard.get().toggle() }
    }
    IpcHandler {
        target: "lock"
        function lock(): void { lock.lock() }
        function test(): void { lock.test() }
    }
    IpcHandler {
        target: "preset"
        function apply(name: string): void { Presets.applyNamed(name) }
        // looks as files: export → ~/kusanagi-looks/<name>.kusanagi; import applies + keeps one, but a look
        // that runs commands is refused here (Settings → Presets shows those before anything happens)
        function exportLook(name: string): string { Presets.exportLook(name, false); return Presets.looksDir + "/" + Presets.slug(name) + ".kusanagi" }
        function importLook(path: string): string {
            const r = Presets.readLookFile(path)
            if (r.error) return r.error
            if (r.commands.length) return "this look runs commands — open Settings → Presets → Share a look to review it"
            Presets.keep(r.look); Presets.apply(r.look)
            return "applied " + r.look.name
        }
        function next(): void { Presets.next() }
    }
    IpcHandler {
        target: "setup"
        function open(): void { shell.openSetup() }
    }
    IpcHandler {
        target: "idle"
        function status(): string { return Idle.status }
        function toggle(): void { Config.idle.enabled = !Config.idle.enabled }
    }
    IpcHandler {
        target: "record"
        function replay(): void { Recorder.replay() }
        function save(): void { Recorder.save() }
        function record(): void { Recorder.record() }
        function stream(): void { Recorder.stream() }
        function stop(): void { Recorder.stop() }
        function status(): string { return Recorder.mode }
    }
    IpcHandler {
        target: "updates"
        function check(): void { Updates.check() }
        function count(): string { return Updates.known ? String(Updates.count) : "?" }
        function upgrade(): void { Updates.upgrade() }
    }
    IpcHandler {
        target: "osd"
        function preview(kind: string): void { osd.preview(kind) }
    }
    IpcHandler {
        target: "bar"
        function media(): void { bar.mediaPinned = !bar.mediaPinned }
        function traymenu(n: int): void { bar.openTrayIndex(n) }
        // layouts (BarTemplates.qml): template <powerline|slants|dwm|…>, classic = back to Settings → Bar options
        function template(name: string): void { const b = BarTemplates.bars(name); if (b.length) Config.bars = b }
        function classic(): void { Config.bars = [] }
        function templates(): string { return BarTemplates.list.map(t => t.id).join(" ") }
    }
}
