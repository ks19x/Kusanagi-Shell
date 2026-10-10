#!/bin/sh
# build-native.sh: builds the native shell (native/, C++) and installs kusanagi-shell and its assets into
# ~/.local. Run by install.sh, and by `kusanagi build` after pulling a new version. Needs the packages
# `distro.sh pkgname shell-build` names (meson, a C++23 compiler and the libraries).
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
B="${KUSANAGI_BUILD_DIR:-$ROOT/native/build}"
command -v meson >/dev/null || { echo "build-native: meson isn't installed (kusanagi doctor)" >&2; exit 1; }
if [ ! -f "$B/build.ninja" ]; then
    meson setup "$B" "$ROOT/native" --prefix="$HOME/.local" --buildtype=release -Dtests=disabled
fi
# half the cores: the build is heavy on RAM and the desktop should stay usable
j=$(( $(nproc 2>/dev/null || echo 2) / 2 )); [ $j -ge 1 ] || j=1
meson compile -C "$B" -j "$j"
meson install -C "$B" --quiet
echo "kusanagi-shell installed in $HOME/.local/bin"
