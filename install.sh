#!/bin/sh
# Kusanagi installer — any distro, any init. Safe to re-run.
#
#   ./install.sh                     interactive: pick compositors, see the plan, confirm
#   ./install.sh --dry-run           show every command it would run, change nothing
#   ./install.sh -y --compositors=niri,mango   no questions
#   ./install.sh --no-packages | --no-services | --no-config    skip a step
#   ./install.sh --uninstall         remove the links + compositor includes (settings stay)
#
# What it does:
#   1. installs Kusanagi's dependencies + the compositors you pick (pacman/AUR, xbps, dnf, emerge, …)
#   2. enables what a Wayland session needs (dbus, elogind/seatd, NetworkManager) for your init:
#      systemd, runit, OpenRC, dinit or s6 — only services that exist and aren't on yet
#   3. adds ONE include line per compositor config (backup first, validated, reverted if invalid)
#      pointing at Kusanagi's autostart + keybinds, and links the `kusanagi` command
set -u

ROOT=$(cd "$(dirname "$0")" && pwd)
CFG="${XDG_CONFIG_HOME:-$HOME/.config}"
BIN="$HOME/.local/bin"
DRY=0; YES=0; DO_PKG=1; DO_SVC=1; DO_CFG=1; PICK=""

for a in "$@"; do
    case "$a" in
        --dry-run) DRY=1 ;;
        -y|--yes) YES=1 ;;
        --no-packages) DO_PKG=0 ;;
        --no-services) DO_SVC=0 ;;
        --no-config) DO_CFG=0 ;;
        --compositors=*) PICK=$(echo "${a#*=}" | tr ',' ' ') ;;
        --uninstall) UNINSTALL=1 ;;
        -h|--help) sed -n '2,17s/^# \{0,1\}//p' "$0"; exit 0 ;;
        *) echo "unknown option: $a (see --help)"; exit 1 ;;
    esac
done

# ---------------------------------------------------------------- output helpers
if [ -t 1 ]; then B=$(printf '\033[1m'); D=$(printf '\033[2m'); G=$(printf '\033[32m'); Y=$(printf '\033[33m'); R=$(printf '\033[31m'); N=$(printf '\033[0m')
else B=""; D=""; G=""; Y=""; R=""; N=""; fi
say()  { printf '%s\n' "$*"; }
step() { printf '\n%s==>%s %s%s%s\n' "$G" "$N" "$B" "$*" "$N"; }
warn() { printf '%s!%s %s\n' "$Y" "$N" "$*"; }
fail() { printf '%sx%s %s\n' "$R" "$N" "$*"; exit 1; }
run()  { if [ $DRY = 1 ]; then printf '  %s$ %s%s\n' "$D" "$*" "$N"; else printf '  %s$ %s%s\n' "$D" "$*" "$N"; "$@"; fi; }
ask()  { [ $YES = 1 ] && return 0; printf '%s [Y/n] ' "$1"; read -r r </dev/tty; case "$r" in n*|N*) return 1 ;; esac; return 0; }
have() { command -v "$1" >/dev/null 2>&1; }

# ---------------------------------------------------------------- uninstall
if [ "${UNINSTALL:-0}" = 1 ]; then
    have kusanagi && kusanagi stop >/dev/null 2>&1
    rm -f "$BIN/kusanagi"; [ -L "$CFG/quickshell/kusanagi" ] && rm -f "$CFG/quickshell/kusanagi"
    for f in "$CFG/mango/config.conf" "$CFG/hypr/hyprland.lua" "$CFG/hypr/hyprland.conf" "$CFG/niri/config.kdl"; do
        [ -f "$f" ] && grep -q "kusanagi" "$f" && sed -i '/# kusanagi include/d;/kusanagi\.conf/d;/kusanagi\.lua/d;/kusanagi\.kdl/d' "$f" && say "removed the include from $f"
    done
    rm -f "$CFG/mango/kusanagi.conf" "$CFG/hypr/kusanagi.lua" "$CFG/hypr/kusanagi.conf" "$CFG/niri/kusanagi.kdl"
    say "Kusanagi uninstalled — your settings are still in $CFG/kusanagi"; exit 0
fi

