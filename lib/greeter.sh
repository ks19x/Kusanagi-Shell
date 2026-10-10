#!/bin/sh
# greeter.sh — Kusanagi's login screen for greetd. Used by `kusanagi greeter <cmd>`:
#   install     (asks for sudo once) cage if missing, /var/lib/kusanagi-greeter, the kusanagi-greeter
#               launcher, greetd's config pointed at it (the old one kept as config.toml.pre-kusanagi)
#   sync        copy kusanagi-shell and its assets, your settings, colours, wallpaper and picture where the
#               greeter reads them
#   preview     the login screen in a window (logs nobody in)
#   status      installed? synced?
#   uninstall   (sudo) greetd back to exactly what it was before
# The login screen is `kusanagi-shell --greeter` in cage, run as greetd's greeter user with
# HOME=/var/lib/kusanagi-greeter; that folder is yours (group: the greeter user, read-only for it), so
# syncing never needs root.
# If it can't start, the launcher falls back to the greeter greetd had before (agreety …): you can
# always log in. TTY logins (Ctrl+Alt+F1 / F2) are untouched.
# Launchers written before 0.3.0 run the old QML login screen from $D/shell. sync never touches that copy
# while one of them is installed, so the login keeps working until `kusanagi greeter install` replaces it.
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
# the launcher root_install writes says this word; older ones (QML login screen) don't
MARK=kusanagi-greeter-native
launcher_current() { [ -f "$LAUNCHER" ] && grep -qF "$MARK" "$LAUNCHER"; }
launcher_outdated() { [ -f "$LAUNCHER" ] && ! grep -qF "$MARK" "$LAUNCHER"; }

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
    # the GPU and input through seatd too when it runs (elogind can be late at boot); video for the GPU
    [ "${KG_TEST:-}" = 1 ] || for g in _seatd seat video; do
        getent group "$g" >/dev/null && ! id -nG "$guser" | tr ' ' '\n' | grep -qx "$g" && usermod -aG "$g" "$guser" && echo "added $guser to group $g"
    done

    # the greeter's home: yours to write, the greeter user's to read
    if [ "${KG_TEST:-}" = 1 ]; then install -d -m 750 "$D"; else install -d -o "$owner" -g "$guser" -m 750 "$D"; fi

    # the old greeter becomes the fallback, and the old config stays next to it
    [ -f "$CONF.pre-kusanagi" ] || cp -p "$CONF" "$CONF.pre-kusanagi"
    old=$(sed -n '/^\[default_session\]/,/^\[/{s/^[[:space:]]*command[[:space:]]*=[[:space:]]*"\(.*\)"[[:space:]]*$/\1/p}' "$CONF.pre-kusanagi" | head -1)
    [ -n "$old" ] && [ "$old" != "kusanagi-greeter" ] || old="agreety --cmd /bin/sh"

    cat > "$LAUNCHER" <<EOF
#!/bin/sh
# kusanagi-greeter — Kusanagi's login screen for greetd (written by \`kusanagi greeter install\`).
# $MARK: runs kusanagi-shell --greeter in cage; if that can't start, the previous greeter takes over.
# (\`kusanagi greeter sync\` looks for that word to tell this launcher from older ones.)
D=$D
# a readable note of what happened at the last start (for \`kusanagi greeter status\`), and the
# greeter's own output — both in /tmp: the runtime folder can vanish under us at boot
NOTE=/tmp/kusanagi-greeter.log
OUT=/tmp/kusanagi-greeter-\$(id -u).out
note() { echo "\$(date '+%F %T') \$*" >> "\$NOTE" 2>/dev/null; }
: > "\$NOTE" 2>/dev/null; chmod 644 "\$NOTE" 2>/dev/null

# at boot greetd can start us before the GPU and the seat manager (seatd / elogind) are up: wait for
# them, at most 20 s
ready() {
    ls /dev/dri/card* >/dev/null 2>&1 || return 1
    { [ -S /run/seatd.sock ] && [ -w /run/seatd.sock ]; } || [ -e /run/systemd/seats/seat0 ]
}
waitready() {
    i=0
    while ! ready && [ \$i -lt 40 ]; do sleep 0.5; i=\$((i + 1)); done
    [ \$i -gt 0 ] && note "waited \$((i / 2)) s for the GPU/seat (\$(ready && echo ready || echo 'still not ready'))"
}

