#!/bin/bash
# Run as an unprivileged user on Arch after packaging/release.sh.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(realpath "${1:-$ROOT/dist}")
VER=$(cat "$ROOT/VERSION")
ARCH=$(uname -m)
ARCHIVE="kusanagi-$VER-$ARCH.tar.zst"
test "$ARCH" = x86_64
test "$(id -u)" != 0 || { echo 'Run package-arch.sh as a regular user.' >&2; exit 1; }
test -f "$OUT/$ARCHIVE"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
RECIPE="$WORK/kusanagi-bin"
mkdir "$RECIPE"
cp "$ROOT/packaging/aur/kusanagi-bin/PKGBUILD" "$ROOT/packaging/aur/kusanagi-bin/kusanagi.install" "$RECIPE/"
SUM=$(sha256sum "$OUT/$ARCHIVE" | cut -d ' ' -f1)
sed -i -e "s/^pkgver=.*/pkgver=$VER/" \
  -e "s/^sha256sums_x86_64=.*/sha256sums_x86_64=('$SUM')/" "$RECIPE/PKGBUILD"
cd "$RECIPE"
makepkg --printsrcinfo > .SRCINFO
# Cache the freshly built archive: its public release URL does not exist yet.
cp "$OUT/$ARCHIVE" .
# This only repackages usr/. The separate CI install job checks runtime dependencies.
PKGDEST="$OUT" makepkg --nodeps --noconfirm
tar -C "$WORK" -czf "$OUT/kusanagi-bin-$VER-recipe.tar.gz" \
  kusanagi-bin/PKGBUILD kusanagi-bin/.SRCINFO kusanagi-bin/kusanagi.install
cd "$OUT"
sha256sum -- *.tar.zst *.tar.gz > SHA256SUMS