# ---------------------------------------------------------------- detect: distro, privileges, init
. "${KUSANAGI_OS_RELEASE:-/etc/os-release}" 2>/dev/null || true     # (override: testing other distros)
ID=${ID:-unknown}; LIKE=" ${ID_LIKE:-} "
case "$ID $LIKE" in
    *void*)                              FAM=void ;;
    *arch*|*artix*|*cachyos*|*endeavouros*|*manjaro*|*garuda*) FAM=arch ;;
    *gentoo*)                            FAM=gentoo ;;
    *fedora*|*rhel*|*nobara*)            FAM=fedora ;;
    *debian*|*ubuntu*|*mint*|*pop*)      FAM=debian ;;
    *suse*)                              FAM=suse ;;
    *nixos*)                             FAM=nixos ;;
    *)                                   FAM=unknown ;;
esac
ARTIX=0; case "$ID" in artix) ARTIX=1 ;; esac

if [ "$(id -u)" = 0 ]; then SU=""
elif have doas; then SU=doas
elif have sudo; then SU=sudo
else SU=""; fi

if [ -n "${KUSANAGI_INIT:-}" ]; then INIT=$KUSANAGI_INIT              # (override: testing other inits)
elif [ -d /run/systemd/system ]; then INIT=systemd
elif [ -d /run/runit ] || [ -d /etc/runit ] || { have sv && [ -d /var/service ]; }; then INIT=runit
elif have openrc || [ -d /run/openrc ]; then INIT=openrc
elif have dinitctl; then INIT=dinit
elif have s6-rc; then INIT=s6
else INIT=unknown; fi

step "Kusanagi $(cat "$ROOT/VERSION" 2>/dev/null) installer"
say "  distro: ${PRETTY_NAME:-$ID} ($FAM)    init: $INIT    privileges: ${SU:-none (root or nothing)}"
[ $DRY = 1 ] && say "  ${Y}dry run — nothing will be changed${N}"

# ---------------------------------------------------------------- choose compositors
comp_bin() { case "$1" in mango) echo mango ;; hyprland) echo Hyprland ;; niri) echo niri ;; esac; }
if [ -z "$PICK" ]; then
    if [ $YES = 1 ]; then
        for c in mango hyprland niri; do have "$(comp_bin $c)" && PICK="$PICK $c"; done
        [ -z "$PICK" ] && PICK=niri
    else
        step "Which compositors should Kusanagi run on?"
        for c in mango hyprland niri; do
            have "$(comp_bin $c)" && st="${G}installed${N}" || st="${D}not installed${N}"
            printf '  %-9s %s\n' "$c" "$st"
        done
        printf 'Pick any, separated by spaces [default: the installed ones, or niri]: '
        read -r PICK </dev/tty
        if [ -z "$PICK" ]; then for c in mango hyprland niri; do have "$(comp_bin $c)" && PICK="$PICK $c"; done; [ -z "$PICK" ] && PICK=niri; fi
    fi
fi
for c in $PICK; do case "$c" in mango|hyprland|niri) ;; *) fail "unknown compositor '$c' (mango, hyprland, niri)" ;; esac; done
say "  compositors:$(printf ' %s' $PICK)"

