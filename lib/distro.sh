#!/bin/sh
# lib/distro.sh — what distro family this is, what things are called here, how to install / update them.
#   sh lib/distro.sh family                 void arch gentoo fedora debian suse alpine nixos unknown
#   sh lib/distro.sh pkgname <logical>      package name(s) here ("" = not packaged)
#   sh lib/distro.sh install-cmd <logical…> the command line to install them (+ "# …" notes for the rest)
#   sh lib/distro.sh updates                available updates, no root, nothing changed:
#                                           line 1 = count (or ?), then "name<TAB>source" (≤ 200 lines)
#                                           exit 0 ok · 1 a check failed (count ?) · 2 no checker available
#   sh lib/distro.sh upgrade-cmd            the command (for sh -c) that upgrades everything
#   sh lib/distro.sh storage                package cache / orphans / old kernels: key=value lines + the clean-up commands
# Sourced (. lib/distro.sh) it only defines FAM, ARTIX, D_ID and pkg() — the installer uses that.
# Testing: KUSANAGI_OS_RELEASE=<fake os-release>, KUSANAGI_INIT=<init>, KUSANAGI_UPDATES_TIMEOUT=<s>.
# Logical names: quickshell python magick wl-clipboard cliphist grim slurp gammastep gamemode swappy foot
# font nm pavucontrol hyprlock libnotify pipewire portal seat wtype wf-recorder gpu-screen-recorder polkit
# fd checkupdates mango hyprland niri

# ---------------------------------------------------------------- detect (doesn't touch the caller's vars)
_d_ids=$( . "${KUSANAGI_OS_RELEASE:-/etc/os-release}" 2>/dev/null; printf '%s|%s' "${ID:-unknown}" "${ID_LIKE:-}")
D_ID=${_d_ids%%|*}
case "$D_ID ${_d_ids#*|} " in
    *void*)                              FAM=void ;;
    *arch*|*artix*|*cachyos*|*endeavouros*|*manjaro*|*garuda*) FAM=arch ;;
    *gentoo*)                            FAM=gentoo ;;
    *fedora*|*rhel*|*nobara*)            FAM=fedora ;;
    *debian*|*ubuntu*|*mint*|*pop*)      FAM=debian ;;
    *suse*)                              FAM=suse ;;
    *nixos*)                             FAM=nixos ;;
    *alpine*|*postmarketos*)             FAM=alpine ;;
    *)                                   FAM=unknown ;;
esac
ARTIX=0; case "$D_ID" in artix) ARTIX=1 ;; esac
unset _d_ids

_d_have() { command -v "$1" >/dev/null 2>&1; }
_d_init() {   # same detection as install.sh (only Artix's NetworkManager package needs it)
    if [ -n "${KUSANAGI_INIT:-}" ]; then echo "$KUSANAGI_INIT"
    elif [ -d /run/systemd/system ]; then echo systemd
    elif [ -d /run/runit ] || [ -d /etc/runit ] || { _d_have sv && [ -d /var/service ]; }; then echo runit
    elif _d_have openrc || [ -d /run/openrc ]; then echo openrc
    elif _d_have dinitctl; then echo dinit
    elif _d_have s6-rc; then echo s6
    else echo unknown; fi
}

# ---------------------------------------------------------------- packages
# logical name -> package per family ("" = not packaged there)
pkg() {
    INIT=${INIT:-$(_d_init)}
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
        alpine:python) echo python3 ;;                 alpine:magick) echo imagemagick ;;
        alpine:font) echo font-jetbrains-mono-nerd ;;  alpine:nm) echo networkmanager ;;
        alpine:hyprland) echo "hyprland xdg-desktop-portal-hyprland" ;; alpine:niri) echo niri ;;
        alpine:pipewire) echo "pipewire wireplumber" ;; alpine:portal) echo xdg-desktop-portal-gtk ;;
        alpine:seat) echo seatd ;;
        *:quickshell|*:python|*:magick|*:font|*:nm|*:mango|*:hyprland|*:niri|*:pipewire|*:portal|*:seat) echo "" ;;
        # colour emoji for the launcher's ":" search (and every app)
        void:emoji|arch:emoji) echo noto-fonts-emoji ;; debian:emoji) echo fonts-noto-color-emoji ;;
        fedora:emoji) echo google-noto-color-emoji-fonts ;; suse:emoji) echo google-noto-coloremoji-fonts ;;
        gentoo:emoji) echo media-fonts/noto-emoji ;;   alpine:emoji) echo font-noto-emoji ;; *:emoji) echo "" ;;
        # (not part of the installer's list — recording, launcher, file search, update count)
        arch:checkupdates) echo pacman-contrib ;;      *:checkupdates) echo "" ;;
        void:gpu-screen-recorder|arch:gpu-screen-recorder) echo gpu-screen-recorder ;;
        *:gpu-screen-recorder) echo "" ;;
        debian:fd|fedora:fd) echo fd-find ;;           debian:polkit) echo polkitd ;;
        gentoo:wtype) echo gui-apps/wtype ;;           gentoo:wf-recorder) echo gui-apps/wf-recorder ;;
        gentoo:fd) echo sys-apps/fd ;;                 gentoo:polkit) echo sys-auth/polkit ;;
        gentoo:*) case "$1" in wl-clipboard) echo gui-apps/wl-clipboard ;; cliphist) echo gui-apps/cliphist ;; grim) echo gui-apps/grim ;;
                               slurp) echo gui-apps/slurp ;; gammastep) echo x11-misc/gammastep ;; gamemode) echo games-util/gamemode ;;
                               swappy) echo gui-apps/swappy ;; foot) echo gui-apps/foot ;; hyprlock) echo gui-apps/hyprlock ;;
                               libnotify) echo x11-libs/libnotify ;; pavucontrol) echo media-sound/pavucontrol ;; *) echo "" ;; esac ;;
        *) echo "$1" ;;                                # same name everywhere else
    esac
}

