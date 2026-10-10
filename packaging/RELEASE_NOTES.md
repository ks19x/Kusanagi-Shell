## Install on Arch Linux (x86_64)

Download the `kusanagi-bin-…-x86_64.pkg.tar.zst` package and `SHA256SUMS` below into the same folder.
Verify that your package is reported as `OK`, then install it (substitute the downloaded filename):

```sh
sha256sum --ignore-missing -c SHA256SUMS
sudo pacman -Syu
sudo pacman -U ./kusanagi-bin-<version>-<pkgrel>-x86_64.pkg.tar.zst
kusanagi
```

Run `kusanagi` in a terminal inside your Wayland session as your normal user. First run configures
compositor autostart and keybinds, then opens the setup wizard. `kusanagi doctor` checks your setup.

**No AUR account or helper required.** AUR publication is pending. Download updates from GitHub and
install them with pacman; this does not add an automatic-update repository.

- [Full installation, update and uninstall guide](https://github.com/ks19x/Kusanagi-Shell/blob/main/docs/install.md)
- [Changelog](https://github.com/ks19x/Kusanagi-Shell/blob/main/CHANGELOG.md)
- [Report a problem](https://github.com/ks19x/Kusanagi-Shell/issues)

### Other downloads

- `kusanagi-bin-…-recipe.tar.gz`: checksum-pinned PKGBUILD and metadata for `makepkg -si`.
- `kusanagi-…-x86_64.tar.zst`: raw installation tree for packagers, not a pacman package.
- `SHA256SUMS`: checksums of all release archives and packages, not a publisher signature.
- GitHub's source archives: source code, not the prebuilt application.

The binary targets current Arch Linux x86_64. Other distributions and architectures should build
from source using the installation guide. CI checks installation, library resolution and CLI startup
in a fresh Arch container; graphical-session behavior still depends on your compositor and drivers.
