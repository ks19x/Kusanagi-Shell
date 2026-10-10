<p align="center"><img src="assets/logo.svg" width="160" alt="Kusanagi logo: a K run through by a katana"></p>

# Kusanagi 草薙

sig's desktop shell for Wayland (MangoWM, Hyprland, niri, sway, labwc, KDE Plasma's KWin and dwl): wallpaper, bar, control panel, launcher,
notifications, OSD, lock screen, clipboard, wallpaper picker, settings — one program, configured from
its own Settings app. One native C++ binary (`kusanagi-shell`), started by the `kusanagi` command.

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
kusanagi install             the installer again (installed from a package: just your keybinds/autostart)
```

The launcher does more than apps: `=` calculates, `>` runs a command, `:` finds emoji and symbols,
`/` finds files, `?` searches the web — and typing "lock", "replay" or "settings bar" finds Kusanagi
itself. Kusanagi also locks and blanks the screen when you're away (no hypridle needed) and asks for
admin passwords itself (a polkit agent).

Keybinds call `kusanagi msg …` — Super+Space launcher, Super+A wallpapers, Super+V clipboard,
Super+N inbox, Super+I settings, Super+L lock, Super+G game mode, Super+` power menu,
Print / Super+Shift+S screenshots, Super+Shift+C colour picker.

## Native shell

`native/` is Kusanagi as one C++ binary, `kusanagi-shell`, and the default engine: the installer builds it
(`sh lib/build-native.sh` rebuilds it after an update; the build packages come from
`sh lib/distro.sh pkgname shell-build`). The original QML shell (`shell/`, on Quickshell) still ships as
`kusanagi engine qml`. How the native shell is laid out and how to work on it: [docs/native.md](docs/native.md).
Third-party notices are in `native/THIRD_PARTY_LICENSES`.

## Layout

```
bin/kusanagi     the command
native/          the native shell (C++, kusanagi-shell)
shell/           the QML shell — shell.qml is the entry point
assets/logo.svg  the logo
lib/palette.py   wallpaper → colours
install.sh       installs deps + compositors, enables services, wires it up
packaging/       release tarball script, AUR packages
```

Settings live in `~/.config/kusanagi/settings.json` (edited live by the Settings app, or by hand).
Your saved looks: `~/.config/kusanagi/presets.json`.

## Install

**Start here: [full installation guide](docs/install.md)** — downloads, first run, compositor
setup, optional features, updates, troubleshooting and uninstalling.

