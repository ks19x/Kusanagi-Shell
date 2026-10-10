# The native shell

`native/` is Kusanagi as one C++23 Wayland client, `kusanagi-shell`: wallpaper, bars, control panel,
launcher, notifications, OSD, lock screen, polkit dialog, clipboard, wallpaper picker, Settings and Setup,
and the greetd login screen (`kusanagi-shell --greeter`). The `kusanagi` command starts it, and it is
configured by `~/.config/kusanagi/settings.json`.

## Using it

`install.sh` builds it with `lib/build-native.sh` (meson, installed to `~/.local/bin/kusanagi-shell` with
assets in `~/.local/share/kusanagi/assets`).
After pulling a new version, rebuild with `sh lib/build-native.sh` and run `kusanagi restart`.

Packages install it system-wide instead (`-Druntime=true`, see `packaging/README.md`): `kusanagi-shell` in
`/usr/bin`, its assets in `/usr/share/kusanagi/assets` (found next to the binary, so a `--destdir` staging
tree runs as is), and the `kusanagi` command with what it runs in `/usr/lib/kusanagi`, linked from
`/usr/bin/kusanagi`. That tree is read-only; per-user state stays in the XDG folders.

Keybinds call `kusanagi msg <target> <fn>`, which the CLI translates to the native IPC
(`kusanagi-shell msg <command> [args]`). `kusanagi settings` and `kusanagi setup` open the shell's own
Settings and Setup windows (IPC `kusanagi-settings`, `kusanagi-setup`). On mango with `blur_layer=1`, the
shell's layer names need a noblur rule (`kusanagi doctor` prints it), otherwise open panels show the blurred
wallpaper instead of your windows.

## Settings flow

`settings.json` is the source of truth. `src/config/kusanagi_import.*` turns it, `colors.json` and the
`wallpaper` file into the shell's internal config table at load time and on every change; the palette and
accent are written to `~/.config/kusanagi/palettes/kusanagi.json`. Hand-written `*.toml` files in
`~/.config/kusanagi/` and the `~/.local/state/kusanagi/settings.toml` sidecar override the imported values,
which is useful for options the Settings app doesn't expose. `kusanagi-shell config validate` checks them.

Kusanagi's look lives in `src/shell/kusanagi/` (bar engine, control panel morph, styles,
`kusanagi_style.h` for the live settings and colour tokens). Features Kusanagi doesn't use stay off in the
import: screen corners, hot corners, desktop widgets, overview, switcher, dock, backdrop, theme templates.

## Code layout

| Path | What's there |
|---|---|
| `src/main.cpp`, `src/app/` | startup, the event loop, wiring of services and surfaces, IPC registration |
| `src/core/`, `src/util/`, `src/time/` | logging, files and paths, processes, timers, small helpers |
| `src/wayland/`, `src/compositors/` | Wayland connection, layer-shell surfaces, per-compositor backends (mango, Hyprland, niri, sway and more) |
| `src/render/`, `src/ui/` | GLES renderer, scene graph, text (Pango/Cairo), controls and layout |
| `src/config/` | config types, schema, merge and overrides, `kusanagi_import.*` (settings.json) |
| `src/dbus/`, `src/pipewire/`, `src/system/`, `src/idle/`, `src/auth/`, `src/security/` | system services: NetworkManager, BlueZ, UPower, MPRIS, tray, notifications, polkit, audio, brightness, weather and location, idle, PAM, Secret Service |
| `src/shell/` | surfaces: bar, panels, launcher, lock screen, notifications, OSD, wallpaper, greeter, settings |
| `src/shell/kusanagi/` | Kusanagi's own surfaces and IPC: control panel, Settings and Setup (`settings/`), presets, bar templates, game mode, power menu, wallpaper picker |
| `src/launcher/` | launcher providers (`kusanagi_provider.*` is the main search) |
| `src/theme/`, `src/i18n/`, `src/scripting/` | palettes, translations, Luau scripting |
| `assets/` | fonts, translations, templates, `kusanagi-logo.svg` (installed to `~/.local/share/kusanagi/assets`) |
| `protocols/`, `third_party/` | Wayland protocol XML, bundled libraries (with their licenses) |
| `tests/` | unit tests (`meson setup -Dtests=enabled`, then `meson test`) |
| `tools/kdev/` | development helpers, below |

The services that reach the network are the weather (Open-Meteo), location lookup (IP geolocation via
ipwho.is, address geocoding via Open-Meteo, only when configured) and the optional external IP display
(api.ipify.org). Offline mode turns all of them off.

## Building and testing

Build dependencies: `sh lib/distro.sh pkgname shell-build` names the packages for your distro. See
`native/BUILDING.md`.

- `native/tools/kdev/kb` builds `native/build` (one build at a time, safe to run from several shells).
- `native/tools/kdev/kt <slot> up|shot|msg|log|down` runs a private headless mango with the native
  shell and a copy of your settings. Nothing touches the real desktop.
- `native/tools/kdev/kin <slot> click|key|type|scroll ...` injects input into a `kt` slot.

`KUSANAGI_IDLE_PROFILE=1` logs what wakes the shell every 10 s. `KUSANAGI_ASSETS_DIR` points the shell at
an uninstalled assets folder (the `kusanagi` command sets it when running from a checkout).

## Compositors

The shell picks a backend from the environment (`native/src/compositors/compositor_detect.cpp`): mango,
Hyprland, niri and sway over their IPC; labwc (`LABWC_PID`) and KDE Plasma (KWin, via `XDG_CURRENT_DESKTOP`)
through ext-workspace, KWin's virtual desktops and a KWin script for the active window (which also reports
fullscreen for game mode); dwl through dwl-ipc when the build has it, detected by `KUSANAGI_DWL_PID` (exported
by the start script install.sh writes) or `XDG_CURRENT_DESKTOP=dwl`. Compositor-specific parts of Kusanagi
itself live in `native/src/shell/kusanagi/wm_layout.*` (name, config file, monitor list, gaps and borders for
mango, Hyprland and sway), game mode effects (mango, Hyprland) and logout (KDE through org.kde.Shutdown, dwl
by SIGTERM to its PID). Without ext-session-lock the lock falls back to swaylock or hyprlock.

## Status

| Surface | State |
|---|---|
| settings.json, colors.json and wallpaper translated to config, live reload | done |
| Bar: every module (formats, icons, states, when, actions, built-in tooltips), groups and islands, caps, gradients, lines, workspaces, all templates | done |
| Wallpaper: transitions, fill, parallax, dim, slideshow | done (grow is disc, blinds is stripes, blur and slide map to fade and wipe) |
| OSD, notifications (StyledText bodies), screenshot card, media popup, tray menu, tooltips | done |
| Control panel (all tabs, BT pairing card, inline Wi-Fi password) | done |
| Launcher, clipboard, wallpaper picker, power menu | done |
| Lock screen (6 styles), polkit dialog | done |
| Game mode, idle (held off in game mode and while media plays) | done |
| Settings (all 19 pages), Setup wizard (first run too) | done, `src/shell/kusanagi/settings/`, see docs/native-settings.md |
| Greeter | `kusanagi-shell --greeter` in cage, falling back to greetd's previous greeter if it can't start. Login screen auto-sync done. Launchers from before 0.3.0 (QML login screen) keep working until `kusanagi greeter install` replaces them |
| Recorder, updates, presets, bar layouts, idle | done, native IPC in `src/shell/kusanagi/kusanagi_ipc.cpp`. Presets and bar templates are in `presets.*` and `bar_templates.*`; `kusanagi-shell preset` works without a running shell |
| Installer, doctor, CLI | done |