_d_su() { if [ "$(id -u)" = 0 ]; then echo ""; elif _d_have doas; then echo "doas "; else echo "sudo "; fi; }

# how to get something that isn't packaged here (one line, no "#")
_d_hint() {
    case "$1" in
        quickshell)          echo "quickshell isn't packaged on $FAM — build it from source: https://quickshell.org/docs/guide/install-setup/" ;;
        gpu-screen-recorder) echo "gpu-screen-recorder isn't packaged on $FAM — use the Flatpak: flatpak install flathub com.dec05eba.gpu_screen_recorder" ;;
        font)                echo "no Nerd Font package on $FAM — get JetBrainsMono Nerd Font from https://www.nerdfonts.com/font-downloads into ~/.local/share/fonts, then: fc-cache -f" ;;
        mango)               echo "mango isn't packaged on $FAM — build it from source: https://github.com/DreamMaoMao/mangowc" ;;
        checkupdates|seat)   ;;   # Arch-only / not needed here
        *)                   echo "$1 isn't packaged on $FAM — install it by hand" ;;
    esac
}

install_cmd() {
    su=$(_d_su); pk=""; notes=""
    if [ "$FAM" = nixos ]; then
        echo "# NixOS isn't supported here — add these to environment.systemPackages in your configuration.nix: $*"
        return 0
    fi
    for w in "$@"; do
        p=$(pkg "$w")
        if [ -z "$p" ]; then h=$(_d_hint "$w"); [ -n "$h" ] && notes="$notes
# $h"; continue; fi
        pk="$pk $p"
        case "$FAM:$w" in
            fedora:quickshell) notes="$notes
# quickshell comes from COPR first: ${su}dnf copr enable errornointernet/quickshell" ;;
            fedora:hyprland)   notes="$notes
# hyprland comes from COPR first: ${su}dnf copr enable solopasha/hyprland" ;;
            gentoo:quickshell|gentoo:niri|gentoo:cliphist) notes="$notes
# $w is in the GURU overlay: ${su}eselect repository enable guru && ${su}emaint sync -r guru" ;;
        esac
    done
    pk=${pk# }
    if [ -n "$pk" ]; then
        case "$FAM" in
            arch)
                repo=""; aur=""
                for p in $pk; do
                    if ! _d_have pacman || pacman -Si "$p" >/dev/null 2>&1; then repo="$repo $p"; else aur="$aur $p"; fi
                done
                if [ -n "$aur" ] && _d_have paru; then echo "paru -S --needed $pk"
                elif [ -n "$aur" ] && _d_have yay; then echo "yay -S --needed $pk"
                else
                    [ -n "$repo" ] && echo "${su}pacman -S --needed${repo}"
                    [ -n "$aur" ] && notes="$notes
# from the AUR (install paru or yay first):$aur"
                fi ;;
            void)    echo "${su}xbps-install -S $pk" ;;
            debian)  echo "${su}apt install $pk" ;;
            fedora)  if _d_have dnf || ! _d_have dnf5; then echo "${su}dnf install $pk"; else echo "${su}dnf5 install $pk"; fi ;;
            suse)    echo "${su}zypper install $pk" ;;
            gentoo)  echo "${su}emerge -av $pk" ;;
            alpine)  echo "${su}apk add $pk" ;;
            *)       echo "# unknown distro — install these with your package manager: $pk" ;;
        esac
    fi
    # one note per line, no repeats
    [ -n "$notes" ] && printf '%s\n' "$notes" | awk 'NF && !seen[$0]++'
    return 0
}

