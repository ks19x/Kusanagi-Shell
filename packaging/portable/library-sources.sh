#!/bin/bash
# Put the corresponding sources beside the binary download.
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
version=$(cat "$root/VERSION")
bundle="$root/dist/kusanagi-$version-linux-x86_64"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/library-sources/ubuntu" "$work/library-sources/upstream"
# Ubuntu's container enables binary repositories only by default.
sed -i 's/^Types: deb$/Types: deb deb-src/' /etc/apt/sources.list.d/ubuntu.sources
apt-get update
python3 - "$bundle/usr/share/licenses/kusanagi/bundled/packages.json" <<'PY' > "$work/packages"
import json, sys
packages = json.load(open(sys.argv[1]))
for name, version in sorted({(p['source'], p['source_version']) for p in packages}):
    print(f'{name}={version}')
PY
cd "$work/library-sources/ubuntu"
while IFS= read -r package; do
  apt-get source --download-only "$package"
done < "$work/packages"
cp "$work/packages" ../ubuntu-packages.txt
cd ../upstream
curl -fL --retry 3 https://github.com/Kistler-Group/sdbus-cpp/archive/refs/tags/v2.1.0.tar.gz -o sdbus-cpp-2.1.0.tar.gz
curl -fL --retry 3 https://github.com/PipeWire/wireplumber/archive/refs/tags/0.5.8.tar.gz -o wireplumber-0.5.8.tar.gz
curl -fL --retry 3 https://github.com/nothings/stb/archive/2c980bb59875b0d32144a71867fbdebb2f77cd20.tar.gz -o stb.tar.gz
cp -a "$root/packaging/portable" "$work/library-sources/build-scripts"
cp "$root/packaging/README.md" "$work/library-sources/README.md"
XZ_OPT=-1 tar -C "$work" -cJf "$root/dist/kusanagi-$version-library-sources.tar.xz" library-sources
(cd "$root/dist" && sha256sum ./*.tar.xz > SHA256SUMS)