# a runtime folder of our own: the one greetd names may not exist yet, or be wiped when elogind starts
XDG_RUNTIME_DIR=/tmp/kusanagi-greeter-runtime-\$(id -u)
mkdir -p "\$XDG_RUNTIME_DIR" 2>/dev/null
[ -O "\$XDG_RUNTIME_DIR" ] || XDG_RUNTIME_DIR=\$(mktemp -d)
chmod 700 "\$XDG_RUNTIME_DIR"; export XDG_RUNTIME_DIR
# the greeter user can only read \$D: caches and state go in the runtime folder
R=\$XDG_RUNTIME_DIR/kusanagi-greeter
mkdir -p "\$R" || note "can't create \$R"
export HOME=\$D XDG_CONFIG_HOME=\$D/.config XDG_CACHE_HOME=\$R/cache XDG_STATE_HOME=\$R/state
export KUSANAGI_GREETER_MARK=\$R/launched

if [ -x "\$D/native/kusanagi-shell" ] && command -v cage >/dev/null; then
    for try in 1 2 3; do
        waitready
        rm -f "\$KUSANAGI_GREETER_MARK"
        t0=\$(date +%s)
        note "try \$try: starting cage + kusanagi-shell --greeter"
        cage -s -- "\$D/native/kusanagi-shell" --greeter >"\$OUT" 2>&1
        rc=\$?
        note "cage exited \$rc after \$(( \$(date +%s) - t0 )) s"
        sleep 0.2
        # it handed a session to greetd (it leaves this mark first): done, whatever cage returned
        [ -f "\$KUSANAGI_GREETER_MARK" ] && exit 0
        # a login takes longer than 3 s; a crash at start doesn't — then try again
        [ \$rc -eq 0 ] && [ \$(( \$(date +%s) - t0 )) -gt 3 ] && exit 0
        tail -n 25 "\$OUT" | sed 's/^/    /' >> "\$NOTE" 2>/dev/null
        sleep 2
    done
else
    note "not starting: kusanagi-shell \$([ -x "\$D/native/kusanagi-shell" ] && echo ok || echo 'missing (kusanagi greeter sync)'), cage \$(command -v cage || echo missing)"