# ---------------------------------------------------------------- 1. packages
# logical name -> package per family ("" = not packaged there)
pkg() {
    case "$FAM:$1" in
        arch:quickshell) echo quickshell ;;            void:quickshell) echo quickshell ;;
        arch:python) echo python ;;                    void:python) echo python3 ;;
        arch:magick) echo imagemagick ;;               void:magick) echo ImageMagick ;;
        arch:font) echo ttf-jetbrains-mono-nerd ;;     void:font) echo nerd-fonts-ttf ;;
        arch:nm) [ $ARTIX = 1 ] && echo "networkmanager networkmanager-$INIT" || echo networkmanager ;;
        void:nm) echo NetworkManager ;;
        arch:mango) echo mangowm ;;                    void:mango) echo mangowc ;;
        arch:hyprland) echo "hyprland xdg-desktop-portal-hyprland" ;;
        void:hyprland) echo "hyprland xdg-desktop-portal-hyprland" ;;
        arch:niri|void:niri) echo "niri xdg-desktop-portal-gnome xwayland-satellite" ;;
        arch:pipewire|void:pipewire) echo "pipewire wireplumber" ;;
        arch:portal|void:portal) echo "xdg-desktop-portal-gtk" ;;
        arch:seat) [ $ARTIX = 1 ] && echo "elogind elogind-$INIT dbus-$INIT" || echo "" ;;
        void:seat) echo "elogind dbus" ;;
        fedora:quickshell) echo quickshell ;;          fedora:python) echo python3 ;;
        fedora:magick) echo ImageMagick ;;             fedora:font) echo "" ;;
        fedora:nm) echo NetworkManager ;;              fedora:mango) echo "" ;;
        fedora:hyprland) echo "hyprland xdg-desktop-portal-hyprland" ;;
        fedora:niri) echo "niri xdg-desktop-portal-gnome xwayland-satellite" ;;
        fedora:pipewire) echo "pipewire wireplumber" ;; fedora:portal) echo "xdg-desktop-portal-gtk" ;;
        fedora:seat) echo "" ;;
        gentoo:quickshell) echo gui-apps/quickshell ;; gentoo:python) echo dev-lang/python ;;
        gentoo:magick) echo media-gfx/imagemagick ;;   gentoo:font) echo "" ;;
        gentoo:nm) echo net-misc/networkmanager ;;     gentoo:mango) echo "" ;;
        gentoo:hyprland) echo gui-wm/hyprland ;;       gentoo:niri) echo gui-wm/niri ;;
        gentoo:pipewire) echo "media-video/pipewire media-video/wireplumber" ;; gentoo:portal) echo sys-apps/xdg-desktop-portal-gtk ;;
        gentoo:seat) echo sys-auth/elogind ;;
        debian:python) echo python3 ;;                 debian:magick) echo imagemagick ;;
        debian:nm) echo network-manager ;;             debian:pipewire) echo "pipewire wireplumber" ;;
        debian:portal) echo xdg-desktop-portal-gtk ;;  debian:niri) echo niri ;;
        debian:hyprland) echo hyprland ;;              debian:libnotify) echo libnotify-bin ;;
        suse:quickshell) echo quickshell ;;            suse:python) echo python3 ;;
        suse:magick) echo ImageMagick ;;               suse:nm) echo NetworkManager ;;
        suse:pipewire) echo "pipewire wireplumber" ;;  suse:portal) echo xdg-desktop-portal-gtk ;;
        suse:niri) echo niri ;;                        suse:hyprland) echo hyprland ;;
        *:quickshell|*:python|*:magick|*:font|*:nm|*:mango|*:hyprland|*:niri|*:pipewire|*:portal|*:seat) echo "" ;;
        gentoo:*) case "$1" in wl-clipboard) echo gui-apps/wl-clipboard ;; cliphist) echo gui-apps/cliphist ;; grim) echo gui-apps/grim ;;
                               slurp) echo gui-apps/slurp ;; gammastep) echo x11-misc/gammastep ;; gamemode) echo games-util/gamemode ;;
                               swappy) echo gui-apps/swappy ;; foot) echo gui-apps/foot ;; hyprlock) echo gui-apps/hyprlock ;;
                               libnotify) echo x11-libs/libnotify ;; pavucontrol) echo media-sound/pavucontrol ;; *) echo "" ;; esac ;;
        *) echo "$1" ;;                                # same name everywhere else
    esac
}
WANT="quickshell python magick wl-clipboard cliphist grim slurp gammastep gamemode swappy foot font nm pavucontrol hyprlock libnotify pipewire portal seat"
for c in $PICK; do WANT="$WANT $c"; done

installed() {   # is package $1 installed?
    case "$FAM" in
        arch)   pacman -Q "$1" >/dev/null 2>&1 ;;
        void)   xbps-query "$1" >/dev/null 2>&1 ;;
        fedora) rpm -q "$1" >/dev/null 2>&1 ;;
        gentoo) [ -d "/var/db/pkg/$(echo "$1" | cut -d/ -f1)" ] && ls -d /var/db/pkg/"$1"-[0-9]* >/dev/null 2>&1 ;;
        debian) dpkg -s "$1" >/dev/null 2>&1 ;;
        *)      return 1 ;;
    esac
}

