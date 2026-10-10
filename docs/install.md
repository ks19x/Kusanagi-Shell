# Installing Kusanagi

Kusanagi is a desktop shell for a Wayland session: bar, launcher, notifications, wallpaper,
control panel and lock screen. You still need a Wayland compositor and working graphics drivers.
Run Kusanagi as your regular user, inside your graphical session.

- [Arch Linux: prebuilt package](#arch-linux-prebuilt-package)
- [Arch Linux: package recipe](#arch-linux-package-recipe)
- [Build from source](#build-from-source)
- [First launch and compositor setup](#first-launch-and-compositor-setup)
- [Optional features](#optional-features)
- [Daily commands](#daily-commands)
- [Updates](#updates)
- [Troubleshooting](#troubleshooting)
- [Uninstall](#uninstall)

## Arch Linux: prebuilt package

The GitHub release targets **up-to-date Arch Linux on x86_64**. Arch derivatives may ship different
library versions; if dependencies cannot be resolved, use a source build against your distribution.
Do not install this package on Void, Debian, Ubuntu, Fedora or other non-Arch distributions.

1. Open [GitHub Releases](https://github.com/ks19x/Kusanagi-Shell/releases) and select a release.
2. Download its `kusanagi-bin-<version>-<pkgrel>-x86_64.pkg.tar.zst` and `SHA256SUMS` into the same folder.
3. In a terminal in that folder, verify the download:

   ```sh
   sha256sum --ignore-missing -c SHA256SUMS
   ```

   The downloaded package must be listed with `OK`. If verification fails or no file is verified,
   download both files again from the same release before proceeding. The checksum detects a damaged
   or mismatched download; it is not a separate publisher signature.

4. Update your Arch system, then install the package. Replace the example filename with the actual
   downloaded filename (for the first release: `kusanagi-bin-0.2.0-1-x86_64.pkg.tar.zst`):

   ```sh
   sudo pacman -Syu
   sudo pacman -U ./kusanagi-bin-0.2.0-1-x86_64.pkg.tar.zst
   ```

   Pacman installs required dependencies from your configured repositories and tracks installed files
   for updates and removal. The package is unsigned; normal local-file pacman settings accept this.
   If your system requires signed local packages, follow your administrator's policy rather than
   disabling signature checks globally.

5. Continue with [first launch](#first-launch-and-compositor-setup).

No AUR account, `yay`, or `paru` is required. AUR publication is pending.

### Which release file do I need?

| Download | Purpose |
| --- | --- |
| `kusanagi-bin-…-x86_64.pkg.tar.zst` | Recommended: install with pacman |
| `kusanagi-bin-…-recipe.tar.gz` | PKGBUILD, generated .SRCINFO and install message, for makepkg or future AUR submission |
| `kusanagi-…-x86_64.tar.zst` | Staged `usr/` tree for packagers; not a pacman package |
| `SHA256SUMS` | Checksums for all three downloads |
| `kusanagi-…-x86_64.tar.zst.sha256` | Checksum for the raw binary archive |
| GitHub's “Source code” archives | Source only; these are not the prebuilt application |

Use pacman instead of copying the raw archive into `/usr` by hand.

## Arch Linux: package recipe

Use this if you prefer to inspect the package instructions and repackage the release yourself.
It downloads the prebuilt archive and verifies its pinned SHA-256 checksum; it does not compile C++.

Download `kusanagi-bin-0.2.0-recipe.tar.gz` and `SHA256SUMS` from the same release, then:

```sh
sha256sum --ignore-missing -c SHA256SUMS
sudo pacman -Syu --needed base-devel
mkdir kusanagi-package
tar -xzf kusanagi-bin-0.2.0-recipe.tar.gz -C kusanagi-package
cd kusanagi-package/kusanagi-bin
less PKGBUILD
less kusanagi.install
makepkg -si
```

Substitute the version you downloaded. Run `makepkg` as a regular user, not with sudo. It requests
privileges to install dependencies and the resulting package. Use the recipe attached to the release:
the repository's recipe is a template whose checksum is filled during the release build.

## Build from source

Install Git using your distribution's package manager, then:

```sh
git clone https://github.com/ks19x/Kusanagi-Shell.git
cd Kusanagi-Shell
./install.sh --dry-run
./install.sh
```

For a specific released version, run `git checkout v0.2.0` before the installer (substitute your
chosen tag). Otherwise the checkout follows the development branch.

The installer can install dependencies and selected compositors, enable relevant services, configure
autostart and keybinds, and build the native shell under `~/.local`. Review its plan before applying.
Keep the checkout: the local `kusanagi` command links to it. Make sure `~/.local/bin` is in your PATH.

The installer has package mappings for Void, Arch, Fedora, Gentoo, Debian/Ubuntu and openSUSE, with
support for several init systems. This does not mean every distribution release has sufficiently
recent libraries or a C++23 compiler. See [native build requirements](../native/BUILDING.md).

Useful installer options:

```sh
./install.sh --plain          # plain terminal interface
./install.sh --no-services    # manage services yourself
./install.sh --no-config      # leave compositor configuration to you
./install.sh --no-packages    # dependencies are already installed
./install.sh --print=niri     # inspect generated compositor commands
```

For maintainers creating distribution packages, see [packaging](../packaging/README.md).

## First launch and compositor setup

Inside your Wayland session, open a terminal and run:

```sh
kusanagi
```

On a fresh packaged install this runs per-user setup: select your compositors, choose keybinds and
configure autostart. It then starts the shell and opens the setup wizard for appearance, wallpaper,
bar and other preferences. Users with existing settings may skip automatic setup; open it manually:

```sh
kusanagi install     # compositor autostart and keybinds
kusanagi setup       # appearance and feature wizard
kusanagi doctor      # required and optional dependencies, session diagnostics
```

Packaged per-user setup does not install a compositor or enable system services. Install and start
a suitable Wayland session first. Do not run the shell with sudo.

| Compositor | Setup behavior |
| --- | --- |
| MangoWM, Hyprland, niri, sway | Adds a Kusanagi config file and an include in your compositor config |
| labwc | Adds a marked keyboard block and an autostart entry |
| KDE Plasma (Wayland) | Adds desktop autostart and command shortcuts; log out and in for shortcuts |
| dwl | Generates a header for compiled keybinds and a session script; integrate the header and rebuild dwl |

The installer offers choices for conflicting keybinds and backs up edited configuration files.
You can inspect generated commands with `kusanagi install --print=niri` (or `mango`, `hyprland`,
`sway`, `labwc`, `kde`, `dwl`). Avoid adding a second autostart if one already launches Kusanagi.
Plasma keeps its own panel running; decide which panel you want before changing Plasma's startup.

Default bindings include Super+Space (launcher), Super+I (settings), Super+V (clipboard),
Super+N (notifications) and Super+L (lock). Your selected bindings may differ.

## Optional features

`kusanagi doctor` is the best starting point: it explains missing programs and the install command
for your distribution. On Arch, `pacman -Qi kusanagi-bin` also lists optional dependencies.

| Feature | Components |
| --- | --- |
| Audio controls | PipeWire and WirePlumber running in your session |
| Wi-Fi/network controls | NetworkManager with its service running |
| Bluetooth | BlueZ, bluetoothctl; python-gobject for pairing prompts |
| Portals/file pickers | A desktop portal backend appropriate to your compositor |
| Night light | gammastep, where supported by your compositor |
| Recording/replays | gpu-screen-recorder; wf-recorder offers basic recording |
| External-monitor brightness | ddcutil and access to the monitor's I²C device |
| Screenshot editing | swappy |
| Emoji typing | wtype, where supported; noto-fonts-emoji for rendering |
| Login screen | greetd and cage; optional, separate from the desktop shell |

Some components may come from AUR or other distribution sources. Install only the features you need.
For features with a setup helper:

```sh
kusanagi bluetooth setup
kusanagi brightness setup
```

The login screen changes system login configuration and is entirely optional. Get your desktop
session working before trying `kusanagi greeter install`. Use `kusanagi greeter uninstall` to undo it.

## Daily commands

```sh
kusanagi start
kusanagi status
kusanagi restart
kusanagi stop
kusanagi settings
kusanagi msg launcher toggle
kusanagi preset list
kusanagi log -f
```

Settings live in `~/.config/kusanagi/settings.json`, saved presets in
`~/.config/kusanagi/presets.json`. Back up this directory before major upgrades. These locations
follow `XDG_CONFIG_HOME` if set. See the [bar guide](bar.md) for layouts and custom modules.

## Updates

**GitHub package:** download the new package and matching `SHA256SUMS`, verify them, run
`sudo pacman -Syu`, then `sudo pacman -U ./<new-package-filename>` and `kusanagi restart`.
A manually downloaded package does not automatically receive new Kusanagi releases through
`pacman -Syu`; check GitHub Releases until an AUR package or package repository is available.

**Recipe:** download the new release's recipe and repeat `makepkg -si` in a fresh folder.

**Source checkout on main:** inside the checkout, run `git pull --ff-only`, then `./install.sh`
and `kusanagi restart`. If you checked out a release tag, fetch tags and select the newer tag instead.
Resolve your own source modifications before switching versions.

## Troubleshooting

Start with:

```sh
kusanagi doctor
kusanagi status
kusanagi log
```

- **Command not found:** packaged installs use `/usr/bin/kusanagi`; source installs use
  `~/.local/bin/kusanagi`. Check `command -v kusanagi` and your PATH. A previous source installation
  can take precedence over the system package; use `/usr/bin/kusanagi` to check.
- **A shared library is missing:** on Arch, perform a full `sudo pacman -Syu` and use a current
  package. Arch library updates can require a new Kusanagi build. On another distribution, build
  from source. Do not symlink unrelated library versions together.
- **Shell exits immediately:** run it inside a Wayland graphical session. Check
  `echo "$XDG_SESSION_TYPE"` and `echo "$WAYLAND_DISPLAY"`, then inspect `kusanagi log`.
- **No setup or shortcuts:** run `kusanagi install` in a terminal. Existing settings can skip the
  automatic first-run flow. Reload your compositor or log out and in after configuration changes.
- **Duplicate bars or notifications:** check your compositor autostart for an existing panel or
  notification daemon and choose which program should provide that feature.
- **No audio, network or Bluetooth:** installing a library does not start the corresponding
  service. Use `kusanagi doctor` and your distribution's service/session setup.

When [reporting a bug](https://github.com/ks19x/Kusanagi-Shell/issues), include the release version,
distribution, compositor, relevant doctor output and logs. Review logs for private information first.
CI checks package installation and command startup; it does not test a full graphical desktop session.

## Uninstall

Remove your per-user compositor integration **before** removing the package:

```sh
kusanagi stop
kusanagi install --uninstall
sudo pacman -R kusanagi-bin
```

Run the first two commands separately for each user who configured Kusanagi. If you installed the
optional greeter, run `kusanagi greeter uninstall` before removing the program as well.
For dwl, remove the generated header include from your `config.h` and rebuild.

For a source installation, run `./install.sh --uninstall` from the original checkout instead of
`pacman -R`. Settings are kept for future use. Installer backups use `.bak-kusanagi-<timestamp>`;
review those if you want to restore earlier compositor configuration or overridden shortcuts.
