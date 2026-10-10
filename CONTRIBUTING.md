# Contributing

Bug reports and patches are welcome. For a bug, include the version, distribution, compositor,
steps to reproduce it and relevant `kusanagi log` output. `kusanagi doctor` helps with session
and dependency problems. Please check logs for private information before posting them.

For a larger change, open an issue first so we can talk through it before you spend time on it.
Keep a pull request focused on one problem and describe what you checked. For visual changes,
a screenshot or short recording is useful.

## Building

See [native/BUILDING.md](native/BUILDING.md) for dependencies and build commands, and
[docs/native.md](docs/native.md) for the source layout.

For native tests:

```sh
meson setup native/build-tests native -Dtests=enabled
meson test -C native/build-tests --print-errorlogs
```

Run the tests relevant to your change. A shell, compositor or rendering change also needs a
manual check in a Wayland session; a successful compile does not cover that.

Release and portable-bundle work lives in `packaging/`. The bundle must keep host graphics
and authentication working, and it must not pass its private library or plugin paths to apps
launched from the desktop. See [packaging/README.md](packaging/README.md).
