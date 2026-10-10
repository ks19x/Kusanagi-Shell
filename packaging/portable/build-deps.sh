#!/bin/bash
# Build dependencies on Ubuntu 24.04. Run in a disposable build container.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  ca-certificates git curl build-essential g++-14 ccache cmake meson ninja-build pkg-config patchelf \
  python3 file xz-utils zstd libwayland-dev wayland-protocols libegl-dev libgles-dev \
  libfreetype-dev libfontconfig-dev libcairo2-dev libpango1.0-dev librsvg2-dev \
  libxkbcommon-dev libglib2.0-dev libsecret-1-dev libsodium-dev libsystemd-dev \
  libpipewire-0.3-dev libspa-0.2-dev libspa-0.2-modules libpipewire-0.3-modules pipewire-bin \
  libpam0g-dev libpolkit-agent-1-dev libcurl4-gnutls-dev libwebp-dev libjxl-dev \
  libsndfile1-dev libqalculate-dev libqalculate-data libxml2-dev libmd4c-dev libtomlplusplus-dev \
  nlohmann-json3-dev libjemalloc-dev libical-dev liblua5.4-dev

export CC=gcc-14 CXX=g++-14
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
# Noble's sdbus-c++ and WirePlumber predate the APIs used by Kusanagi.
git clone --quiet --depth 1 --branch v2.1.0 https://github.com/Kistler-Group/sdbus-cpp.git
cmake -S sdbus-cpp -B sdbus-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_INSTALL_LIBDIR=lib -DSDBUSCPP_BUILD_DOCS=OFF
cmake --build sdbus-build --parallel 2
cmake --install sdbus-build
install -Dm644 sdbus-cpp/COPYING /usr/local/share/kusanagi-bundled-licenses/sdbus-cpp/COPYING
install -Dm644 sdbus-cpp/COPYING-LGPL-Exception /usr/local/share/kusanagi-bundled-licenses/sdbus-cpp/COPYING-LGPL-Exception

git clone --quiet --depth 1 --branch 0.5.8 https://github.com/PipeWire/wireplumber.git
meson setup wp-build wireplumber --prefix=/usr/local --libdir=lib --buildtype=release \
  -Ddaemon=false -Dtools=false -Dtests=false -Ddoc=disabled -Dintrospection=disabled \
  -Dsystem-lua=true -Dsystemd=disabled -Delogind=disabled
meson compile -C wp-build -j 2
meson install -C wp-build
install -Dm644 wireplumber/LICENSE /usr/local/share/kusanagi-bundled-licenses/wireplumber/LICENSE

# Ubuntu's stb package does not yet contain stb_image_resize2.h.
stb_revision=2c980bb59875b0d32144a71867fbdebb2f77cd20
mkdir -p /usr/local/include/stb /usr/local/share/kusanagi-bundled-licenses/stb
for header in stb_image_resize2.h stb_image_write.h; do
  curl --fail --location --retry 3 "https://raw.githubusercontent.com/nothings/stb/$stb_revision/$header" \
    -o "/usr/local/include/stb/$header"
done
curl --fail --location --retry 3 "https://raw.githubusercontent.com/nothings/stb/$stb_revision/LICENSE" \
  -o /usr/local/share/kusanagi-bundled-licenses/stb/LICENSE
ldconfig