**Arch Linux, x86_64:** download the `kusanagi-bin-<version>-<pkgrel>-x86_64.pkg.tar.zst`
package and `SHA256SUMS` from [GitHub Releases](https://github.com/ks19x/Kusanagi-Shell/releases).
In the download folder, verify and install (replace the filename with the one you downloaded):

```sh
sha256sum --ignore-missing -c SHA256SUMS
sudo pacman -Syu
sudo pacman -U ./kusanagi-bin-<version>-<pkgrel>-x86_64.pkg.tar.zst
kusanagi
```

Check that the package is reported as `OK` before installing. Run `kusanagi` as your regular user
in a terminal inside your Wayland session. It sets up compositor autostart and keybinds, then opens
the shell's setup wizard. `kusanagi doctor` explains missing optional features.

**AUR publication is pending.** You do not need an AUR account or helper to use the GitHub package.
The release also includes a checksum-pinned recipe for `makepkg -si`; see the guide.

**Other distributions and ARM:** build from source using the installer. The prebuilt download is
built against Arch Linux libraries and is not a universal Linux binary.

```sh
git clone https://github.com/ks19x/Kusanagi-Shell.git
cd Kusanagi-Shell
./install.sh --dry-run
./install.sh
```

The installer presents compositor and keybind choices before making changes. Run it as your regular
user; it requests elevated privileges for system packages and services when needed.

**From source** (a git checkout; builds `kusanagi-shell` into `~/.local`):

```
./install.sh                              the TUI: compositors → your keybinds → the plan → install
./install.sh --dry-run                    walk through it, change nothing
./install.sh --plain                      plain output instead of the TUI
./install.sh -y --compositors=niri,hyprland
./install.sh --no-packages | --no-services | --no-config
./install.sh --uninstall                  remove links + includes (settings stay)
./install.sh --print=niri                 just print the autostart + keys (mango | hyprland | niri |
                                          sway | labwc | kde | dwl)
./install.sh --no-build                   skip building kusanagi-shell at the end
./install.sh --user                       only your part: compositors + keybinds (what a package runs)
```

- **Distros:** Void (xbps), Arch and friends — Artix, CachyOS, EndeavourOS, Manjaro, Garuda (pacman,
  AUR via paru/yay), Fedora (dnf + COPR), Gentoo (emerge + GURU), Debian/Ubuntu (apt), openSUSE (zypper).
- **Inits:** systemd, runit, OpenRC, dinit, s6 — it enables dbus, elogind/seatd and NetworkManager
  only if they exist and aren't already on (on Artix it pulls the `-<init>` service packages).
- **Keybinds:** got a config already? Keep your binds and pick free keys for Kusanagi in the TUI
  (clashes are blocked), or let Kusanagi's keys win (mango: clashing lines get commented out and
  `--uninstall` restores them; Hyprland: `unbind`; niri, sway, labwc: Kusanagi's keys come last; KDE:
  KDE's own shortcut on that key is cleared, backup kept), or add no keys at all. Volume, mic, brightness
  and media keys go to Kusanagi wherever they're free.
- **Compositors:** installs whichever of MangoWM, Hyprland, niri, sway, labwc, KDE Plasma and dwl you pick
  if missing, then wires Kusanagi in (backup first, validated where the compositor can check, reverted if
  invalid). Already-wired configs are left alone.
  - mango, Hyprland, niri, sway: `kusanagi.<conf|lua|kdl>` + one include line (sway: starts your config
    from `/etc/sway/config` if you have none).
  - labwc: rc.xml has no include, so the keys go in a marked `<!-- kusanagi:begin … end -->` block at the
    end of `<keyboard>` (must parse as XML or rc.xml isn't touched), and `kusanagi &` into `autostart`.
  - KDE Plasma: `~/.config/autostart/kusanagi.desktop`, and each key as a command shortcut
    (`~/.local/share/applications/kusanagi-key-*.desktop` + its `[services]` group in
    `kglobalshortcutsrc`) — active from your next Plasma login. plasmashell keeps drawing its own panel;
    to run Kusanagi alone, leave plasmashell out (systemd: `systemctl --user mask plasma-plasmashell`).
  - dwl: keys are compiled in, so it writes `~/.config/dwl/kusanagi.h` — add
    `#include "/home/you/.config/dwl/kusanagi.h"` inside `keys[]` in your `config.h` and rebuild — and
    `~/.config/dwl/kusanagi.sh`, which starts dwl with Kusanagi (start your session with it). dwl is built
    from source on most distros.

Logout, workspaces, game mode effects and the monitor list adapt to the compositor Kusanagi runs on.
Gaps and borders from Settings → Display → Windows work on mango, Hyprland and sway (hidden elsewhere);
game mode turns compositor effects off on mango and Hyprland, and follows fullscreen windows everywhere
the compositor reports them (wlr foreign-toplevel: mango, Hyprland, niri, sway, labwc; KWin's own
scripting on KDE; dwl only with its IPC/foreign-toplevel patches). Screenshots and the colour picker
use Spectacle / KWin's picker on KDE.

## Memory

About 70 MB idle with everything (a bare Quickshell window is ~42 MB). Panels are kept ready but their
contents only exist while open; the wallpaper is kept as a GPU texture only; jemalloc is told to return
freed memory immediately, and Qt draws through Vulkan when there's a hardware Vulkan driver — the same
pixels as OpenGL for ~11 MB less RAM and ~25% less CPU while things animate (it falls back to OpenGL by
itself if Vulkan doesn't work; `KUSANAGI_RENDERER=opengl` forces it; see `bin/kusanagi`).

## License

MIT, see [LICENSE](LICENSE). Bundled third-party code keeps its own notices: see
[native/THIRD_PARTY_LICENSES](native/THIRD_PARTY_LICENSES).
