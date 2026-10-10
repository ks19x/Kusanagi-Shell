# Building kusanagi-shell

`install.sh` builds and installs the native shell for you. To do it by hand:

```sh
# the build packages for your distro (Void, Arch, Fedora, Debian/Ubuntu, ...)
sh lib/distro.sh pkgname shell-build

# configure (once), build and install to ~/.local
sh lib/build-native.sh
```

`lib/build-native.sh` runs meson with `--prefix=$HOME/.local --buildtype=release -Dtests=disabled` in
`native/build` (or `$KUSANAGI_BUILD_DIR`), builds with half the cores and installs `kusanagi-shell` to
`~/.local/bin` and its assets to `~/.local/share/kusanagi/assets`. Afterwards, `kusanagi restart`.

## Requirements

- A C++23 compiler (GCC 14 or newer, or a recent Clang), meson 1.1+, ninja, pkg-config.
- The libraries `distro.sh pkgname shell-build` lists: Wayland, EGL/GLES, Cairo, Pango, HarfBuzz, FreeType,
  Fontconfig, librsvg, libxkbcommon, GLib, sdbus-c++, libsecret, libsodium, PipeWire and WirePlumber,
  polkit, PAM, libcurl, libwebp, libjxl, libsndfile, libqalculate, libxml2, libical, md4c, toml++,
  nlohmann/json, stb, and optionally jemalloc.
- Luau, fzy, Wuffs and Material Color Utilities are bundled in `third_party/`.

## Options

| Option | Default | Meaning |
|---|---|---|
| `-Dtests=enabled\|disabled\|auto` | `auto` | unit tests (`auto`: on for unsanitized debug builds) |
| `-Djemalloc=enabled\|disabled\|auto` | `auto` | link jemalloc on glibc builds |
| `-Dnative_optimizations=true` | `false` | optimize for the build machine's CPU (not portable) |

## Development builds

```sh
meson setup native/build native --prefix=$HOME/.local --buildtype=release -Dtests=disabled
native/tools/kdev/kb                       # ninja -C native/build, one build at a time

meson setup native/build-tests native -Dtests=enabled
meson test -C native/build-tests
```

Run an uninstalled build against the checkout's assets with `KUSANAGI_ASSETS_DIR=native/assets`, or in a
private headless compositor with `native/tools/kdev/kt` (see docs/native.md).
