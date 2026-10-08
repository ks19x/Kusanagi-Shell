# Changelog

## Unreleased
- **Setup wizard**: opens by itself the first time Kusanagi starts (no settings.json yet) — look, wallpaper,
  bar, lock & idle, extras, login screen, the keys to remember. Any time: `kusanagi setup`, the launcher
  ("setup"), Settings → About
- **Launcher**: `:` emoji & symbols (bundled list, works offline on any distro — Enter copies, Shift+Enter
  types it with wtype), `/` files in your home (fd / fdfind / find), `?` web search (pick the engine in
  Settings → Launcher), and Kusanagi's own commands in every search ("lock", "replay", "settings bar",
  "preset zen", "doctor"…), with web search as the last result
- **Idle**: lock, screens off and suspend after N minutes (Settings → Lock & power → When you're away) —
  replaces hypridle / swayidle on mango, Hyprland, niri and any wlroots compositor (wlopm). Apps that
  inhibit idle, Caffeine, game mode and playing media keep it awake; a heads-up pill 10 s before locking
- **Recording**: record, replay buffer (save the last N seconds) and streaming via `kusanagi record` —
  gsr-ui when it runs, gpu-screen-recorder (also its Flatpak) otherwise, wf-recorder as a fallback.
  Bar module `recorder`, control panel tiles Record / Replay buffer / Save clip, Settings → Recording
- **Password prompts**: a built-in polkit agent in Kusanagi's style (GParted, mounting disks, pkexec…);
  steps aside when another agent runs. Settings → Lock & power
- **`kusanagi doctor`** rewritten: required / recommended / optional with what each enables, session checks
  (D-Bus, PipeWire, polkit, a second polkit agent / idle daemon / notification daemon, portal, login
  screen, last session log) and the exact install command for your distro
- **Updates**: `kusanagi updates [count|list|upgrade]` — xbps, pacman (+ AUR via paru/yay), apt, dnf,
  zypper, apk, eix, plus Flatpak; no root. Bar module `updates`, control panel tile, Settings → Updates
- `lib/distro.sh` (`kusanagi distro …`): one package table for the installer, doctor and Settings;
  Settings → Storage cleans the package cache / unneeded packages / old kernels on every distro, not just Void
- Installer: adds an emoji font and polkit (+ enables polkitd where the init needs it); the banner showed
  the distro's VERSION instead of Kusanagi's on Ubuntu / Fedora — fixed
- **Login screen** (greetd greeter): your lock screen design — or its own — with a user and session
  picker (Hyprland / MangoWM / niri …), reboot and power off. `kusanagi greeter install` (sudo once:
  cage, /var/lib/kusanagi-greeter, /usr/local/bin/kusanagi-greeter, greetd's config with the old one
  kept), `sync` (automatic once installed), `preview`, `status`, `uninstall`. If it can't start, the
  previous greeter takes over; sessions start through your login shell on their own D-Bus.
  Settings → Login screen. Sessions start through `kusanagi session` (own D-Bus, a runtime folder if
  the login didn't make one, a log in ~/.local/state/kusanagi/sessions, one retry when the compositor
  dies at once); the login screen remembers the session you used last
- **Lock screen designs** (Settings → Lock & power): Centered, Card, Split, Minimal, Stacked, Terminal
- **Power menu styles**: Row, Tiles, List (a corner menu), Fullscreen, Pill — letters pick too (L E S R P)
- **Share a look**: export everything a preset holds to `~/kusanagi-looks/<name>.kusanagi` or copy it
  as text; import from those folders, ~/Downloads or pasted text, with a preview first. Looks that
  run commands (custom bar modules, shell click actions) are listed before anything applies, with
  "Apply without its commands"; `kusanagi look export <name>` / `kusanagi look import <file>`
  (the CLI refuses looks with commands)
- Control panel **Sound** tab: switch output / input device in one click, their volumes, per-app volume
  sliders. The bar's volume module opens it (middle-click = Sound settings)
- Control panel **Net** tab: connection + live speeds graph, Wi-Fi list / connect / on-off (when there is a
  Wi-Fi card), Mullvad on/off with location, which DNS resolver answers. The bar's network module opens it
- **Caffeine**: panel tile, `kusanagi msg caffeine toggle|on|off`; a coffee cup in the bar while the screen
  is kept awake (idle inhibitor), click it to turn off
- **Weather** card on Home (wttr.in, cached, refreshed every 30 min only while the panel is open; 3-day
  forecast); location + units in Settings → Panel
- **Control panel, your way**: panel looks (Default / Compact / Icon grid / Dashboard); tiles as
  cards, pills or icons with any column count; thick or slim sliders; big / compact / hidden header;
  tab row on or off; Home sections in any order, each on or off; presets carry all of it
- **Bar editor drag and drop**: drag modules (or islands) in the preview, into and out of islands
- **Motion presets** (Instant … Cinematic, live demos) and a **font picker** over every installed font
- **8 new bar templates + presets**: Aurora, Material You, Candy, Line, Notch, Windows 11, Cyber, Zen
- **Settings** restyled: plain section titles over soft cards, icon tiles in the sidebar, page icons
- Fixed: notifications looping (popups rebuilt on every change), tray icons pushed off-screen,
  Settings building the Presets page on every opening; preset / template previews are now cached
  pictures (~/.cache/kusanagi/previews) instead of live bars (Settings ~half the memory while open)
- **Dock**: pinned apps (Settings → Bar → Dock & taskbar: app search, "pin my most used", running
  dots / line, grow on hover; right-click a dock icon to pin). Icons that really load (letter tile
  when an app has none), the Kusanagi logo in Settings, About, the dock and as a launcher button
- **Settings**: Bar page in tabs (Layout · Templates · Dock & taskbar); click a module in the editor's
  preview to edit it; options in plain-language folds; a sentence under every page title; visual
  pickers (little drawings) for every design choice below
- **Designs to choose from**: launcher Card / Spotlight / Fullscreen grid / Side panel; control panel
  Grow from clock / Drop down / Float / Side sheet; notifications Comfortable / Compact / Minimal
  pills / Accent edge, at any top or bottom corner or centre; OSD Pill / Minimal / Box (macOS-like),
  top / bottom / left / right; `kusanagi msg notifs test` (also a button) shows a pretend popup
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
