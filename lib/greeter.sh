#!/bin/sh
# greeter.sh — Kusanagi's login screen for greetd. Used by `kusanagi greeter <cmd>`:
#   install     (asks for sudo once) cage if missing, /var/lib/kusanagi-greeter, the kusanagi-greeter
#               launcher, greetd's config pointed at it (the old one kept as config.toml.pre-kusanagi)
#   sync        copy the shell, your settings, colours, wallpaper and picture where the greeter reads them
#   preview     the login screen in a window (logs nobody in)
#   status      installed? synced?
#   uninstall   (sudo) greetd back to exactly what it was before
# The greeter runs as greetd's greeter user inside cage with HOME=/var/lib/kusanagi-greeter; that folder
# is yours (group: the greeter user, read-only for it), so syncing never needs root.
# If it can't start, the launcher falls back to the greeter greetd had before (agreety …): you can
# always log in. TTY logins (Ctrl+Alt+F1 / F2) are untouched.
set -u

# (KG_* override the paths, and KG_TEST=1 skips the root checks — only for testing this script)
D=${KG_DIR:-/var/lib/kusanagi-greeter}
CONF=${KG_CONF:-/etc/greetd/config.toml}
LAUNCHER=${KG_LAUNCHER:-/usr/local/bin/kusanagi-greeter}
asroot() { [ "$(id -u)" -eq 0 ] || [ "${KG_TEST:-}" = 1 ]; }

die() { echo "kusanagi greeter: $*" >&2; exit 1; }
# the value of `key` inside [section] of greetd's config
toml_get() { sed -n "/^\[$1\]/,/^\[/{s/^[[:space:]]*$2[[:space:]]*=[[:space:]]*\"\(.*\)\"[[:space:]]*$/\1/p}" "$CONF" | head -1; }
installed() { [ -f "$CONF" ] && [ "$(toml_get default_session command)" = "kusanagi-greeter" ]; }

pkg_install() {   # pkg_install <name> — with whatever this distro uses
    if command -v xbps-install >/dev/null; then xbps-install -y "$1"
    elif command -v pacman >/dev/null; then pacman -S --needed --noconfirm "$1"
    elif command -v dnf >/dev/null; then dnf install -y "$1"
    elif command -v apt-get >/dev/null; then apt-get install -y "$1"
    elif command -v zypper >/dev/null; then zypper --non-interactive install "$1"
    elif command -v emerge >/dev/null; then emerge --noreplace "gui-wm/$1"
    else return 1; fi
}

# ---------------- root part (sudo sh greeter.sh root-install <user>) ----------------
root_install() {
    owner=$1
    asroot || die "root_install needs root"
    [ -f "$CONF" ] || die "no $CONF — is greetd installed?"
    command -v cage >/dev/null || pkg_install cage || die "couldn't install cage"
    guser=$(toml_get default_session user); guser=${guser:-greeter}
    id "$guser" >/dev/null 2>&1 || die "greetd's greeter user '$guser' doesn't exist"

    # the greeter's home: yours to write, the greeter user's to read
    if [ "${KG_TEST:-}" = 1 ]; then install -d -m 750 "$D"; else install -d -o "$owner" -g "$guser" -m 750 "$D"; fi

    # the old greeter becomes the fallback, and the old config stays next to it
    [ -f "$CONF.pre-kusanagi" ] || cp -p "$CONF" "$CONF.pre-kusanagi"
    old=$(sed -n '/^\[default_session\]/,/^\[/{s/^[[:space:]]*command[[:space:]]*=[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p}' "$CONF.pre-kusanagi" | head -1)
    [ -n "$old" ] && [ "$old" != "kusanagi-greeter" ] || old="agreety --cmd /bin/sh"

    cat > "$LAUNCHER" <<EOF
#!/bin/sh
# kusanagi-greeter — Kusanagi's login screen for greetd (written by \`kusanagi greeter install\`).
# Runs the shell's greeter.qml in cage; if that can't start, the previous greeter takes over.
D=$D
# the greeter user can only read \$D: logs, caches and state go in its own runtime folder
R=\${XDG_RUNTIME_DIR:-/tmp}/kusanagi-greeter
mkdir -p "\$R"
export HOME=\$D XDG_CONFIG_HOME=\$D/.config XDG_CACHE_HOME=\$R/cache XDG_STATE_HOME=\$R/state QSG_RENDER_LOOP=threaded
export KUSANAGI_GREETER_MARK=\$R/launched
rm -f "\$KUSANAGI_GREETER_MARK"
if [ -f "\$D/shell/greeter.qml" ] && command -v cage >/dev/null && command -v qs >/dev/null; then
    t0=\$(date +%s)
    cage -s -- qs -p "\$D/shell/greeter.qml" >"\$R/greeter.log" 2>&1
    rc=\$?
    sleep 0.2
    # it handed a session to greetd (it leaves this mark first): done, whatever cage returned
    [ -f "\$KUSANAGI_GREETER_MARK" ] && exit 0
    # a login takes longer than 3 s; a crash at start doesn't — then fall back
    [ \$rc -eq 0 ] && [ \$(( \$(date +%s) - t0 )) -gt 3 ] && exit 0
fi
exec $old
EOF
    chmod 755 "$LAUNCHER"

    # point greetd's default session at it (only that line changes)
    sed -i '/^\[default_session\]/,/^\[/{s|^\([[:space:]]*command[[:space:]]*=[[:space:]]*\).*|\1"kusanagi-greeter"|}' "$CONF"
    echo "greetd now starts kusanagi-greeter (old config: $CONF.pre-kusanagi, fallback: $old)"
}

