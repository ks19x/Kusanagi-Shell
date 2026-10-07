pragma Singleton
// Notifs.qml — notification daemon (replaces mako): history, popups, do-not-disturb
import Quickshell
import Quickshell.Services.Notifications
import QtQuick

Singleton {
    id: root

    property bool dnd: false
    // notifications currently shown as popups (newest first)
    property var popups: []
    // everything still in the history (oldest first, as the server keeps them)
    readonly property var history: server.trackedNotifications.values
    readonly property int count: history.length

    // when each notification arrived (by id), for "5m ago"
    property var stamps: ({})
    function ago(n) {
        const t = n ? stamps[n.id] : 0
        if (!t) return ""
        const s = Math.floor((clock.date - t) / 1000)
        return s < 60 ? "now" : s < 3600 ? Math.floor(s / 60) + "m" : s < 86400 ? Math.floor(s / 3600) + "h" : Math.floor(s / 86400) + "d"
    }
    SystemClock { id: clock; precision: SystemClock.Minutes }

    // Settings / `kusanagi msg notifs test`: a pretend popup (no D-Bus) to try styles and positions
    property int testCount: 0
    function test() {
        testCount++
        const n = { id: -testCount, summary: "Kusanagi", appName: "Kusanagi", appIcon: "", image: "",
                    body: ["This is how your notifications look.", "Swipe right or right-click to dismiss.", "Hover to pause the countdown."][testCount % 3],
                    urgency: 1, actions: [], dismiss: () => {}, invokeDefault: () => {} }
        const st = Object.assign({}, stamps); st[n.id] = Date.now(); stamps = st
        popups = [n].concat(popups.filter(x => x)).slice(0, Config.notifications.max)
    }
    function removePopup(n) {
        popups = popups.filter(x => x && x !== n)
    }

    function clearAll() {
        const all = server.trackedNotifications.values.slice()
        for (let i = 0; i < all.length; ++i) all[i].dismiss()
        popups = []
    }

    // default action if the app offered one, then close it
    function activate(n) {
        if (!n) return
        const acts = n.actions || []
        for (let i = 0; i < acts.length; ++i) {
            if (acts[i].identifier === "default") { acts[i].invoke(); break }
        }
        n.dismiss()
    }

    NotificationServer {
        id: server
        keepOnReload: true
        persistenceSupported: true
        bodySupported: true
        bodyMarkupSupported: true
        actionsSupported: true
        imageSupported: true

        onNotification: n => {
            n.tracked = true
            const st = Object.assign({}, root.stamps); st[n.id] = Date.now(); root.stamps = st
            const critical = n.urgency === NotificationUrgency.Critical
            // quiet while DND or gaming, unless it's critical; it still lands in the history
            if (critical || (!root.dnd && !(GameMode.active && Config.gamemode.dnd)))
                root.popups = [n].concat(root.popups.filter(x => x)).slice(0, Config.notifications.max)
        }
    }
}
