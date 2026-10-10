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

## Cutting a release

1. Bump `VERSION`, note it in `CHANGELOG.md`, commit.
2. Tag and push: `git tag v$(cat VERSION) && git push origin main v$(cat VERSION)`.
3. `.github/workflows/release.yml` builds `kusanagi-<version>-x86_64.tar.zst` in an `archlinux` container
   with `packaging/release.sh` and attaches it and its `.sha256` to the tag's GitHub release (creating
   the release if there isn't one yet).
4. In `packaging/aur/kusanagi-bin/PKGBUILD`: set `pkgver`, reset `pkgrel=1`, put the `.sha256` value in
   `sha256sums_x86_64` (or run `updpkgsums`), then `makepkg --printsrcinfo > .SRCINFO`. Test with
   `makepkg -si` in a clean chroot or at least a clean checkout.
5. Push to the AUR (once: an AUR account with your SSH key, then
   `git clone ssh://aur@aur.archlinux.org/kusanagi-bin.git`): copy `PKGBUILD`, `.SRCINFO` and
   `kusanagi.install` into that clone, commit, push. `kusanagi-git` only needs a push when its
   `PKGBUILD` changes.

`packaging/release.sh [outdir]` also works locally (output in `./dist` by default). A binary only runs
against the libraries it was built with, so the tarball for Arch has to come from Arch (the CI job); one
built elsewhere is only good for that distro. `KUSANAGI_LTO=0` turns link-time optimization off and
`KUSANAGI_BUILD_DIR=<dir>` keeps the build for a quicker second run.