# ---------------------------------------------------------------- updates
_d_to() { if _d_have timeout; then timeout "${KUSANAGI_UPDATES_TIMEOUT:-120}" "$@"; else "$@"; fi; }

updates() {
    out=$(mktemp "${TMPDIR:-/tmp}/kusanagi-updates.XXXXXX") || return 2
    any=0 failed=0
    # $1 = source; reads "name" lines on stdin
    _d_add() { awk -v s="$1" 'NF { print $1 "\t" s }' >>"$out"; }

    case "$FAM" in
        void)
            if _d_have xbps-install; then any=1
                r=$(_d_to xbps-install -Mun 2>/dev/null); rc=$?
                case $rc in 0|6) printf '%s\n' "$r" | awk '$2 == "update" { n = $1; sub(/-[^-]*$/, "", n); print n }' | _d_add xbps ;; *) failed=1 ;; esac
            fi ;;
        arch)
            if _d_have checkupdates; then any=1
                r=$(_d_to checkupdates 2>/dev/null); rc=$?
                case $rc in 0|2) printf '%s\n' "$r" | _d_add pacman ;; *) failed=1 ;; esac
            elif _d_have pacman; then any=1
                echo "distro.sh: no checkupdates (pacman-contrib) — pacman -Qu only knows the last sync" >&2
                pacman -Qu 2>/dev/null | _d_add "pacman (stale)"
            fi
            for h in paru yay; do
                _d_have $h || continue
                any=1; r=$(_d_to $h -Qua 2>/dev/null); rc=$?
                case $rc in 0|1) printf '%s\n' "$r" | grep -v '^\[\|^:: ' | _d_add aur ;; *) failed=1 ;; esac
                break
            done ;;
        debian)
            if _d_have apt; then any=1
                echo "distro.sh: apt only knows the last 'apt update' (needs root), so this can be stale" >&2
                apt list --upgradable 2>/dev/null | awk -F/ '/\// { print $1 }' | _d_add apt
            elif _d_have apt-get; then any=1
                apt-get -s upgrade 2>/dev/null | awk '$1 == "Inst" { print $2 }' | _d_add apt
            fi ;;
        fedora)
            d=dnf; _d_have dnf || d=dnf5
            if _d_have $d; then any=1
                r=$(_d_to $d check-update -q 2>/dev/null); rc=$?
                case $rc in
                    0|100) printf '%s\n' "$r" | awk '/^(Obsoleting|Security:)/ { exit } NF >= 3 && $1 ~ /\./ && $1 !~ /^ / { n = $1; sub(/\.[^.]*$/, "", n); print n }' | _d_add dnf ;;
                    *) failed=1 ;;
                esac
            fi ;;
        suse)
            if _d_have zypper; then any=1
                r=$(_d_to zypper -q --non-interactive list-updates 2>/dev/null); rc=$?
                if [ $rc = 0 ]; then printf '%s\n' "$r" | awk -F'|' '$1 ~ /^ *v *$/ { n = $3; gsub(/ /, "", n); print n }' | _d_add zypper
                else failed=1; fi
            fi ;;
        alpine)
            if _d_have apk; then any=1
                r=$(apk list --upgradable 2>/dev/null); rc=$?
                if [ $rc = 0 ]; then printf '%s\n' "$r" | awk '{ n = $1; sub(/-[^-]*-r[0-9]+$/, "", n); print n }' | _d_add apk
                else apk version -l '<' 2>/dev/null | awk 'NR > 1 { n = $1; sub(/-[^-]*-r[0-9]+$/, "", n); print n }' | _d_add apk; fi
            fi ;;
        gentoo)
            if _d_have eix; then any=1
                eix -u --only-names 2>/dev/null | _d_add emerge
            else echo "distro.sh: Gentoo: no cheap way to check without eix (emerge -puDN @world is slow) — install app-portage/eix" >&2; fi ;;
        nixos) echo "distro.sh: NixOS isn't supported (updates come from your channel/flake)" >&2 ;;
        *)     echo "distro.sh: unknown distro — can't check system updates" >&2 ;;
    esac

    if _d_have flatpak; then any=1
        for inst in --system --user; do
            r=$(_d_to flatpak remote-ls --updates --columns=application $inst 2>/dev/null) || { failed=1; continue; }
            printf '%s\n' "$r" | _d_add flatpak
        done
    fi

    if [ $any = 0 ]; then echo "?"; rm -f "$out"; return 2; fi
    if [ $failed = 1 ]; then echo "?"; else wc -l <"$out" | tr -d ' '; fi
    head -n 200 "$out"
    rm -f "$out"
    return $failed
}

