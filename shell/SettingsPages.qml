pragma Singleton
// SettingsPages.qml — the Settings sidebar (Settings.qml) and the launcher's "Settings: …" results.
import Quickshell
import QtQuick

Singleton {
    readonly property var pages: [
        { group: "PERSONALIZE", id: "presets", desc: "Whole looks in one click — bar, motion, popups and colours together.", name: "Presets", icon: 0xf0e09, page: "SpPresets", keys: "looks themes minimal dwl glass zen terminal save" },
        { id: "appearance", desc: "Colours, font, corners, shadows and how lively animations feel.", name: "Appearance", icon: 0xf03d8, page: "SpAppearance", keys: "theme colour accent font corners shadow animation speed" },
        { id: "bar", desc: "Build any bar: start from a template, then change every piece.", name: "Bar", icon: 0xf04e9, page: "SpBar", keys: "style islands solid floating modules clock scroll media marquee" },
        { id: "workspaces", desc: "How workspaces look on the bar.", name: "Workspaces", icon: 0xf0570, page: "SpWorkspaces", keys: "tags icons pills dots roman kanji glow" },
        { id: "panel", desc: "The control panel that drops out of the clock.", name: "Control panel", icon: 0xf056e, page: "SpPanel", keys: "tiles width tab media stats" },
        { id: "wallpaper", desc: "Your wallpapers, transitions, parallax and slideshow.", name: "Wallpaper", icon: 0xf0e09, page: "SpWallpaper", keys: "transition parallax slideshow fill dim awww picker" },
        { id: "launcher", desc: "The app launcher and clipboard history.", name: "Launcher & clipboard", icon: 0xf003b, page: "SpLauncher", keys: "apps search clipboard terminal calculator" },
        { id: "lock", desc: "The lock screen, locking when you're away, the power menu and password prompts.", name: "Lock & power", icon: 0xf033e, page: "SpLock", keys: "hyprlock blur password test idle auto lock screen off dpms suspend sleep away polkit admin authentication" },
        { id: "login", desc: "Kusanagi as your login screen — your lock design, with you and your session to pick.", name: "Login screen", icon: 0xf0004, page: "SpLogin", keys: "greeter greetd display manager login session user boot" },
        { id: "notifications", desc: "Popups, do-not-disturb and the volume / mic overlay.", name: "Notifications & OSD", icon: 0xf009a, page: "SpNotifications", keys: "popups dnd osd volume timeout position" },
        { id: "gamemode", desc: "What happens when a game goes fullscreen.", name: "Game mode", icon: 0xf0297, page: "SpGameMode", keys: "games fullscreen performance governor feral blur" },
        { id: "recording", desc: "Record the screen, keep a replay buffer, or stream.", name: "Recording", icon: 0xf044a, page: "SpRecording", keys: "record replay clip stream gpu screen recorder gsr wf-recorder video obs" },
        { group: "SYSTEM", id: "sound", desc: "Outputs, inputs and per-app volume.", name: "Sound", icon: 0xf057e, page: "SpSound", keys: "audio volume output input microphone apps" },
        { id: "bluetooth", desc: "Pair and connect headphones, mice, controllers and phones.", name: "Bluetooth", icon: 0xf00af, page: "SpBluetooth", keys: "bluetooth pair headphones earbuds headset mouse keyboard controller gamepad phone bluez" },
        { id: "display", desc: "Monitors, brightness, window gaps and night light.", name: "Display", icon: 0xf0379, page: "SpDisplay", keys: "monitor brightness dim ddc ddcutil backlight gaps borders windows night light gammastep resolution" },
        { id: "network", desc: "Your connection at a glance.", name: "Network", icon: 0xf06f3, page: "SpNetwork", keys: "ethernet wifi ip speed" },
        { id: "storage", desc: "Disk space and cleaning up.", name: "Storage", icon: 0xf02ca, page: "SpStorage", keys: "disk xbps pacman apt dnf cache cleanup orphans kernels" },
        { id: "updates", desc: "Package updates for your distro (and Flatpak), checked quietly.", name: "Updates", icon: 0xf06b0, page: "SpUpdates", keys: "packages upgrade xbps pacman apt dnf zypper flatpak aur" },
        { id: "about", desc: "Kusanagi, your system and memory use.", name: "About", icon: 0xf02fd, page: "SpAbout", keys: "system kusanagi version memory" }
    ]
}
