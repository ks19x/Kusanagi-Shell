# Packaging

## Portable Linux release

`release.yml` builds on Ubuntu 24.04 using GCC 14. The archive includes the application’s shared
libraries and audio modules, but leaves glibc, PAM and EGL/GLES drivers to the host. The minimum
host glibc is 2.39. No `LD_LIBRARY_PATH` is exported; bundled ELF files use relative RUNPATHs.
Audio plugin paths are restored to their original values before launching host applications.

The pipeline checks the same archive in fresh Ubuntu 24.04, Debian 13, Fedora 43 and Arch containers.
It checks command startup, missing libraries and relocation, including a directory containing spaces.
These checks do not replace testing audio, authentication and rendering in a real Wayland session.

Files in `portable/`:

- `build-deps.sh`: Ubuntu build packages and pinned upstream dependencies.
- `build.sh`: compile, stage, bundle and archive Kusanagi.
- `bundle.py`: copy libraries and runtime modules, preserve notices, and set relative library paths.
- `launch-shell`: set audio plugin paths for the bundled executable.
- `install`: add per-user command links without overwriting another installation.

Run the build in a disposable Ubuntu 24.04 container:

```sh
bash packaging/portable/build-deps.sh
bash packaging/portable/build.sh
```

`build-deps.sh` installs system packages and builds libraries under `/usr/local`; it is intended for
CI or a disposable container, not your everyday system. Output is in `dist/`.

The bundle records distribution library packages and versions in
`usr/share/licenses/kusanagi/bundled/packages.json`, along with their copyright files. The upstream
sdbus-c++, WirePlumber and stb notices are included separately. Keep those files with the binaries.

## Release steps

1. Update `VERSION`, `CHANGELOG.md` and the example version in `docs/install.md`.
2. Review `packaging/RELEASE_NOTES.md`, commit and push.
3. Run the Release workflow manually and check its build and distribution jobs:

   ```sh
   gh workflow run release.yml --ref main
   ```

4. Test the downloaded artifact in a Wayland session. Check first setup, rendering, audio,
   app launching, lock/unlock, restart and uninstall.
5. Tag the tested commit and push:

   ```sh
   git tag "v$(cat VERSION)"
   git push origin "v$(cat VERSION)"
   ```

A tag publishes the archive and `SHA256SUMS` after the automated checks pass. Manual runs only
produce Actions artifacts. Keep released files immutable; publish a new version for fixes.

## Native Arch packages

The separate **Arch package** workflow is manual. It builds a native Arch archive, a pacman package
and a checksum-pinned recipe. It does not publish a GitHub release or submit anything to AUR.

`release.sh` stages the native installation tree. `package-arch.sh` wraps it with `makepkg` and
creates a recipe archive containing `PKGBUILD`, `.SRCINFO` and `kusanagi.install`. Run that script
as an unprivileged Arch user. The generated recipe is ready for review and testing; the source-tree
recipe is a template with its checksum filled during the build.

AUR publication requires an account and registered SSH key. Once available, clone the package’s
AUR repository, copy the three recipe files from the tested build, commit and push. Do not commit
binaries to AUR. The `kusanagi-git` recipe builds from source instead.

The portable archive and native Arch archive are different artifacts. Do not point the current
native Arch recipe at the portable archive without changing its layout and dependencies.

## Other distribution packages

Meson can install the complete runtime under a chosen prefix:

```sh
meson setup build native --prefix=/usr --buildtype=release -Dtests=disabled -Druntime=true
meson compile -C build
meson install -C build --destdir "$pkgdir"
```

The command and helpers go under `/usr/lib/kusanagi`, the executable under `/usr/bin`, assets under
`/usr/share/kusanagi`, and notices under `/usr/share/licenses/kusanagi`. User settings stay in the
normal XDG directories. A package’s first launch runs only the per-user compositor setup.
