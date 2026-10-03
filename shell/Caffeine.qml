pragma Singleton
// Caffeine.qml — keep the screen awake: while active, the bar holds an idle inhibitor
// (Bar.qml), so the compositor won't blank, dim or lock. Session-only on purpose: a restart
// never leaves the machine stuck awake. IPC: caffeine toggle | on | off
import Quickshell
import QtQuick

Singleton {
    property bool active: false
    function toggle() { active = !active }
}
