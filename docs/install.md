# Install Kusanagi

Kusanagi runs inside a Wayland session. You need a compositor first: MangoWM, Hyprland,
niri, sway or labwc are the usual choices. KDE Plasma and dwl have additional setup notes below.

## Portable download

Get `kusanagi-0.2.0-linux-x86_64.tar.xz` and `SHA256SUMS` from the same
[GitHub release](https://github.com/ks19x/Kusanagi-Shell/releases). Substitute the version
below if you downloaded a newer one.

In the download folder:

```sh
sha256sum --ignore-missing -c SHA256SUMS
```

Check that the archive is listed with `OK`. Then extract it into a permanent location:

```sh
mkdir -p ~/.local/opt
tar -xJf kusanagi-0.2.0-linux-x86_64.tar.xz -C ~/.local/opt
cd ~/.local/opt/kusanagi-0.2.0-linux-x86_64
./install
./kusanagi
```

`install` adds `kusanagi` and `kusanagi-shell` links in `~/.local/bin`. It refuses to overwrite
an existing installation. Keep the extracted folder: those links point into it. Make sure
`~/.local/bin` is on your PATH; log out and back in if you have just added it.

You can also run `./kusanagi` directly without adding the links. Run it as your regular user,
not with sudo. Nothing needs to be copied into `/usr`.

### What systems does it run on?

The download is for **x86_64 Linux with glibc 2.39 or newer**. Check with:

```sh
uname -m
getconf GNU_LIBC_VERSION
```

It is built on Ubuntu 24.04 and includes the libraries used by the application. The release
workflow checks that the same archive starts on Ubuntu 24.04, Debian 13, Fedora 43 and current
Arch Linux, including after moving it to a path containing spaces. Those are CLI and library
checks, not a guarantee that every desktop feature works on each distribution.

Your system still supplies:

- A Wayland compositor and EGL/GLES graphics drivers.
- PAM for authentication and the normal system D-Bus service.
- A user D-Bus session, fonts and standard command-line tools.
- Services such as PipeWire, NetworkManager and BlueZ for their respective features.

The bundle does not require systemd. It is intended to work on compatible glibc-based systems,
including Void glibc. Alpine, Void musl, older glibc systems and ARM need a source build.
This is a portable download, not a claim that one executable works on every Linux installation.

If the loader reports `libEGL.so.1`, `libGLESv2.so.2` or `libpam.so.0` missing, install your
distribution’s graphics or PAM runtime package. Use your distribution’s GPU setup instructions;
the bundle does not replace the graphics driver.

## First launch

Open a terminal inside your Wayland session and run:

```sh
kusanagi
```

Choose which compositors should start Kusanagi and which shortcuts to use. The installer
backs up configuration files it changes and offers choices when a key is already taken.
The shell then opens its appearance and feature setup.

If you already have Kusanagi settings, automatic setup may be skipped. You can reopen either part:

```sh
kusanagi install     # autostart and keybinds
kusanagi setup       # appearance and features
```

Packaged and portable setup only handles your user configuration. It does not install a
compositor or enable system services. The source installer can do those additional steps.

### Compositor notes

| Session | What gets added |
| --- | --- |
| MangoWM, Hyprland, niri, sway | A Kusanagi config file and an include in the main config |
| labwc | A marked keyboard block and an autostart entry |
| KDE Plasma Wayland | Desktop autostart and command shortcuts; log in again to activate them |
| dwl | A keybinding header and session script; add the header to your dwl config and rebuild |

Plasma’s own panel keeps running unless you disable it yourself. For dwl, the installer prints
the integration instructions because keybindings are compiled into the compositor.

To inspect the generated setup without applying it:

```sh
kusanagi install --print=niri
```

Replace `niri` with `mango`, `hyprland`, `sway`, `labwc`, `kde` or `dwl`.

## Optional features

Run `kusanagi doctor` after setup. It checks the session, required tools and optional features,
and prints distribution-specific package commands.

| Feature | What it uses |
| --- | --- |
| Sound controls | PipeWire and a session manager such as WirePlumber |
| Wi-Fi and networking | NetworkManager |
| Bluetooth | BlueZ and bluetoothctl; python-gobject for pairing prompts |
| Clipboard | wl-clipboard and cliphist |
| Screenshots | grim and slurp; swappy for editing |
| Recording | gpu-screen-recorder, or wf-recorder for basic recording |
| External monitor brightness | ddcutil and access to the monitor’s I²C device |
| Night light | gammastep, where supported |
| Emoji | An emoji font; wtype for typing instead of copying |

Most desktop sessions already provide D-Bus, fonts, audio and a portal backend. Install the
remaining tools you actually want. The library bundle does not install or start these services.

`kusanagi bluetooth setup` and `kusanagi brightness setup` can help with those features.

The greetd login screen is separate from the desktop shell. Get the desktop working first,
then see `kusanagi greeter install`. That command changes system login configuration and
requires elevated privileges. `kusanagi greeter uninstall` restores the previous setup.

## Building from source

Install Git, then:

```sh
git clone https://github.com/ks19x/Kusanagi-Shell.git
cd Kusanagi-Shell
./install.sh --dry-run
./install.sh
```

The installer presents its plan before making changes. It can install dependencies and selected
compositors, configure services and shortcuts, then build the native shell under `~/.local`.
Keep the checkout because the command links to files inside it.

Useful options:

```sh
./install.sh --plain
./install.sh --no-services
./install.sh --no-config
./install.sh --no-packages
```

The installer has package mappings for Void, Arch, Fedora, Gentoo, Debian/Ubuntu and openSUSE.
Older distribution releases may not have the necessary compiler or library versions. See the
[build requirements](../native/BUILDING.md) for manual builds.

To build a particular release, run `git checkout v0.2.0` before the installer, replacing the tag
with the version you want.

### Arch and AUR

You can use the portable download on Arch just like on other compatible distributions.
AUR publication is pending. The repository also contains native Arch package recipes for
maintainers; see [packaging](../packaging/README.md). Do not expect `yay -S kusanagi-bin` to work
ahead of publication.

## Updating

For a portable installation:

1. Download and verify the new archive.
2. Run `kusanagi stop`.
3. Run `kusanagi install --uninstall` to remove the old autostart entries and shortcuts.
4. Remove the two links you created in `~/.local/bin` (`kusanagi` and `kusanagi-shell`).
5. Extract the new version, run its `./install`, then `kusanagi install` and `kusanagi`.

Your settings are kept. Once the new version works, you can delete the old extracted folder.
Kusanagi does not silently replace its own download; get updates from GitHub Releases.

For a source checkout on `main`, run `git pull --ff-only`, `./install.sh`, then `kusanagi restart`.
If you checked out a release tag, fetch and select the newer tag instead. Keep local source changes
committed or backed up before switching versions.

## When something goes wrong

Start with:

```sh
kusanagi doctor
kusanagi status
kusanagi log
```

**Command not found:** check `~/.local/bin` is on PATH, or run the extracted folder’s `./kusanagi`.
`command -v kusanagi` shows which installation your terminal is using.

**Missing library or GLIBC version:** check the requirements above. Do not fix library errors by
symlinking unrelated versions together. Build from source if your system is older than the bundle.

**Starts and immediately exits:** check `echo "$WAYLAND_DISPLAY"` inside your graphical session,
then read the log. A terminal over SSH is not normally attached to your Wayland session.

**Shortcuts do nothing:** rerun `kusanagi install`, then reload the compositor or log out and in.
Check that the extracted folder has not moved since you configured autostart.

**Duplicate panels or notifications:** check compositor autostart for another bar or notification
daemon, then choose which one should provide the feature.

**Audio or Bluetooth is missing:** the service must be running, not just installed. Check
`kusanagi doctor` and your distribution’s service setup.

**Lock screen:** authentication uses your system’s PAM configuration. Test locking and unlocking
manually before enabling idle locking on a new setup.

For a bug report, include your distribution, compositor, Kusanagi version and relevant log output.
[Open an issue here](https://github.com/ks19x/Kusanagi-Shell/issues). Check logs for private information
before posting them.

## Removing it

If you enabled the greetd login screen, run `kusanagi greeter uninstall` first.

For the portable download:

```sh
kusanagi stop
kusanagi install --uninstall
```

Then remove the two Kusanagi links you created in `~/.local/bin` and the extracted folder.
Do not remove links belonging to a different installation. Each user should remove their own
compositor integration. On dwl, remove the generated header include and rebuild.

For a source install, run `./install.sh --uninstall` from its checkout. For an Arch package,
remove the per-user integration first, then run `sudo pacman -R kusanagi-bin`.

Settings remain in `~/.config/kusanagi/` (`XDG_CONFIG_HOME` if set). Installer backups are named
`.bak-kusanagi-<timestamp>` beside the original configuration files.
