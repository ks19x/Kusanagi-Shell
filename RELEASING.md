# Releasing

A release is a version bump, one CI run to check it, and a tag. The tag publishes the download.

## 1. Bump the version

- `VERSION`: the new number, e.g. `0.3.1`.
- `CHANGELOG.md`: rename "(unreleased)" to the version and date, and write what changed for people
  using it, not for people reading the code.
- `docs/install.md`: the example file names (`kusanagi-0.3.1-linux-x86_64.tar.xz`).
- `packaging/RELEASE_NOTES.md`: the text shown on the release page, if anything in it changed.

Commit and push to `main`.

## 2. Run the checks

```sh
gh workflow run release.yml --ref main
gh run watch
```

This builds the portable download on Ubuntu 24.04, then starts it on Ubuntu, Debian, Fedora and Arch,
including a real headless Wayland session on each. Everything must be green. If something fails, fix it
on `main` and run it again; don't tag a commit that hasn't passed.

Then try the build yourself before tagging: download the artifact from the run page, extract it, run
`./install`, `kusanagi restart`, and click through the things that changed. Open and close the panel,
launcher and clipboard, lock and unlock once with your password, and change the volume.

## 3. Tag it

Tag exactly the commit that passed:

```sh
git tag -a "v$(cat VERSION)" <commit> -m "Kusanagi $(cat VERSION)"
git push origin "v$(cat VERSION)"
```

The tag runs the same workflow again and, when it passes, creates the GitHub release with
`kusanagi-<version>-linux-x86_64.tar.xz`, the library sources and `SHA256SUMS`. It takes about 25 minutes.

## 4. After it's out

- Download it from the release page and check `sha256sum -c SHA256SUMS`.
- The website's Download button points at the latest release, so it needs no change. If screenshots
  are out of date, update `site/` and push; the Website workflow deploys it.

## If a release is broken

Don't replace files on a published release. Fix it, bump the patch version and release again. Only if
nobody can have downloaded it yet (the publish step hasn't run) may you move the tag: delete it with
`git push origin :refs/tags/v<version>`, tag the fixed commit and push again.
