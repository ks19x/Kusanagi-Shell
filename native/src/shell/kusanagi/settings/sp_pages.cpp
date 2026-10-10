// The Settings page table, and a placeholder for pages without a builder.

#include "shell/kusanagi/settings/sp_pages.h"

#include "shell/kusanagi/settings/sp_kit.h"

namespace kusanagi::sp {

  const std::vector<PageInfo>& pages() {
    static const std::vector<PageInfo> p{
        {"presets", "PERSONALIZE", "Presets", 0xf0e09, "Whole looks in one click — bar, motion, popups and colours together.",
         "looks themes minimal dwl glass zen terminal save", buildPresets},
        {"appearance", "", "Appearance", 0xf03d8, "Colours, font, corners, shadows and how lively animations feel.",
         "theme colour accent font corners shadow animation speed", buildAppearance},
        {"bar", "", "Bar", 0xf04e9, "Build any bar: start from a template, then change every piece.",
         "style islands solid floating modules clock scroll media marquee", buildBar},
        {"workspaces", "", "Workspaces", 0xf0570, "How workspaces look on the bar.", "tags icons pills dots roman kanji glow",
         buildWorkspaces},
        {"panel", "", "Control panel", 0xf056e, "The control panel that drops out of the clock.", "tiles width tab media stats",
         buildPanel},
        {"wallpaper", "", "Wallpaper", 0xf0e09, "Your wallpapers, transitions, parallax and slideshow.",
         "transition parallax slideshow fill dim awww picker", buildWallpaper},
        {"launcher", "", "Launcher & clipboard", 0xf003b, "The app launcher and clipboard history.",
         "apps search clipboard terminal calculator", buildLauncher},
        {"lock", "", "Lock & power", 0xf033e, "The lock screen, locking when you're away, the power menu and password prompts.",
         "hyprlock blur password test idle auto lock screen off dpms suspend sleep away polkit admin authentication", buildLock},
        {"login", "", "Login screen", 0xf0004, "Kusanagi as your login screen — your lock design, with you and your session to pick.",
         "greeter greetd display manager login session user boot", buildLogin},
        {"notifications", "", "Notifications & OSD", 0xf009a, "Popups, do-not-disturb and the volume / mic overlay.",
         "popups dnd osd volume timeout position", buildNotifications},
        {"gamemode", "", "Game mode", 0xf0297, "What happens when a game goes fullscreen.",
         "games fullscreen performance governor feral blur", buildGameMode},
        {"recording", "", "Recording", 0xf044a, "Record the screen, keep a replay buffer, or stream.",
         "record replay clip stream gpu screen recorder gsr wf-recorder video obs", buildRecording},
        {"sound", "SYSTEM", "Sound", 0xf057e, "Outputs, inputs and per-app volume.", "audio volume output input microphone apps",
         buildSound},
        {"bluetooth", "", "Bluetooth", 0xf00af, "Pair and connect headphones, mice, controllers and phones.",
         "bluetooth pair headphones earbuds headset mouse keyboard controller gamepad phone bluez", buildBluetooth},
        {"display", "", "Display", 0xf0379, "Monitors, brightness, window gaps and night light.",
         "monitor brightness dim ddc ddcutil backlight gaps borders windows night light gammastep resolution", buildDisplay},
        {"network", "", "Network", 0xf06f3, "Your connection at a glance.", "ethernet wifi ip speed", buildNetwork},
        {"storage", "", "Storage", 0xf02ca, "Disk space and cleaning up.", "disk xbps pacman apt dnf cache cleanup orphans kernels",
         buildStorage},
        {"updates", "", "Updates", 0xf06b0, "Package updates for your distro (and Flatpak), checked quietly.",
         "packages upgrade xbps pacman apt dnf zypper flatpak aur", buildUpdates},
        {"about", "", "About", 0xf02fd, "Kusanagi, your system and memory use.", "system kusanagi version memory", buildAbout},
    };
    return p;
  }

  void buildPlaceholder(Column& page, const PageInfo& info) {
    auto* g = page.add<Group>("Not in the native Settings yet",
                              std::string("The ") + info.name
                                  + " page hasn't been ported to the native window. Its options still work — set them in "
                                    "settings.json, the shell picks them up live.");
    auto* row = g->add<Flow>(8.0F);
    row->add<Chip>("Edit settings.json", 0xf107b)->onClick([]() {
      spawn({"xdg-open", expandHome("~/.config/kusanagi/settings.json")});
    });
    row->add<Chip>("Open config folder", 0xf024b)->onClick([]() { spawn({"xdg-open", expandHome("~/.config/kusanagi")}); });
  }

} // namespace kusanagi::sp
