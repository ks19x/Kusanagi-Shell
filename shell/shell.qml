// Kusanagi — sig's desktop shell (~/kusanagi; this is its Quickshell/QML part). Start it with the
// `kusanagi` command (lean memory settings, single instance). Entry point; Quickshell loads this first.
//
// Always on: wallpaper, bar, OSD, notification popups, screenshot flyout, lock (idle until used).
// Panels: control panel, launcher, wallpaper picker, clipboard are kept ready (Config.look.preload)
// so they open on the first frame; settings loads on demand.
//
// IPC (kusanagi msg <target> <fn>):
//   panel     toggle | home | system | inbox | quick        launcher  toggle | open | close | search <text> | actions
//   settings  toggle | open | page <name> | sound | network wallpaper toggle      clipboard toggle
//   notifs    toggle | open | close | dnd | clear            lock      lock | test
//   preset    apply <name> | next
//   power     toggle | open
//   osd       preview <volume|mic|game>                      bar       media | traymenu <n>   gamemode toggle
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
    ScreenshotOsd {}
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
    }
    IpcHandler {
        target: "notifs"
        // Super+N: straight to the Inbox tab of the control panel
        function toggle(): void { shell.panelTab(2) }
        function open(): void { panel.get().openTab(2) }
        function close(): void { if (panel.item) panel.item.close() }
        function dnd(): void { Notifs.dnd = !Notifs.dnd }
        function clear(): void { Notifs.clearAll() }
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
        function next(): void { Presets.next() }
    }
    IpcHandler {
        target: "osd"
        function preview(kind: string): void { osd.preview(kind) }
    }
    IpcHandler {
        target: "bar"
        function media(): void { bar.mediaPinned = !bar.mediaPinned }
        function traymenu(n: int): void { bar.openTrayIndex(n) }
    }
}
