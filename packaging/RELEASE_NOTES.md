## Linux download

Download `kusanagi-<version>-linux-x86_64.tar.xz` and `SHA256SUMS` from this release.
Verify the archive before extracting it:

```sh
sha256sum --ignore-missing -c SHA256SUMS
```

Extract it somewhere permanent, then run `./install` followed by `./kusanagi` from that folder.
The installer adds links in `~/.local/bin`; first launch handles compositor setup and appearance.
No root access or AUR helper is needed.

Requires x86_64 Linux, glibc 2.39 or newer, a Wayland session and working EGL/GLES drivers.
Application libraries are included. Graphics drivers, PAM and desktop services come from your system.
Other architectures, musl systems and older distributions should build from source.

[Installation, updates and removal](https://github.com/ks19x/Kusanagi-Shell/blob/main/docs/install.md) ·
[Changelog](https://github.com/ks19x/Kusanagi-Shell/blob/main/CHANGELOG.md) ·
[Report a bug](https://github.com/ks19x/Kusanagi-Shell/issues)

The release pipeline checks this archive on Ubuntu 24.04, Debian 13, Fedora 43 and Arch.
These are startup and library checks; desktop behavior also depends on your compositor and services.

`kusanagi-<version>-library-sources.tar.xz` contains sources for the bundled libraries.
You do not need it to install or run Kusanagi.
