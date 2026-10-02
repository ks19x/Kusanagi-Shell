// Shortcuts.qml — answers Hyprland's `hl.dsp.global("kusanagi:<name>")` binds (keybinds.lua):
// media keys, session menu, control panel, screenshots. Mango binds call the IPC instead.
import Quickshell
import Quickshell.Hyprland
import Quickshell.Services.Mpris
import QtQuick

Scope {
    id: root
    required property var shell

    readonly property MprisPlayer player: {
        const ps = Mpris.players.values
        return ps.find(p => p.isPlaying) ?? ps[0] ?? null
    }
    readonly property string scripts: Quickshell.env("HOME") + "/.config/mango/scripts"

    component Key: GlobalShortcut { appid: "kusanagi" }

    Key { name: "mediaToggle"; onPressed: if (root.player) root.player.togglePlaying() }
    Key { name: "mediaNext"; onPressed: if (root.player) root.player.next() }
    Key { name: "mediaPrev"; onPressed: if (root.player) root.player.previous() }
    Key { name: "mediaStop"; onPressed: if (root.player) root.player.stop() }
    Key { name: "session"; onPressed: Quickshell.execDetached([Quickshell.env("HOME") + "/.config/rices/zei/wlogout/wlogout.sh"]) }
    Key { name: "showall"; onPressed: root.shell.panelTab(0) }
    Key { name: "screenshot"; onPressed: Quickshell.execDetached([root.scripts + "/screenshot", "region"]) }
    Key { name: "screenshotFreeze"; onPressed: Quickshell.execDetached([root.scripts + "/screenshot", "region"]) }
}
