pragma Singleton
// Dock.qml — pinned apps for the taskbar / dock module (Config.dock), shared by the bar and Settings.
// Apps are desktop entry ids ("firefox", "org.gnome.Nautilus"); windows are matched to them by app id.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import QtQuick

Singleton {
    id: root

    readonly property var pinned: { try { return JSON.parse(JSON.stringify(Config.dock.pinned || [])) } catch (e) { return [] } }
    function isPinned(id) { return pinned.includes(id) }
    function pin(id) { if (id && !isPinned(id)) Config.dock.pinned = pinned.concat([id]) }
    function unpin(id) { Config.dock.pinned = pinned.filter(x => x !== id) }
    function toggle(id) { isPinned(id) ? unpin(id) : pin(id) }
    function move(id, delta) {
        const l = pinned.slice(), i = l.indexOf(id), j = i + delta
        if (i < 0 || j < 0 || j >= l.length) return
        l.splice(i, 1); l.splice(j, 0, id); Config.dock.pinned = l
    }

    function entry(id) { return id ? (DesktopEntries.byId(id) ?? DesktopEntries.heuristicLookup(id)) : null }
    function name(id) { if (id === "org.quickshell") return "Kusanagi"; const e = entry(id); return e ? e.name : id }
    // an icon that really exists: the entry's (name or absolute path), then the id in a few spellings,
    // then a generic app icon; "" = nothing found (callers draw a letter tile). Quickshell 0.3's
    // iconPath(name, "fallback") form does not fall back, so every candidate is checked.
    property var iconCache: ({})
    function iconName(n) {
        if (!n) return ""
        if (n.startsWith("/")) return "file://" + n
        if (n.startsWith("file:") || n.startsWith("image:")) return n
        return Quickshell.iconPath(n, true)
    }
    function icon(id) {
        if (!id) return ""
        if (id in iconCache) return iconCache[id]
        if (id === "org.quickshell") return Qt.resolvedUrl("logo.svg")    // Kusanagi's own windows
        const e = entry(id)
        const tries = [e ? e.icon : "", id, id.toLowerCase(), id.split(".").pop(), id.split(".").pop().toLowerCase(), "application-x-executable"]
        let found = ""
        for (const n of tries) { found = iconName(n); if (found) break }
        iconCache[id] = found
        return found
    }
    function iconOfEntry(e) { return e ? (iconName(e.icon) || icon(e.id)) : "" }
    function launch(id) { const e = entry(id); if (e) e.execute() }

    // a window's app: its desktop entry id when one matches, else its raw app id (cached per app id)
    property var cache: ({})
    function keyOf(t) {
        const a = t ? t.appId || "" : ""
        if (a in cache) return cache[a]
        // exact id first ("foot" → foot.desktop), the heuristic only when that fails
        const e = DesktopEntries.byId(a) ?? DesktopEntries.byId(a.toLowerCase()) ?? DesktopEntries.heuristicLookup(a)
        const k = e ? e.id : a
        cache[a] = k
        return k
    }
    function windowsOf(id) { return ToplevelManager.toplevels.values.filter(t => keyOf(t) === id) }
    // click on a dock icon: start it, focus it, or step through its windows
    function activate(id) {
        const w = windowsOf(id)
        if (!w.length) { launch(id); return }
        const i = w.findIndex(t => t.activated)
        w[(i + 1) % w.length].activate()
    }

    // your most launched apps (the launcher counts them), for "pin my top apps"
    FileView {
        id: usage
        path: Quickshell.env("HOME") + "/.config/kusanagi/launcher-usage.json"
        printErrors: false
        blockLoading: true
    }
    function topUsed(n) {
        usage.reload()
        let counts = {}
        try { counts = JSON.parse(usage.text()).counts || {} } catch (e) {}
        return Object.keys(counts).sort((a, b) => counts[b] - counts[a]).filter(id => entry(id)).slice(0, n || 6)
    }
}
