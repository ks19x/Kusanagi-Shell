# Changelog

## Unreleased
- Control panel **Sound** tab: switch output / input device in one click, their volumes, per-app volume
  sliders. The bar's volume module opens it (middle-click = Sound settings)
- Control panel **Net** tab: connection + live speeds graph, Wi-Fi list / connect / on-off (when there is a
  Wi-Fi card), Mullvad on/off with location, which DNS resolver answers. The bar's network module opens it
- **Caffeine**: panel tile, `kusanagi msg caffeine toggle|on|off`; a coffee cup in the bar while the screen
  is kept awake (idle inhibitor), click it to turn off
- **Weather** card on Home (wttr.in, cached, refreshed every 30 min only while the panel is open; 3-day
  forecast); location + units in Settings → Panel
- **Bar engine**: the bar is now data (`settings.json → "bars"`, docs/bar.md). Any number of bars on
  any edge, any length (full / fraction / px / fit), floating margins, autohide; groups + 26 module
  types (taskbar, battery, disk, mic, weather, notifications, uptime, custom commands with JSON
  output…); style cascade with tokens, gradients, powerline caps, indicator lines; formats, icons by
  level, threshold + built-in states with `when` overrides; actions or shell commands on every
  button/scroll. Settings → Bar: classic options or a live custom-layout editor with real previews;
  12 templates; presets can carry a layout (Powerline, Sidebar, Dock added); `kusanagi msg bar
  template <name> | templates | classic`. Without a custom layout the classic bar is rebuilt from
  the old options, pixel for pixel. Only data sources for modules on a bar run. Cost: ~+4 MB
- **Window gaps / borders** in Settings → Display → Windows (MangoWM, Hyprland): applied live while you
  drag, off = your compositor config decides. Mango keeps it through reloads via `~/.config/kusanagi/mango.conf`
  (the installer now sources it); Hyprland gets it re-applied after every config reload

## 0.2.0 — 2026-10-03
- Runs on niri and Hyprland (Lua and classic config) as well as MangoWM: workspaces, scroll, fullscreen /
  game mode effects, logout and monitor list all go through one compositor layer (`Wm`)
- `install.sh` rewritten: installs dependencies + chosen compositors per distro (Void, Arch/Artix/CachyOS/…,
  Fedora, Gentoo, Debian/Ubuntu, openSUSE), enables services for systemd/runit/OpenRC/dinit/s6,
  wires each compositor with one validated include line; `--dry-run`, `--uninstall`
- `compositors/`: ready autostart + keybinds for each compositor
- Installer is a centred TUI with the logo; per-compositor keybind picker that reads your existing
  binds (mango, Hyprland classic/Lua — live via hyprctl when running — and niri) and blocks clashes
- Logo: a K run through by a katana (assets/logo.svg), shown in Settings → About
- Presets trimmed to 10 (dropped Text only, Retro, Rosé, Monochrome)
- Built-in power menu (lock / log out / suspend / reboot / shut down) via `kusanagi msg power toggle`

## 0.1.0 — 2026-10-03
First release as its own program (was a Quickshell config in ~/.config/quickshell/kusanagi).
- `kusanagi` command: start/stop/restart/status, msg/ipc, preset, settings, log, edit, doctor
- Wallpaper engine with transitions (fade, blur, wipe, grow, slide, zoom, blinds), parallax, slideshow
- Bar: islands / solid / floating / clear, top or bottom, layouts, modules incl. window title, media popover, tray menus
- Workspaces: pills, dots, numbers, roman, kanji, dwl blocks, custom icons
- Control panel (Home / System / Inbox / Quick), launcher (list/grid, calculator, run, app actions),
  clipboard, wallpaper picker, notifications, OSD, screenshot flyout, lock screen (with test mode)
- presets + your own saved looks; 10 palettes; game mode with clean switching
- Settings app with 15 pages
