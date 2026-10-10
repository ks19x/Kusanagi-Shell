# Packaging Kusanagi

## What gets installed

One `meson install` puts the whole program under a prefix when it's configured with `-Druntime=true`:

| Path | What |
|---|---|
| `<prefix>/bin/kusanagi-shell` | the shell |
| `<prefix>/share/kusanagi/assets` | its fonts, translations, templates |
| `<prefix>/lib/kusanagi/` | the `kusanagi` command (`bin/`), `lib/`, `scripts/`, the QML engine (`shell/`), `assets/`, `install.sh`, `VERSION` |
| `<prefix>/bin/kusanagi` | a link to `../lib/kusanagi/bin/kusanagi` |
| `<prefix>/share/licenses/kusanagi/` | the license files (bundled libraries, icon font, derived code) |

Nothing writes into those folders at run time. Each user's settings, colours, caches and logs live in
`~/.config/kusanagi`, `~/.cache/kusanagi` and `~/.local/state/kusanagi`.

For any distro:

```sh
meson setup build native --prefix=/usr --buildtype=release -Dtests=disabled -Druntime=true
meson compile -C build
meson install -C build --destdir "$pkgdir"
```

Build dependencies: `sh lib/distro.sh pkgname shell-build`, or the list in `native/BUILDING.md`. The
runtime dependencies are those libraries plus python3, wl-clipboard, cliphist, grim, slurp, ImageMagick,
notify-send and a Nerd Font; `kusanagi doctor` checks the rest on a user's system.

## First run

A package has no per-user step, so `kusanagi` does it: the first time a user starts it from a terminal it
runs `install.sh --user` (compositor autostart and keybinds; no packages, services or build), then starts
the shell, which opens its Setup because there's no `settings.json` yet. Started without a terminal it
starts the shell anyway and shows a notification asking for one `kusanagi` in a terminal.
`install.sh` leaves `~/.config/kusanagi/.installed`; users who already have a `settings.json` count as set
up. `kusanagi install` runs the installer again.

## GitHub releases (no AUR account needed)

The release workflow builds on Arch Linux x86_64, then installs the resulting package in a separate,
fresh Arch container. It verifies all checksums, package file integrity, shared-library resolution
and `kusanagi-shell --help`. A tagged release is published only after those checks pass.
This is an installation smoke test, not a graphical-session test.

Each release includes:

- `kusanagi-<version>-x86_64.tar.zst` and its `.sha256`: staged installation tree.
- `kusanagi-bin-<version>-<pkgrel>-x86_64.pkg.tar.zst`: pacman-installable package.
- `kusanagi-bin-<version>-recipe.tar.gz`: PKGBUILD with the actual archive checksum, generated
  `.SRCINFO`, and `kusanagi.install`.
- `SHA256SUMS`: checksums for all three archives/packages.

`packaging/package-arch.sh [outdir]` creates the pacman package and recipe from the raw archive.
It requires Arch's `makepkg` and must run as an unprivileged user. The temporary recipe uses the
locally built archive before the public release URL exists. It skips build-time dependency checks
because it only copies files; the fresh CI install job resolves and tests runtime dependencies.
The recipe in the source tree is a template; use the checksum-pinned release recipe for distribution.

## Cutting a release

1. Set `VERSION`, update `CHANGELOG.md` and the example version in `docs/install.md`. Set the binary
   PKGBUILD's `pkgver` to match; reset `pkgrel=1` for a new upstream version. Review
   `packaging/RELEASE_NOTES.md` and commit the release changes.
2. Push the branch. Run the `release` workflow manually on that branch in GitHub Actions (or
   `gh workflow run release.yml --ref main`). Manual runs build and test downloadable Actions artifacts
   without creating a public release. Wait for both `arch` and `install` jobs to succeed.
3. Tag the tested commit and push the tag:

   ```sh
   git tag "v$(cat VERSION)"
   git push origin "v$(cat VERSION)"
   ```

4. Wait for the tagged workflow to succeed. It creates the GitHub release with installation notes
   and all assets. Download the public assets and verify `SHA256SUMS` before announcing it.
5. Keep published release archives immutable. For fixes, use a new version/tag rather than replacing
   binaries behind an existing checksum. Arch library transitions can require a new build.

The build uses two compiler jobs and disables LTO to keep memory usage suitable for hosted runners.
CPU-specific optimizations are disabled for distributable binaries.

## Publishing to AUR later

Once you have an AUR account with a registered SSH public key:

```sh
git clone ssh://aur@aur.archlinux.org/kusanagi-bin.git
```

Extract the release's recipe archive somewhere separate, then copy its `PKGBUILD`, `.SRCINFO` and
`kusanagi.install` into the AUR clone. Review the version and checksum, test `makepkg -si` on Arch,
commit those three files and push. Do not commit binary archives to AUR.
The `kusanagi-git` recipe builds the development branch and has a separate AUR repository.

## Local builds

`packaging/release.sh [outdir]` works locally (output in `./dist` by default). A binary only runs
against compatible libraries, so the release for Arch must be built on Arch. A build from Void or
another distribution is not a substitute for the Arch artifact.
`KUSANAGI_LTO=0` turns link-time optimization off; `KUSANAGI_BUILD_DIR=<dir>` keeps the build directory;
`KUSANAGI_JOBS=2` limits compiler concurrency.