if [ $DO_PKG = 1 ]; then
    step "Packages"
    MISSING=""; UNPACKAGED=""
    for w in $WANT; do
        p=$(pkg "$w")
        if [ -z "$p" ]; then
            case "$w" in seat|font) ;; *) UNPACKAGED="$UNPACKAGED $w" ;; esac
            continue
        fi
        for one in $p; do installed "$one" || MISSING="$MISSING $one"; done
    done
    if [ -z "$MISSING" ]; then say "  everything is already installed"
    else
        say "  to install:$MISSING"
        case "$FAM" in
            arch)
                if ask "Install them with pacman?"; then
                    # anything pacman doesn't know goes to an AUR helper if there is one
                    REPO=""; AUR=""
                    for p in $MISSING; do pacman -Si "$p" >/dev/null 2>&1 && REPO="$REPO $p" || AUR="$AUR $p"; done
                    [ -n "$REPO" ] && run $SU pacman -S --needed --noconfirm $REPO
                    if [ -n "$AUR" ]; then
                        if have paru; then run paru -S --needed $AUR
                        elif have yay; then run yay -S --needed $AUR
                        else warn "not in your repos (install from the AUR):$AUR"; fi
                    fi
                fi ;;
            void)   ask "Install them with xbps?" && run $SU xbps-install -Sy $MISSING ;;
            fedora) say "  ${D}quickshell and hyprland come from COPR: errornointernet/quickshell, solopasha/hyprland${N}"
                    ask "Install them with dnf?" && run $SU dnf install -y $MISSING ;;
            gentoo) say "  ${D}quickshell / niri need the GURU overlay (eselect repository enable guru)${N}"
                    ask "Install them with emerge?" && run $SU emerge --noreplace $MISSING ;;
            debian) warn "Debian/Ubuntu don't package quickshell yet — build it from https://quickshell.org first"
                    ask "Install the rest with apt?" && run $SU apt-get install -y $MISSING ;;
            suse)   ask "Install them with zypper?" && run $SU zypper install -y $MISSING ;;
            nixos)  warn "NixOS: add these to your configuration instead:$MISSING" ;;
            *)      warn "unknown distro — install these yourself:$MISSING" ;;
        esac
    fi
    [ -n "$UNPACKAGED" ] && warn "not packaged on $FAM — install by hand:$UNPACKAGED"
    case "$FAM" in fedora|gentoo|debian|suse|unknown) say "  ${D}also needed: a Nerd Font (JetBrainsMono Nerd Font recommended)${N}" ;; esac
fi

# ---------------------------------------------------------------- 2. services (init-agnostic)
svc_exists() {
    case "$INIT" in
        systemd) systemctl list-unit-files "$1.service" >/dev/null 2>&1 && systemctl list-unit-files "$1.service" | grep -q "$1" ;;
        runit)   [ -d "/etc/sv/$1" ] || [ -d "/etc/runit/sv/$1" ] ;;
        openrc)  [ -x "/etc/init.d/$1" ] ;;
        dinit)   [ -f "/etc/dinit.d/$1" ] ;;
        s6)      [ -d "/etc/s6/sv/$1" ] || s6-rc-db list all 2>/dev/null | grep -qx "$1" ;;
        *)       return 1 ;;
    esac
}
svc_enabled() {
    case "$INIT" in
        systemd) systemctl is-enabled "$1" >/dev/null 2>&1 ;;
        runit)   [ -e "/var/service/$1" ] || [ -e "/run/runit/service/$1" ] || [ -e "/etc/runit/runsvdir/default/$1" ] ;;
        openrc)  rc-update show 2>/dev/null | grep -qw "$1" ;;
        dinit)   [ -e "/etc/dinit.d/boot.d/$1" ] ;;
        s6)      s6-rc-db -c /etc/s6/rc/compiled atomics default 2>/dev/null | grep -qx "$1" ;;
        *)       return 0 ;;
    esac
}
svc_enable() {
    case "$INIT" in
        systemd) run $SU systemctl enable --now "$1" ;;
        runit)   if [ -d /etc/sv/"$1" ]; then run $SU ln -s "/etc/sv/$1" /var/service/
                 else run $SU ln -s "/etc/runit/sv/$1" /run/runit/service/; fi ;;
        openrc)  run $SU rc-update add "$1" default; run $SU rc-service "$1" start ;;
        dinit)   run $SU dinitctl enable "$1" ;;
        s6)      run $SU s6-service add default "$1"; run $SU s6-db-reload ;;
    esac
}
if [ $DO_SVC = 1 ]; then
    step "Services ($INIT)"
    if [ "$INIT" = unknown ]; then warn "couldn't tell your init — make sure dbus, a seat manager (elogind or seatd) and NetworkManager run"
    else
        TODO=""
        for s in dbus elogind NetworkManager; do
            [ "$INIT" = systemd ] && [ "$s" = elogind ] && continue      # logind is part of systemd
            svc_exists "$s" || continue
            svc_enabled "$s" || TODO="$TODO $s"
        done
        if [ -z "$TODO" ]; then say "  dbus / seat / NetworkManager: all set"
        else
            say "  not running yet:$TODO"
            case " $TODO " in *" NetworkManager "*) say "  ${D}(if you use dhcpcd/iwd/connman for networking, answer no — they conflict)${N}" ;; esac
            if ask "Enable them?"; then for s in $TODO; do svc_enable "$s"; done; fi
        fi
    fi
fi