root_uninstall() {
    asroot || die "root_uninstall needs root"
    if [ -f "$CONF.pre-kusanagi" ]; then cp -p "$CONF.pre-kusanagi" "$CONF" && rm -f "$CONF.pre-kusanagi"; echo "greetd's config is back to what it was"
    else echo "no $CONF.pre-kusanagi — leaving $CONF alone"; fi
    rm -f "$LAUNCHER"
    rm -rf "$D"
}

# ---------------- your part ----------------
sync_greeter() {   # sync_greeter <shell dir> [-q]
    src=$1; quiet=${2:-}
    [ -d "$D" ] && [ -w "$D" ] || die "not installed yet — run: kusanagi greeter install"
    cfg=${XDG_CONFIG_HOME:-$HOME/.config}/kusanagi
    mkdir -p "$D/.config/kusanagi" "$D/faces"
    # the shell: replaced whole, so a half-copied one is never what the greeter loads
    rm -rf "$D/shell.new" && cp -r "$src" "$D/shell.new" && rm -rf "$D/shell" && mv "$D/shell.new" "$D/shell"
    # the wallpaper the greeter shows: a copy (yours lives in your home, which it can't read)
    wall=$(cat "$cfg/wallpaper" 2>/dev/null)
    gwall=""
    if [ -n "$wall" ] && [ -f "$wall" ]; then gwall="$D/wallpaper.${wall##*.}"; rm -f "$D"/wallpaper.*; cp "$wall" "$gwall"; fi
    printf '%s\n' "$gwall" > "$D/.config/kusanagi/wallpaper"
    [ -f "$cfg/colors.json" ] && cp "$cfg/colors.json" "$D/.config/kusanagi/colors.json"
    # settings: yours, with the login screen's own design (if you picked one) as the lock style
    python3 -I - "$cfg/settings.json" "$D/.config/kusanagi/settings.json" <<'PY'
import json, sys
try: d = json.load(open(sys.argv[1]))
except Exception: d = {}
g = d.get("greeter", {})
lock = d.setdefault("lock", {})
if g.get("style"): lock["style"] = g["style"]
lock["media"] = False
json.dump(d, open(sys.argv[2], "w"), indent=2)
PY
    # pictures: yours by user name (the greeter shows the one of whoever is picked)
    [ -f "$HOME/.face" ] && cp "$HOME/.face" "$D/faces/$(id -un)"
    chmod -R g+rX "$D"
    [ -n "$quiet" ] || echo "login screen synced ($D)"
}

case "${1:-}" in
    root-install)   root_install "${2:-}" ;;
    root-uninstall) root_uninstall ;;
    sync)           sync_greeter "$2" "${3:-}" ;;
    installed)      installed ;;
    *)              die "internal: $*" ;;
esac
