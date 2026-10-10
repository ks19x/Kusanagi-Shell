# Changelog

## 0.3.0 (unreleased)

Kusanagi is now a native C++ program (`kusanagi-shell`) instead of a Quickshell config. It reads the
same `settings.json`, keeps the same look and is driven by the same `kusanagi` command, but starts
faster and uses less memory. The QML version is gone, and Quickshell is no longer needed;
`kusanagi engine` only answers `native` now, for old scripts.

If the login screen was set up with an earlier version, run `kusanagi greeter install` once to
switch it to the native one. Until then the old launcher keeps working as before.

New:

- Portable Linux download that runs on most current distros without building anything.
- Support for sway, labwc, KDE Plasma and dwl, next to MangoWM, Hyprland and niri. The installer
  writes autostart and keybinds for each of them.
- Login screen for greetd, using your lock screen design, with user and session pickers.
- Bluetooth: pair, connect and forget devices from the control panel or Settings, including
  devices that ask for a PIN or code.
- Brightness for laptop screens and external monitors (DDC/CI through ddcutil).
- Idle handling: lock, screen off and suspend after a set time. Video, games and Caffeine keep
  the screen on.
- Screen recording, replay buffer and streaming through gpu-screen-recorder or wf-recorder.
- Polkit password prompts in Kusanagi's own style.
- Package updates in the bar and control panel for xbps, pacman, apt, dnf, zypper, apk and Flatpak.
- Launcher prefixes: `:` for emoji, `/` for files, `?` for web search. Kusanagi's own commands
  ("lock", "settings bar", "preset zen") show up in normal searches.
- Lock screen designs (Centered, Card, Split, Minimal, Stacked, Terminal) and power menu styles.
- Looks can be exported to a file and shared. Imported looks that run commands ask first.
- The bar is fully configurable: any number of bars on any edge, groups, 26 module types, custom
  command modules, powerline caps, gradients, states and click actions. Settings has a live
  editor with drag and drop and 20 templates.
- Control panel tabs for sound, network and Bluetooth, a weather card, and layout options.
- Settings has 19 pages and a setup wizard that opens on first start.
- Window gaps and borders from Settings on MangoWM, Hyprland and sway.

Fixed:

- Notification popups no longer loop or rebuild on every change.
- Tray icons no longer get pushed off screen.
- Open panels no longer hide your windows on MangoWM with layer blur on.

## 0.2.0 (2026-10-03)

- Runs on niri and Hyprland (Lua and classic config) as well as MangoWM: workspaces, game mode
  effects, logout and the monitor list work the same on all three.
- New installer: a terminal UI that installs dependencies and compositors for Void, Arch, Fedora,
  Gentoo, Debian/Ubuntu and openSUSE, enables services for systemd, runit, OpenRC, dinit and s6,
  and adds one validated include line per compositor. It reads your existing keybinds and avoids
  clashes. `--dry-run` and `--uninstall` are there too.
- Logo: a K run through by a katana.
- Power menu (lock, log out, suspend, reboot, shut down).
- Presets trimmed to 10.

## 0.1.0 (2026-10-03)

First release as its own program (before this it was a Quickshell config in
`~/.config/quickshell/kusanagi`).

- The `kusanagi` command: start, stop, restart, status, msg, preset, settings, log, edit, doctor.
- Wallpapers with transitions, parallax and slideshow.
- Bar styles (islands, solid, floating, clear), top or bottom, window title, media popover, tray.
- Workspace styles: pills, dots, numbers, roman numerals, kanji, dwl blocks, custom icons.
- Control panel, launcher (calculator, run, app actions), clipboard, wallpaper picker,
  notifications, OSD, screenshot card and lock screen.
- Presets, saved looks, 10 colour palettes and game mode.
- Settings app with 15 pages.