# ---------------------------------------------------------------- 3. compositor config + links
backup() { [ $DRY = 1 ] || cp "$1" "$1.bak-kusanagi-$(date +%Y%m%d-%H%M%S)"; }
# already wired up anywhere in that compositor's config folder (sourced/rice files included)?
wired() { grep -RqsI --exclude='*.bak*' --exclude='kusanagi.*' -e 'kusanagi msg' -e 'kusanagi start' -e 'kusanagi restart' -e 'exec-once=kusanagi' -e '"kusanagi:' "$1" 2>/dev/null; }
add_include() {   # file, line
    f=$1; line=$2
    if wired "$(dirname "$f")"; then say "  $(dirname "$f") already starts/binds Kusanagi (maybe via a rice) — leaving it as is"; return 0; fi
    if [ ! -f "$f" ]; then
        warn "$f doesn't exist yet — start the compositor once (it writes a default config), then re-run this"
        return 0
    fi
    backup "$f"
    if [ $DRY = 1 ]; then say "  would append to $f:  $line"; return 0; fi
    printf '\n# kusanagi include (added by install.sh)\n%s\n' "$line" >> "$f"
    say "  added the include to $f"
}
if [ $DO_CFG = 1 ]; then
    step "Compositor config"
    for c in $PICK; do
        case "$c" in
            mango)
                mkdir -p "$CFG/mango"
                [ $DRY = 1 ] || cp "$ROOT/compositors/mango.conf" "$CFG/mango/kusanagi.conf"
                add_include "$CFG/mango/config.conf" "source=./kusanagi.conf"
                if [ $DRY = 0 ] && have mango && ! mango -c "$CFG/mango/config.conf" -p >/dev/null 2>&1; then
                    warn "mango rejected the config — reverting"; f=$(ls -t "$CFG/mango/config.conf.bak-kusanagi-"* | head -1); cp "$f" "$CFG/mango/config.conf"; fi ;;
            hyprland)
                mkdir -p "$CFG/hypr"
                if [ -f "$CFG/hypr/hyprland.lua" ]; then
                    [ $DRY = 1 ] || cp "$ROOT/compositors/hyprland.lua" "$CFG/hypr/kusanagi.lua"
                    add_include "$CFG/hypr/hyprland.lua" 'dofile(os.getenv("HOME") .. "/.config/hypr/kusanagi.lua")'
                else
                    [ $DRY = 1 ] || cp "$ROOT/compositors/hyprland.conf" "$CFG/hypr/kusanagi.conf"
                    add_include "$CFG/hypr/hyprland.conf" "source = $CFG/hypr/kusanagi.conf"
                fi ;;
            niri)
                mkdir -p "$CFG/niri"
                [ $DRY = 1 ] || cp "$ROOT/compositors/niri.kdl" "$CFG/niri/kusanagi.kdl"
                add_include "$CFG/niri/config.kdl" 'include "kusanagi.kdl"'
                if [ $DRY = 0 ] && have niri && [ -f "$CFG/niri/config.kdl" ] && ! niri validate -c "$CFG/niri/config.kdl" >/dev/null 2>&1; then
                    warn "niri rejected the config (a keybind may clash with yours) — reverting; edit $CFG/niri/kusanagi.kdl and re-run"
                    f=$(ls -t "$CFG/niri/config.kdl.bak-kusanagi-"* 2>/dev/null | head -1); [ -n "$f" ] && cp "$f" "$CFG/niri/config.kdl"; fi ;;
        esac
    done
fi

step "Kusanagi itself"
if [ $DRY = 0 ]; then
    mkdir -p "$BIN" "$CFG/quickshell" "$CFG/kusanagi"
    ln -sfn "$ROOT/bin/kusanagi" "$BIN/kusanagi"
    if [ -e "$CFG/quickshell/kusanagi" ] && [ ! -L "$CFG/quickshell/kusanagi" ]; then warn "$CFG/quickshell/kusanagi exists and isn't a link — leaving it"
    else ln -sfn "$ROOT/shell" "$CFG/quickshell/kusanagi"; fi
    say "  linked: $BIN/kusanagi"
else say "  would link $BIN/kusanagi → $ROOT/bin/kusanagi"; fi
case ":$PATH:" in *":$BIN:"*) ;; *) warn "$BIN isn't on your PATH — add it (e.g. in ~/.profile)" ;; esac

step "Check"
if [ $DRY = 0 ] && [ -x "$BIN/kusanagi" ]; then "$BIN/kusanagi" doctor || true; fi
say ""
say "${B}Done.${N} Log into $(printf '%s ' $PICK)— Kusanagi starts with it. Or right now:  kusanagi"
say "Keys: Super+Space apps · Super+A wallpapers · Super+V clipboard · Super+N inbox · Super+I settings · Super+\` power"
