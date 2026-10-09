<p align="center"><img src="assets/logo.svg" width="160" alt="Kusanagi logo: a K run through by a katana"></p>

# Kusanagi 草薙

sig's desktop shell for Wayland (MangoWM, Hyprland and niri): wallpaper, bar, control panel, launcher,
notifications, OSD, lock screen, clipboard, wallpaper picker, settings — one program, configured from
its own Settings app. Built on [Quickshell](https://quickshell.org) (QML), started by the `kusanagi` command.

## The bar is yours

The bar is a layout engine, not a fixed design: any number of bars on any edge (top, bottom, left,
right), full-width, floating, docked or sized to fit; groups and modules in any order; colours,
gradients, borders, per-corner radius, powerline caps, underlines; formats with `{values}`, icons by
level, states (`warning`, `muted`, `charging`…) that restyle anything; clicks and scrolls bound to
built-in actions or shell commands; your own modules from any command (text or waybar-style JSON).
Settings → Bar → Custom layout edits it live; 12 templates (powerline, dwm, polybar, macOS, GNOME,
sidebar, dock, taskbar…) are one click or `kusanagi msg bar template <name>` away.
Format reference: [docs/bar.md](docs/bar.md).

## Use

```
kusanagi                     start (or: kusanagi start)
kusanagi restart | stop | status
kusanagi msg <target> <fn>   talk to it — e.g. kusanagi msg launcher toggle (see: kusanagi ipc)
kusanagi preset next         cycle looks · kusanagi preset list · kusanagi preset minimal
kusanagi settings [page]     open Settings
kusanagi log [-f]            logs
kusanagi doctor              check dependencies — and the exact install command for your distro
kusanagi setup               the setup wizard (opens by itself on the first start)
kusanagi updates [upgrade]   waiting package updates (any distro + Flatpak) · install them
kusanagi record replay|save|record|stream|stop   screen recording and the replay buffer
kusanagi brightness up|down|<percent>|setup      screen brightness (laptop panel, monitors over DDC/CI)
kusanagi bluetooth toggle|status|setup           Bluetooth
kusanagi greeter install     Kusanagi as your login screen (greetd)
```

The launcher does more than apps: `=` calculates, `>` runs a command, `:` finds emoji and symbols,
`/` finds files, `?` searches the web — and typing "lock", "replay" or "settings bar" finds Kusanagi
itself. Kusanagi also locks and blanks the screen when you're away (no hypridle needed) and asks for
admin passwords itself (a polkit agent).

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
freed memory immediately, and Qt draws through Vulkan when there's a hardware Vulkan driver — the same
pixels as OpenGL for ~11 MB less RAM and ~25% less CPU while things animate (it falls back to OpenGL by
itself if Vulkan doesn't work; `KUSANAGI_RENDERER=opengl` forces it; see `bin/kusanagi`).
