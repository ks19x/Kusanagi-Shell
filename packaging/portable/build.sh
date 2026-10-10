#!/bin/bash
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
version=$(cat "$root/VERSION")
name="kusanagi-$version-linux-x86_64"
out="$root/dist"
build="${KUSANAGI_BUILD_DIR:-$root/build-portable}"
stage="$out/$name"
mkdir -p "$out"
test ! -e "$stage" || { echo "$stage already exists; use a fresh output directory" >&2; exit 1; }
export CC="ccache gcc-14" CXX="ccache g++-14"
meson setup "$build" "$root/native" --prefix=/usr --libdir=lib --buildtype=release \
  -Dtests=disabled -Druntime=true -Dnative_optimizations=false -Djemalloc=enabled -Db_lto=false
meson compile -C "$build" -j "${KUSANAGI_JOBS:-4}"
meson install -C "$build" --destdir "$stage" --strip
python3 "$root/packaging/portable/bundle.py" "$stage"
cp "$root/docs/install.md" "$stage/INSTALL.md"
"$stage/usr/bin/kusanagi-shell" --version
"$stage/kusanagi" version
# A fixed timestamp and ownership keep the archive independent of the build account.
epoch=$(git -C "$root" log -1 --format=%ct)
tar -C "$out" --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$epoch" \
  -cJf "$out/$name.tar.xz" "$name"
(cd "$out" && sha256sum "$name.tar.xz" > SHA256SUMS)
