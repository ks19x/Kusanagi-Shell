<p align="center"><img src="assets/logo.svg" width="140" alt="Kusanagi"></p>

# Kusanagi 草薙

A desktop shell for Wayland, written in C++. Bar, launcher, wallpapers, notifications,
control panel and lock screen, with settings you can change without editing a config file.

I build and use it on Void. It works with MangoWM, Hyprland, niri, sway and labwc, with
additional integration for KDE Plasma and dwl. Your compositor still runs the session;
Kusanagi provides the desktop around it.

[Website](https://ks19x.github.io/Kusanagi-Shell/) | [Install](docs/install.md) | [Releases](https://github.com/ks19x/Kusanagi-Shell/releases) |
[Bar configuration](docs/bar.md) | [Changelog](CHANGELOG.md) |
[Issues](https://github.com/ks19x/Kusanagi-Shell/issues)

## Install

The portable Linux download is `kusanagi-<version>-linux-x86_64.tar.xz` on the
[releases page](https://github.com/ks19x/Kusanagi-Shell/releases). Extract it somewhere you
want to keep it, then run:

```sh
./install
./kusanagi
```

`install` adds links in `~/.local/bin`. It does not install system packages or need root.
The first launch walks through compositor autostart, keybinds and appearance.

The bundle targets x86_64 Linux with glibc 2.39 or newer, a Wayland session, and working
EGL/GLES drivers. It carries its application libraries; your system provides graphics,
PAM and desktop services. See the [installation guide](docs/install.md) for checksums,
requirements, updates and removal. AUR publication is still pending.

To build from source:

```sh
git clone https://github.com/ks19x/Kusanagi-Shell.git
cd Kusanagi-Shell
./install.sh
```

The source installer shows its plan before installing dependencies or changing compositor
configuration. `./install.sh --dry-run` previews it.

## What's here

- **Bar:** any edge, multiple bars, floating or full-width, custom modules, click actions
  and live layout editing. Start with a template and change it in Settings.
- **Launcher:** apps, files, calculations, emoji, web searches and shell commands.
- **Control panel:** audio devices, per-app volume, networking, Bluetooth and brightness.
- **Desktop:** wallpaper picker, colour palettes, notifications, clipboard and screenshots.
- **Session:** lock screen, idle handling, power menu and polkit prompts. A greetd login
  screen is optional.

Some features use external programs. `kusanagi doctor` shows what's missing and how to
install it on your distribution.

## Everyday commands

```sh
kusanagi                     # start
kusanagi restart
kusanagi stop
kusanagi settings
kusanagi msg launcher toggle
kusanagi preset list
kusanagi doctor
kusanagi log -f
```

The default launcher shortcut is Super+Space. The installer lets you choose different
bindings and handles clashes with your existing configuration.

In the launcher, `=` calculates, `>` runs a command, `:` searches emoji, `/` searches files
and `?` searches the web. You can also type things like "settings bar" or "lock".

Settings and saved looks live in `~/.config/kusanagi/`. Use the Settings app, or back up and
edit the files yourself. [The bar guide](docs/bar.md) covers formats, modules and layouts.

## Working on it

The native shell is in `native/`; `bin/kusanagi` is the command that starts and controls it.
`lib/` and `scripts/` contain the system helpers.

[Build notes](native/BUILDING.md) | [Architecture](docs/native.md) |
[Contributing](CONTRIBUTING.md) | [Packaging](packaging/README.md) | [Releasing](RELEASING.md)

## License

MIT. See [LICENSE](LICENSE). Bundled code and libraries keep their own licenses;
see [third-party notices](native/THIRD_PARTY_LICENSES) and the notices included in the download.
