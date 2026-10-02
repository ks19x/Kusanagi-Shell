# Kusanagi

sig's desktop shell for Wayland (MangoWM and Hyprland): wallpaper, bar, control panel, launcher,
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
Super+N inbox, Super+I settings, Super+L lock, Super+G game mode.

## Layout

```
bin/kusanagi     the command
shell/           the shell itself (QML) — shell.qml is the entry point
install.sh       links it into ~/.local/bin and ~/.config/quickshell
```

Settings live in `~/.config/kusanagi/settings.json` (edited live by the Settings app, or by hand).
Your saved looks: `~/.config/kusanagi/presets.json`.

## Install

```
./install.sh            # links the command + shell, runs `kusanagi doctor`
./install.sh --uninstall
```

Needs: quickshell, wl-clipboard, cliphist, grim, slurp, ImageMagick, gammastep, gamemode, swappy,
hyprlock, NetworkManager, pavucontrol, foot, a Nerd Font (JetBrainsMono).

## Memory

About 70 MB idle with everything (a bare Quickshell window is ~42 MB). Panels are kept ready but their
contents only exist while open; the wallpaper is kept as a GPU texture only; jemalloc is told to return
freed memory immediately (see `bin/kusanagi`).
