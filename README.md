<p align="center"><img src="assets/logo.svg" width="160" alt="Kusanagi logo: a K run through by a katana"></p>

# Kusanagi 草薙

sig's desktop shell for Wayland (MangoWM, Hyprland and niri): wallpaper, bar, control panel, launcher,
notifications, OSD, lock screen, clipboard, wallpaper picker, settings — one program, configured from
its own Settings app. Built on [Quickshell](https://quickshell.org) (QML), started by the `kusanagi` command.

## Use

```
kusanagi                     start (or: kusanagi start)
kusanagi restart | stop | status
kusanagi msg <target> <fn>   talk to it — e.g. kusanagi msg launcher toggle (see: kusanagi ipc)
kusanagi preset next         cycle looks · kusanagi preset list · kusanagi preset minimal
kusanagi settings [page]     open Settings
kusanagi log [-f]            logs
kusanagi doctor              check dependencies
```

Keybinds call `kusanagi msg …` — Super+Space launcher, Super+A wallpapers, Super+V clipboard,
Super+N inbox, Super+I settings, Super+L lock, Super+G game mode, Super+` power menu,
Print / Super+Shift+S screenshots, Super+Shift+C colour picker.

## Layout

```
bin/kusanagi     the command
shell/           the shell itself (QML) — shell.qml is the entry point
assets/logo.svg  the logo
lib/palette.py   wallpaper → colours
install.sh       installs deps + compositors, enables services, wires it up
```

Settings live in `~/.config/kusanagi/settings.json` (edited live by the Settings app, or by hand).
Your saved looks: `~/.config/kusanagi/presets.json`.

## Install

```
./install.sh                              the TUI: compositors → your keybinds → the plan → install
./install.sh --dry-run                    walk through it, change nothing
./install.sh --plain                      plain output instead of the TUI
./install.sh -y --compositors=niri,hyprland
./install.sh --no-packages | --no-services | --no-config
./install.sh --uninstall                  remove links + includes (settings stay)
./install.sh --print=niri                 just print the autostart + keys (mango | hyprland | niri)
```

- **Distros:** Void (xbps), Arch and friends — Artix, CachyOS, EndeavourOS, Manjaro, Garuda (pacman,
  AUR via paru/yay), Fedora (dnf + COPR), Gentoo (emerge + GURU), Debian/Ubuntu (apt), openSUSE (zypper).
- **Inits:** systemd, runit, OpenRC, dinit, s6 — it enables dbus, elogind/seatd and NetworkManager
  only if they exist and aren't already on (on Artix it pulls the `-<init>` service packages).
- **Keybinds:** got a config already? Keep your binds and pick free keys for Kusanagi in the TUI
  (clashes are blocked), or let Kusanagi's keys win (mango: clashing lines get commented out and
  `--uninstall` restores them; Hyprland: `unbind`; niri: included last), or add no keys at all.
- **Compositors:** installs whichever of MangoWM, Hyprland and niri you pick if missing, then adds one
  include line to each config (backup first, validated, reverted if invalid). Already-wired configs are left alone.

Logout, workspaces, game mode effects and the monitor list adapt to the compositor Kusanagi runs on.

## Memory

About 70 MB idle with everything (a bare Quickshell window is ~42 MB). Panels are kept ready but their
contents only exist while open; the wallpaper is kept as a GPU texture only; jemalloc is told to return
freed memory immediately (see `bin/kusanagi`).
