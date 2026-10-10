#!/bin/sh
# packaging/release.sh [outdir]: builds the prebuilt release, kusanagi-<version>-<arch>.tar.zst and its
# .sha256, into outdir (default: ./dist). The tarball holds one folder, kusanagi-<version>-<arch>/, with the
# whole program under usr/ exactly as `meson install` lays it out for --prefix=/usr: unpack it into a
# package's root (the AUR's kusanagi-bin does) or copy usr/ over /.
# A binary only runs against the libraries it was built with: build it on the distro you ship it for
# (the GitHub workflow builds the Arch one in an archlinux container).
#   KUSANAGI_LTO=0          build without link-time optimization
#   KUSANAGI_BUILD_DIR=dir  build in dir (kept, so a second run is quick) instead of a temporary one
#   KUSANAGI_JOBS=n         compile jobs (default: meson's, all cores)
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:-$ROOT/dist}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
VER=$(cat "$ROOT/VERSION")
ARCH=$(uname -m)
NAME=kusanagi-$VER-$ARCH

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM
B=${KUSANAGI_BUILD_DIR:-$WORK/build}
STAGE=$WORK/$NAME

# file times from the last commit, so the same commit gives the same tarball
if [ -z "${SOURCE_DATE_EPOCH:-}" ]; then
    SOURCE_DATE_EPOCH=$(git -C "$ROOT" log -1 --format=%ct 2>/dev/null || date +%s)
fi
export SOURCE_DATE_EPOCH

lto=true
[ "${KUSANAGI_LTO:-1}" = 0 ] && lto=false
if [ ! -f "$B/build.ninja" ]; then
    meson setup "$B" "$ROOT/native" --prefix=/usr --buildtype=release -Dtests=disabled -Druntime=true \
        -Djemalloc=auto -Dnative_optimizations=false -Db_lto=$lto
else
    meson configure "$B" -Dprefix=/usr -Dbuildtype=release -Dtests=disabled -Druntime=true -Db_lto=$lto
fi
meson compile -C "$B" ${KUSANAGI_JOBS:+-j "$KUSANAGI_JOBS"}
meson install -C "$B" --destdir "$STAGE" --strip --quiet

# the tarball: sorted, owned by root, one timestamp
find "$STAGE" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
tar -C "$WORK" --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_DATE_EPOCH" \
    --format=gnu -cf - "$NAME" | zstd -q -19 -T0 -o "$OUT/$NAME.tar.zst" -f
(cd "$OUT" && sha256sum "$NAME.tar.zst" > "$NAME.tar.zst.sha256")
echo "$OUT/$NAME.tar.zst"
cat "$OUT/$NAME.tar.zst.sha256"