fi
# cage can leave the screen in graphics mode: back to text, or the fallback is an invisible prompt
python3 -c 'import os, fcntl; fd = os.open("/dev/tty", os.O_RDWR); fcntl.ioctl(fd, 0x4B3A, 0)' 2>/dev/null
printf '\033c' 2>/dev/null
note "falling back to: $old"
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
sync_greeter() {   # sync_greeter [-q]
    quiet=${1:-}
    [ -d "$D" ] && [ -w "$D" ] || die "not installed yet — run: kusanagi greeter install"
    cfg=${XDG_CONFIG_HOME:-$HOME/.config}/kusanagi
    mkdir -p "$D/.config/kusanagi" "$D/faces"
    # the wallpaper the greeter shows: a copy (yours lives in your home, which it can't read)
    wall=$(cat "$cfg/wallpaper" 2>/dev/null)
    gwall=""
    if [ -n "$wall" ] && [ -f "$wall" ]; then gwall="$D/wallpaper.${wall##*.}"; rm -f "$D"/wallpaper.*; cp "$wall" "$gwall"; fi
    printf '%s\n' "$gwall" > "$D/.config/kusanagi/wallpaper"
    [ -f "$cfg/colors.json" ] && cp "$cfg/colors.json" "$D/.config/kusanagi/colors.json"
    # the login screen: a copy of kusanagi-shell and its assets. KG_NATIVE_BIN is the binary `kusanagi`
    # runs; it's big, so it's only copied when it changed. A failed copy keeps the last good one.
    bin=${KG_NATIVE_BIN:-$(command -v kusanagi-shell 2>/dev/null)}
    assets=""
    [ -n "$bin" ] && for a in "$(dirname "$bin")/assets" "$(dirname "$bin")/../assets" "$(dirname "$bin")/../share/kusanagi/assets"; do
        [ -f "$a/translations/en.json" ] && { assets=$a; break; }
    done
    if [ -z "$bin" ] || [ ! -x "$bin" ] || [ -z "$assets" ]; then
        echo "kusanagi greeter: kusanagi-shell isn't built${bin:+ ($bin)}, so the login screen's copy isn't updated" >&2
    elif [ "$(head -c 4 "$bin" | od -An -c | tr -d ' ')" != '177ELF' ]; then
        # the portable download's kusanagi-shell is a launcher script that needs its bundle next to it
        echo "kusanagi greeter: $bin isn't the program itself (a portable download?), so the login screen's copy isn't updated" >&2
    elif ! "$bin" --greeter --probe >/dev/null 2>&1; then
        echo "kusanagi greeter: $bin has no login screen (rebuild / reinstall it), so the login screen's copy isn't updated" >&2
    elif ! cmp -s "$bin" "$D/native/kusanagi-shell" || [ ! -d "$D/native/assets" ]; then
        rm -rf "$D/native.new" && mkdir -p "$D/native.new" && cp "$bin" "$D/native.new/kusanagi-shell" \
            && cp -r "$assets" "$D/native.new/assets" \
            && rm -rf "$D/native" && mv "$D/native.new" "$D/native" \
            || { rm -rf "$D/native.new"; echo "kusanagi greeter: couldn't copy kusanagi-shell to $D/native" >&2; }
    fi
    have_native=0; [ -x "$D/native/kusanagi-shell" ] && [ -d "$D/native/assets" ] && have_native=1
    if launcher_current; then
        # the QML copy and engine note were only for older launchers
        rm -rf "$D/shell" "$D/shell.new" "$D/engine"
    elif [ -f "$LAUNCHER" ]; then
        # an older launcher: it runs the native login screen first when $D/engine says so, else the QML
        # copy in $D/shell, which stays exactly as it is
        if [ $have_native = 1 ]; then echo native; else echo qml; fi > "$D/engine"
    fi
    # settings: yours, with the login screen's own design (if you picked one) as the lock style
    last=$(cat "${XDG_STATE_HOME:-$HOME/.local/state}/kusanagi/last-session" 2>/dev/null)
    python3 -I - "$cfg/settings.json" "$D/.config/kusanagi/settings.json" "$last" "$(id -un)" <<'PY'
import json, sys
try: d = json.load(open(sys.argv[1]))
except Exception: d = {}
g = d.setdefault("greeter", {})
# no default picked in Settings: the session you used last, and you
if not g.get("session") and sys.argv[3]: g["session"] = sys.argv[3]
if not g.get("user"): g["user"] = sys.argv[4]
lock = d.setdefault("lock", {})
if g.get("style"): lock["style"] = g["style"]
lock["media"] = False
json.dump(d, open(sys.argv[2], "w"), indent=2)
PY
    # pictures: yours by user name (the greeter shows the one of whoever is picked)
    [ -f "$HOME/.face" ] && cp "$HOME/.face" "$D/faces/$(id -un)"
    chmod -R g+rX "$D"
    [ -n "$quiet" ] || echo "login screen synced ($D)"
    # an older launcher still has its QML copy to show; the current one would fall back to greetd's old greeter
    if [ $have_native = 0 ] && ! { launcher_outdated && [ -f "$D/shell/greeter.qml" ]; }; then
        echo "kusanagi greeter: there's no login screen in $D/native yet; greetd falls back to its old greeter" >&2
        return 1
    fi
}

case "${1:-}" in
    root-install)   root_install "${2:-}" ;;
    root-uninstall) root_uninstall ;;
    sync)           sync_greeter "${2:-}" ;;
    installed)      installed ;;
    launcher-outdated) launcher_outdated ;;
    *)              die "internal: $*" ;;
esac