upgrade_cmd() {
    su=$(_d_su); c=""
    case "$FAM" in
        void)   c="${su}xbps-install -Su" ;;
        arch)   if _d_have paru; then c="paru -Syu"; elif _d_have yay; then c="yay -Syu"; else c="${su}pacman -Syu"; fi ;;
        debian) c="${su}apt update && ${su}apt upgrade" ;;
        fedora) if _d_have dnf || ! _d_have dnf5; then c="${su}dnf upgrade"; else c="${su}dnf5 upgrade"; fi ;;
        suse)   case "$D_ID" in *leap*) c="${su}zypper up" ;; *) c="${su}zypper dup" ;; esac ;;
        alpine) c="${su}apk upgrade" ;;
        gentoo) c="${su}emerge --sync && ${su}emerge -avuDN @world" ;;
    esac
    _d_have flatpak && c="${c:+$c ; }flatpak update"
    [ -n "$c" ] || { echo "distro.sh: don't know how to upgrade $FAM" >&2; return 2; }
    echo "$c"
}

# ---------------------------------------------------------------- storage housekeeping (Settings → Storage)
# key=value lines: cache (dir) · cacheSize · cacheClean · orphans (count, empty = can't tell) · orphansRemove ·
# kernels (count) · kernelsRemove. Commands are for a terminal (they ask for the password); empty = n/a.
storage() {
    su=$(_d_su); cache=""; clean=""; orph=""; orphrm=""; kern=""; kernrm=""
    case "$FAM" in
        void)   cache=/var/cache/xbps; clean="${su}xbps-remove -O"
                orph=$(xbps-query -O 2>/dev/null | grep -c .); orphrm="${su}xbps-remove -o"
                _d_have vkpurge && { kern=$(vkpurge list 2>/dev/null | grep -c .); kernrm="${su}vkpurge rm all"; } ;;
        arch)   cache=/var/cache/pacman/pkg
                if _d_have paccache; then clean="${su}paccache -rk2"; else clean="${su}pacman -Sc"; fi
                orph=$(pacman -Qdtq 2>/dev/null | grep -c .); orphrm="${su}pacman -Rns \$(pacman -Qdtq)" ;;
        debian) cache=/var/cache/apt/archives; clean="${su}apt clean"
                orph=$(apt-get -s autoremove 2>/dev/null | grep -c '^Remv'); orphrm="${su}apt autoremove" ;;
        fedora) cache=/var/cache/libdnf5; [ -d "$cache" ] || cache=/var/cache/dnf; clean="${su}dnf clean packages"
                orphrm="${su}dnf autoremove" ;;
        suse)   cache=/var/cache/zypp/packages; clean="${su}zypper clean -a"
                orph=$(zypper -q packages --unneeded 2>/dev/null | grep -c '^i') ;;
        alpine) cache=/var/cache/apk; clean="${su}apk cache clean" ;;
        gentoo) cache=/var/cache/distfiles; _d_have eclean-dist && clean="${su}eclean-dist"
                orphrm="${su}emerge -a --depclean" ;;
    esac
    size=""; [ -n "$cache" ] && [ -d "$cache" ] && size=$(du -sh "$cache" 2>/dev/null | cut -f1)
    printf 'cache=%s\ncacheSize=%s\ncacheClean=%s\norphans=%s\norphansRemove=%s\nkernels=%s\nkernelsRemove=%s\n' \
        "$cache" "$size" "$clean" "$orph" "$orphrm" "$kern" "$kernrm"
}

# ---------------------------------------------------------------- run as a command
case "$0" in
    *distro.sh)
        set -u
        cmd=${1:-family}; [ $# -gt 0 ] && shift
        case "$cmd" in
            family)      echo "$FAM" ;;
            pkgname)     [ $# -ge 1 ] || { echo "usage: distro.sh pkgname <logical>" >&2; exit 1; }; pkg "$1" ;;
            install-cmd) [ $# -ge 1 ] || { echo "usage: distro.sh install-cmd <logical…>" >&2; exit 1; }; install_cmd "$@" ;;
            updates)     updates ;;
            upgrade-cmd) upgrade_cmd ;;
            storage)     storage ;;
            *)           sed -n '2,/^# Logical/{/^#/s/^# \{0,1\}//p}' "$0"; exit 1 ;;
        esac ;;
esac
