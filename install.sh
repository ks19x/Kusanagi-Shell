#!/bin/sh
# Kusanagi installer — any distro, any init, a TUI when you're at a terminal. Safe to re-run.
#
#   ./install.sh                     the TUI: pick compositors, your keybinds, review the plan, go
#   ./install.sh --dry-run           same, but only shows what it would do
#   ./install.sh -y --compositors=niri,mango   no questions (Kusanagi's default keys where free)
#   ./install.sh --plain             plain line output instead of the TUI
#   ./install.sh --no-packages | --no-services | --no-config    skip a step
#   ./install.sh --uninstall         remove the links + compositor includes (settings stay)
#   ./install.sh --print=niri        print Kusanagi's autostart + default keys for
#                                    mango|hyprland|niri|sway|labwc|kde|dwl
#   ./install.sh --no-build          don't build the shell (kusanagi-shell) at the end
#
# What it does:
#   1. installs Kusanagi's dependencies + the compositors you pick (pacman/AUR, xbps, dnf, emerge, …)
#   2. enables what a Wayland session needs (dbus, elogind/seatd, NetworkManager) for your init:
#      systemd, runit, OpenRC, dinit or s6 — only services that exist and aren't on yet
#   3. per compositor: keeps your keybinds and lets you pick free keys for Kusanagi, or lets
#      Kusanagi's keys win; writes kusanagi.<conf|lua|kdl> and adds ONE include line
#      (backup first, validated, reverted if invalid); links the `kusanagi` command.
#      labwc: a marked block in rc.xml + a line in autostart · KDE Plasma: an autostart entry +
#      command shortcuts in kglobalshortcutsrc · dwl: a keys file to #include in config.h + a start script
set -u

ROOT=$(cd "$(dirname "$0")" && pwd)
CFG="${XDG_CONFIG_HOME:-$HOME/.config}"
BIN="$HOME/.local/bin"
CACHE="${XDG_CACHE_HOME:-$HOME/.cache}/kusanagi"
KVERSION=$(cat "$ROOT/VERSION" 2>/dev/null || echo dev) # (not VERSION: os-release sets that)
PRINT=""
DRY=0
YES=0
PLAIN=0
DO_PKG=1
DO_SVC=1
DO_CFG=1
DO_BUILD=1
PICK=""
UNINSTALL=0

for a in "$@"; do
  case "$a" in
  --dry-run) DRY=1 ;;
  -y | --yes) YES=1 ;;
  --plain) PLAIN=1 ;;
  --no-packages) DO_PKG=0 ;;
  --no-services) DO_SVC=0 ;;
  --no-config) DO_CFG=0 ;;
  --no-build) DO_BUILD=0 ;;
  --compositors=*) PICK=$(echo "${a#*=}" | tr ',' ' ') ;;
  --uninstall) UNINSTALL=1 ;;
  --print=*) PRINT=${a#*=} ;;
  -h | --help)
    sed -n '2,24s/^# \{0,1\}//p' "$0"
    exit 0
    ;;
  *)
    echo "unknown option: $a (see --help)"
    exit 1
    ;;
  esac
done
KEYS=${KUSANAGI_KEYS:-/dev/tty} # (testing: feed keys from a fifo)
TUI=1
{ [ $PLAIN = 1 ] || [ $YES = 1 ] || ! [ -t 1 ]; } && TUI=0
[ "$KEYS" = /dev/tty ] && ! (exec </dev/tty) 2>/dev/null && TUI=0
have() { command -v "$1" >/dev/null 2>&1; }
short() { case "$1" in "$HOME"/*) printf '~/%s' "${1#"$HOME"/}" ;; *) printf '%s' "$1" ;; esac | awk '{ print (length($0) > 60) ? "…" substr($0, length($0) - 58) : $0 }'; }

# ================================================================ colours
ESC=$(printf '\033')
if [ -t 1 ]; then
  # every colour resets attributes first, so bold never bleeds into the next run
  R0="$ESC[0m"
  BO="$ESC[1m"
  DIM="$ESC[0;38;5;244m"
  TXT="$ESC[0;38;5;252m"
  WHT="$ESC[0;1;38;5;231m"
  RED="$ESC[0;38;5;167m"
  BLOOD="$ESC[0;38;5;124m"
  GOLD="$ESC[0;38;5;179m"
  STEEL="$ESC[0;38;5;250m"
  GRN="$ESC[0;38;5;114m"
  YEL="$ESC[0;38;5;221m"
  LINE="$ESC[0;38;5;240m"
else
  R0=""
  BO=""
  DIM=""
  TXT=""
  WHT=""
  RED=""
  BLOOD=""
  GOLD=""
  STEEL=""
  GRN=""
  YEL=""
  LINE=""
fi

# ================================================================ detect: distro, privileges, init
. "${KUSANAGI_OS_RELEASE:-/etc/os-release}" 2>/dev/null || true # (override: testing other distros)
ID=${ID:-unknown}
. "$ROOT/lib/distro.sh"            # -> FAM, ARTIX, pkg() (one package table, shared with `kusanagi doctor`)
[ "$FAM" = alpine ] && FAM=unknown # the installer doesn't drive apk yet: install by hand, as before

if [ "$(id -u)" = 0 ]; then
  SU=""
elif have doas; then
  SU=doas
elif have sudo; then
  SU=sudo
else SU=""; fi

if [ -n "${KUSANAGI_INIT:-}" ]; then
  INIT=$KUSANAGI_INIT # (override: testing other inits)
elif [ -d /run/systemd/system ]; then
  INIT=systemd
elif [ -d /run/runit ] || [ -d /etc/runit ] || { have sv && [ -d /var/service ]; }; then
  INIT=runit
elif have openrc || [ -d /run/openrc ]; then
  INIT=openrc
elif have dinitctl; then
  INIT=dinit
elif have s6-rc; then
  INIT=s6
else INIT=unknown; fi

# ================================================================ packages
# pkg <logical> -> package name(s) for $FAM: the table lives in lib/distro.sh
installed() { # is package $1 installed?
  case "$FAM" in
  arch) pacman -Q "$1" >/dev/null 2>&1 ;;
  void) xbps-query "$1" >/dev/null 2>&1 ;;
  fedora) rpm -q "$1" >/dev/null 2>&1 ;;
  gentoo) ls -d /var/db/pkg/"$1"-[0-9]* >/dev/null 2>&1 ;;
  debian) dpkg -s "$1" >/dev/null 2>&1 ;;
  suse) rpm -q "$1" >/dev/null 2>&1 ;;
  *) return 1 ;;
  esac
}
compute_packages() { # -> MISSING, UNPACKAGED
  WANT="shell-build python magick wl-clipboard cliphist grim slurp gammastep gamemode swappy foot font emoji nm pavucontrol hyprlock libnotify pipewire portal seat polkit"
  for c in $PICK; do WANT="$WANT $c"; done
  MISSING=""
  UNPACKAGED=""
  for w in $WANT; do
    p=$(pkg "$w")
    if [ -z "$p" ]; then
      case "$w" in seat | font | emoji) ;; *) UNPACKAGED="$UNPACKAGED $w" ;; esac
      continue
    fi
    for one in $p; do installed "$one" || MISSING="$MISSING $one"; done
  done
  MISSING=${MISSING# }
  UNPACKAGED=${UNPACKAGED# }
}

# ================================================================ services (init-agnostic)
svc_exists() {
  case "$INIT" in
  systemd) systemctl list-unit-files "$1.service" 2>/dev/null | grep -q "$1" ;;
  runit) [ -d "/etc/sv/$1" ] || [ -d "/etc/runit/sv/$1" ] ;;
  openrc) [ -x "/etc/init.d/$1" ] ;;
  dinit) [ -f "/etc/dinit.d/$1" ] ;;
  s6) [ -d "/etc/s6/sv/$1" ] || s6-rc-db list all 2>/dev/null | grep -qx "$1" ;;
  *) return 1 ;;
  esac
}
svc_enabled() {
  case "$INIT" in
  systemd) systemctl is-enabled "$1" >/dev/null 2>&1 ;;
  runit) [ -e "/var/service/$1" ] || [ -e "/run/runit/service/$1" ] || [ -e "/etc/runit/runsvdir/default/$1" ] ;;
  openrc) rc-update show 2>/dev/null | grep -qw "$1" ;;
  dinit) [ -e "/etc/dinit.d/boot.d/$1" ] ;;
  s6) s6-rc-db -c /etc/s6/rc/compiled atomics default 2>/dev/null | grep -qx "$1" ;;
  *) return 0 ;;
  esac
}
svc_cmd() { # the command(s) enabling $1, one per line
  case "$INIT" in
  systemd) echo "$SU systemctl enable --now $1" ;;
  runit) if [ -d /etc/sv/"$1" ]; then
    echo "$SU ln -s /etc/sv/$1 /var/service/"
  else echo "$SU ln -s /etc/runit/sv/$1 /run/runit/service/"; fi ;;
  openrc)
    echo "$SU rc-update add $1 default"
    echo "$SU rc-service $1 start"
    ;;
  dinit) echo "$SU dinitctl enable $1" ;;
  s6)
    echo "$SU s6-service add default $1"
    echo "$SU s6-db-reload"
    ;;
  esac
}
compute_services() { # -> SVC_TODO
  SVC_TODO=""
  [ "$INIT" = unknown ] && return
  for s in dbus elogind NetworkManager polkitd polkit; do
    # systemd: logind is built in, and polkit starts on demand
    [ "$INIT" = systemd ] && case "$s" in elogind | polkitd | polkit) continue ;; esac
    svc_exists "$s" || continue
    svc_enabled "$s" || SVC_TODO="$SVC_TODO $s"
  done
  SVC_TODO=${SVC_TODO# }
}

# ================================================================ keybinds
# id | label | default key | command
ACTIONS='launcher|Apps launcher|SUPER+space|kusanagi msg launcher toggle
wallpaper|Wallpapers|SUPER+a|kusanagi msg wallpaper toggle
clipboard|Clipboard history|SUPER+v|kusanagi msg clipboard toggle
inbox|Notifications|SUPER+n|kusanagi msg notifs toggle
settings|Settings|SUPER+i|kusanagi msg settings toggle
lock|Lock screen|SUPER+l|kusanagi msg lock lock
gamemode|Game mode|SUPER+g|kusanagi msg gamemode toggle
power|Power menu|SUPER+grave|kusanagi msg power toggle
panel|Control panel|SUPER+b|kusanagi msg panel toggle
shotfull|Screenshot|Print|kusanagi screenshot full
shotregion|Screenshot region|SUPER+SHIFT+s|kusanagi screenshot region
colorpick|Colour picker|SUPER+SHIFT+c|kusanagi colorpick
volup|Volume up|XF86AudioRaiseVolume|kusanagi msg volume up
voldown|Volume down|XF86AudioLowerVolume|kusanagi msg volume down
mute|Mute|XF86AudioMute|kusanagi msg volume mute
micmute|Mute the mic|XF86AudioMicMute|kusanagi msg mic mute
brightup|Brightness up|XF86MonBrightnessUp|kusanagi msg brightness up
brightdown|Brightness down|XF86MonBrightnessDown|kusanagi msg brightness down
play|Play / pause|XF86AudioPlay|kusanagi msg media toggle
next|Next track|XF86AudioNext|kusanagi msg media next
prev|Previous track|XF86AudioPrev|kusanagi msg media previous'
# the media keys aren't in the key picker: they go to Kusanagi wherever they're free (or everywhere: "Kusanagi wins")
MEDIA_IDS="volup voldown mute micmute brightup brightdown play next prev"
action_ids() { printf '%s\n' "$ACTIONS" | cut -d'|' -f1; }
binder_ids() { action_ids | grep -vxF "$(printf '%s\n' $MEDIA_IDS)"; }
action_field() { printf '%s\n' "$ACTIONS" | awk -F'|' -v id="$1" -v f="$2" '$1==id {print $f}'; }

# shared awk: norm() turns any compositor's way of writing a combo into SUPER+CTRL+ALT+SHIFT+KEY (upper)
AWK_NORM='
function norm(s,   n, t, i, u, sup, ctl, alt, sh, key) {
    s = toupper(s); gsub(/SUPER_/, "SUPER+", s); gsub(/SHIFT_/, "SHIFT+", s); gsub(/CTRL_/, "CTRL+", s)
    gsub(/CONTROL_/, "CONTROL+", s); gsub(/ALT_/, "ALT+", s); gsub(/MOD4_/, "MOD4+", s)
    gsub(/[ \t]*\+[ \t]*/, "+", s); gsub(/[ \t]+/, "+", s)
    n = split(s, t, "+"); sup = ctl = alt = sh = 0; key = ""
    for (i = 1; i <= n; i++) {
        u = t[i]
        if (u == "") continue
        if (u ~ /^(SUPER|MOD4|MOD|LOGO|WIN|META|\$MAINMOD|\$MOD)$/) sup = 1
        else if (u ~ /^(CTRL|CONTROL)$/) ctl = 1
        else if (u ~ /^(ALT|MOD1)$/) alt = 1
        else if (u == "SHIFT") sh = 1
        else if (u == "NONE") continue
        else key = u
    }
    if (key == "ENTER") key = "RETURN"; if (key == "ESC") key = "ESCAPE"; if (key == "`") key = "GRAVE"
    if (key == "") return ""
    return (sup ? "SUPER+" : "") (ctl ? "CTRL+" : "") (alt ? "ALT+" : "") (sh ? "SHIFT+" : "") key
}'

# user-typed combo ("super+shift+k", "mod+enter", "print") -> stored form: MODS+keysym (keysym in its real case)
parse_combo() {
  printf '%s\n' "$1" | awk '
    { s = tolower($0); gsub(/[ \t]/, "", s); n = split(s, t, "+"); m = ""; key = ""; bad = 0
      for (i = 1; i <= n; i++) { u = t[i]; if (u == "") continue
        if (u ~ /^(super|mod|mod4|win|meta|logo|cmd)$/) sup = 1
        else if (u ~ /^(ctrl|control)$/) ctl = 1
        else if (u ~ /^(alt|mod1)$/) alt = 1
        else if (u == "shift") sh = 1
        else if (key != "") bad = 1
        else key = u }
      map["space"]="space"; map["grave"]="grave"; map["`"]="grave"; map["print"]="Print"; map["prtsc"]="Print"
      map["return"]="Return"; map["enter"]="Return"; map["escape"]="Escape"; map["esc"]="Escape"; map["tab"]="Tab"
      map["backspace"]="BackSpace"; map["delete"]="Delete"; map["del"]="Delete"; map["insert"]="Insert"
      map["home"]="Home"; map["end"]="End"; map["up"]="Up"; map["down"]="Down"; map["left"]="Left"; map["right"]="Right"
      map["pageup"]="Page_Up"; map["pagedown"]="Page_Down"; map["slash"]="slash"; map["/"]="slash"; map["backslash"]="backslash"
      map["comma"]="comma"; map[","]="comma"; map["period"]="period"; map["."]="period"; map["minus"]="minus"; map["-"]="minus"
      map["equal"]="equal"; map["="]="equal"; map["semicolon"]="semicolon"; map[";"]="semicolon"; map["apostrophe"]="apostrophe"
      map["bracketleft"]="bracketleft"; map["["]="bracketleft"; map["bracketright"]="bracketright"; map["]"]="bracketright"; map["pause"]="Pause"
      if (key in map) key = map[key]
      else if (key ~ /^f[0-9]+$/) key = toupper(key)
      else if (key ~ /^[a-z0-9]$/) key = key
      else if (key ~ /^xf86/) key = "XF86" substr($0, index(tolower($0), "xf86") + 4)
      else bad = 1
      if (bad || key == "") { print ""; exit }
      print (sup ? "SUPER+" : "") (ctl ? "CTRL+" : "") (alt ? "ALT+" : "") (sh ? "SHIFT+" : "") key }'
}
canon() { printf '%s\n' "$1" | awk "$AWK_NORM"'{ print norm($0) }'; }
pretty() { # SUPER+SHIFT+s -> Super + Shift + S
  [ -z "$1" ] && {
    printf '—'
    return
  }
  printf '%s\n' "$1" | awk -F'+' '{ o = ""; for (i = 1; i <= NF; i++) { u = $i
        if (i < NF) u = substr(u, 1, 1) tolower(substr(u, 2)); else if (length(u) == 1) u = toupper(u); else if (u == "grave") u = "`"; else u = toupper(substr(u, 1, 1)) substr(u, 2)
        o = o (i > 1 ? " + " : "") u } printf "%s", o }'
}

# every compositor the installer knows, in picker order
ALL_COMPS="mango hyprland niri sway labwc kde dwl"
# where each compositor keeps its config
conf_dir() {
  case "$1" in
  mango) echo "$CFG/mango" ;; hyprland) echo "$CFG/hypr" ;; niri) echo "$CFG/niri" ;; sway) echo "$CFG/sway" ;;
  labwc) echo "$CFG/labwc" ;; kde) echo "$CFG/autostart" ;; dwl) echo "$CFG/dwl" ;;
  esac
}
conf_main() { # the file holding the user's keybinds ("" = none we can read: dwl compiles them in)
  case "$1" in
  mango) echo "$CFG/mango/config.conf" ;;
  hyprland) if [ -f "$CFG/hypr/hyprland.lua" ]; then echo "$CFG/hypr/hyprland.lua"; else echo "$CFG/hypr/hyprland.conf"; fi ;;
  niri) echo "$CFG/niri/config.kdl" ;;
  sway) echo "$CFG/sway/config" ;;
  labwc) echo "$CFG/labwc/rc.xml" ;;
  kde) echo "$CFG/kglobalshortcutsrc" ;;
  dwl) echo "" ;;
  esac
}
comp_bin() { case "$1" in mango) echo mango ;; hyprland) echo Hyprland ;; niri) echo niri ;; sway) echo sway ;; labwc) echo labwc ;; kde) echo kwin_wayland ;; dwl) echo dwl ;; esac }
comp_name() { case "$1" in mango) echo MangoWM ;; hyprland) echo Hyprland ;; niri) echo niri ;; sway) echo sway ;; labwc) echo labwc ;; kde) echo "KDE Plasma" ;; dwl) echo dwl ;; esac }
# sway reads /etc/sway/config until you have your own: the installer starts yours from it
seed_conf() { [ "$1" = sway ] && [ -f /etc/sway/config ] && echo /etc/sway/config; }
LABWC_AUTOSTART_TAG="# kusanagi autostart (added by install.sh)"
# already wired up anywhere in that compositor's config folder (sourced/rice files included)?
wired() { grep -RqsI --exclude='*.bak*' --exclude='kusanagi.*' -e 'kusanagi msg' -e 'kusanagi start' -e 'kusanagi restart' -e 'exec-once=kusanagi' -e '"kusanagi:' "$1" 2>/dev/null; }
wired_comp() { # wired_comp <compositor>: the user's own config starts Kusanagi / binds it (Kusanagi's own lines don't count)
  case "$1" in
  labwc)
    d="$CFG/labwc"
    { [ -f "$d/rc.xml" ] && sed '/kusanagi:begin/,/kusanagi:end/d' "$d/rc.xml"
      [ -f "$d/autostart" ] && grep -vF "$LABWC_AUTOSTART_TAG" "$d/autostart"; } 2>/dev/null | grep -q -e 'kusanagi msg' -e '^[[:space:]]*kusanagi\([[:space:]]\|$\)' ;;
  kde) wired "$CFG/autostart" ;;
  sway) wired "$CFG/sway" || grep -RqsI --exclude='*.bak*' --exclude='kusanagi.*' -E '^[[:space:]]*exec(_always)?[[:space:]]+kusanagi[[:space:]]*$' "$CFG/sway" ;;
  *) wired "$(conf_dir "$1")" ;;
  esac
}

# every bind the user already has: lines "CANON<TAB>file:line<TAB>what it does"
scan_binds() {
  d=$(conf_dir "$1")
  # (sway falls back to its default config, labwc has default keys, KDE keeps them outside conf_dir)
  [ -d "$d" ] || case "$1" in sway | labwc | kde) ;; *) return 0 ;; esac
  case "$1" in
  mango)
    grep -rnI --include='*.conf' --exclude='kusanagi.conf' --exclude='*.bak*' -E '^[[:space:]]*bind[a-z]*[[:space:]]*=' "$d" 2>/dev/null |
      awk "$AWK_NORM"'
            { f = $0; p1 = index(f, ":"); file = substr(f, 1, p1 - 1); f = substr(f, p1 + 1)
              p2 = index(f, ":"); ln = substr(f, 1, p2 - 1); body = substr(f, p2 + 1)
              sub(/^[^=]*=[ \t]*/, "", body); n = split(body, a, ",")
              if (n < 3) next
              c = norm(a[1] "+" a[2]); what = a[3]; for (i = 4; i <= n; i++) what = what "," a[i]
              gsub(/^[ \t]+/, "", what); if (c != "") printf "%s\t%s:%s\t%s\n", c, file, ln, what }'
    ;;
  hyprland)
    if [ -f "$d/hyprland.lua" ]; then
      if [ -n "${HYPRLAND_INSTANCE_SIGNATURE:-}" ] && hyprctl binds >/dev/null 2>&1; then
        # Hyprland is running: ask it (exact, whatever the Lua does)
        hyprctl binds 2>/dev/null | awk '
                    /^[ \t]*modmask:/ { m = $2 }  /^[ \t]*key:/ { k = $2 }
                    /^[ \t]*dispatcher:/ { d = $2 }
                    /^[ \t]*arg:/ { a = $0; sub(/^[ \t]*arg:[ \t]*/, "", a)
                        if (k == "") next
                        c = (int(m / 64) % 2 ? "SUPER+" : "") (int(m / 4) % 2 ? "CTRL+" : "") (int(m / 8) % 2 ? "ALT+" : "") (m % 2 ? "SHIFT+" : "") toupper(k)
                        printf "%s\tHyprland (running)\t%s %s\n", c, d, a; k = "" }'
      else
        # not running: every quoted "MOD + KEY" string in the Lua files
        grep -rnI --include='*.lua' --exclude='kusanagi.lua' --exclude='*.bak*' -E '"[^"]*(SUPER|ALT|CTRL|SHIFT|Super|Alt|Ctrl|Shift)[^"]*"|"Print"' "$d" 2>/dev/null |
          awk "$AWK_NORM"'
                    { f = $0; p1 = index(f, ":"); file = substr(f, 1, p1 - 1); f = substr(f, p1 + 1)
                      p2 = index(f, ":"); ln = substr(f, 1, p2 - 1); body = substr(f, p2 + 1)
                      s = body
                      while (match(s, /"[^"]*"/)) { q = substr(s, RSTART + 1, RLENGTH - 2); s = substr(s, RSTART + RLENGTH)
                          if (q !~ /^[A-Za-z0-9_ +`]+$/ || q !~ /[+ ]/ && q != "Print") continue
                          c = norm(q); if (c == "" || c !~ /\+/ && c != "PRINT") continue
                          w = body; gsub(/^[ \t]+|[ \t]+$/, "", w); printf "%s\t%s:%s\t%s\n", c, file, ln, w } }'
      fi
    else
      files=$(find "$d" -name '*.conf' ! -name 'kusanagi.conf' ! -name '*.bak*' 2>/dev/null)
      [ -z "$files" ] && return 0
      # $variables first (e.g. $mainMod = SUPER), then the bind lines
      {
        grep -hE '^[[:space:]]*\$[A-Za-z_]+[[:space:]]*=' $files | sed 's/^/VAR /'
        grep -nHE '^[[:space:]]*bind[a-z]*[[:space:]]*=' $files
      } 2>/dev/null |
        awk "$AWK_NORM"'
                /^VAR / { v = $0; sub(/^VAR[ \t]*/, "", v); k = v; sub(/[ \t]*=.*/, "", k); sub(/^[^=]*=[ \t]*/, "", v); vars[k] = v; next }
                { f = $0; p1 = index(f, ":"); file = substr(f, 1, p1 - 1); f = substr(f, p1 + 1)
                  p2 = index(f, ":"); ln = substr(f, 1, p2 - 1); body = substr(f, p2 + 1)
                  sub(/^[^=]*=[ \t]*/, "", body); n = split(body, a, ",")
                  if (n < 3) next
                  mods = a[1]; for (k in vars) gsub("\\" k, vars[k], mods)
                  c = norm(mods "+" a[2]); what = a[3]; for (i = 4; i <= n; i++) what = what "," a[i]
                  gsub(/^[ \t]+/, "", what); if (c != "") printf "%s\t%s:%s\t%s\n", c, file, ln, what }'
    fi
    ;;
  niri)
    for f in $(find "$d" -name '*.kdl' ! -name 'kusanagi.kdl' ! -name '*.bak*' 2>/dev/null); do
      awk -v file="$f" "$AWK_NORM"'
                { line = $0; sub(/\/\/.*/, "", line) }
                !inb && line ~ /^[ \t]*binds[ \t]*\{/ { inb = 1; depth = 1; next }
                inb {
                    if (depth == 1 && line ~ /^[ \t]*[A-Za-z0-9_+]+[^{]*\{/) {
                        tok = line; sub(/^[ \t]+/, "", tok); sub(/[^A-Za-z0-9_+].*/, "", tok)
                        what = substr(line, index(line, "{") + 1); sub(/\}[ \t]*$/, "", what); gsub(/^[ \t]+|[ \t;]+$/, "", what)
                        c = norm(tok); if (c != "") printf "%s\t%s:%d\t%s\n", c, file, FNR, what }
                    o = gsub(/\{/, "{", line); cl = gsub(/\}/, "}", line); depth += o - cl
                    if (depth <= 0) inb = 0 }' "$f"
    done
    ;;
  sway)
    # $variables (set $mod Mod4), then every bindsym outside a mode { } block; your config, else sway's default
    files=$(find "$d" -type f ! -name 'kusanagi.*' ! -name '*.bak*' 2>/dev/null)
    [ -z "$files" ] && files=$(seed_conf sway)
    [ -z "$files" ] && return 0
    for f in $files; do
      awk -v file="$f" "$AWK_NORM"'
                { line = $0; sub(/^[ \t]+/, "", line) }
                line ~ /^#/ { next }
                line ~ /^set[ \t]+\$/ { split(line, w, /[ \t]+/); vars[w[2]] = w[3]; next }
                line ~ /^mode[ \t].*\{[ \t]*$/ { inmode = 1; next }
                inmode { if (line ~ /^\}/) inmode = 0; next }
                line ~ /^bindsym[ \t]/ {
                    n = split(line, w, /[ \t]+/); i = 2
                    while (i <= n && w[i] ~ /^--/) i++
                    combo = w[i]; for (k in vars) gsub("\\" k, vars[k], combo)
                    what = ""; for (j = i + 1; j <= n; j++) what = what (what ? " " : "") w[j]
                    c = norm(combo); if (c != "") printf "%s\t%s:%d\t%s\n", c, file, FNR, what }' "$f"
    done
    ;;
  labwc)
    # <keybind key="W-S-s"> in rc.xml (Kusanagi's own block left out), plus labwc's defaults while they're on
    f="$d/rc.xml"
    if [ -f "$f" ]; then
      sed '/kusanagi:begin/,/kusanagi:end/s/.*//' "$f" | awk -v file="$f" "$AWK_NORM"'
                { line = $0 }
                line ~ /<keybind[ \t]/ && match(line, /key="[^"]*"/) {
                    k = substr(line, RSTART + 5, RLENGTH - 6); n = split(k, p, "-"); m = ""
                    for (i = 1; i < n; i++) m = m (p[i] == "W" ? "SUPER" : p[i] == "C" ? "CTRL" : p[i] == "A" ? "ALT" : p[i] == "S" ? "SHIFT" : p[i]) "+"
                    what = line; sub(/.*<keybind[^>]*>/, "", what); gsub(/<[^>]*action name="/, "", what); gsub(/".*/, "", what)
                    c = norm(m p[n]); if (c != "") printf "%s\t%s:%d\t%s\n", c, file, NR, (what ? what : "keybind") }'
    fi
    if [ ! -f "$f" ] || sed 's/<!--.*-->//' "$f" | grep -q '<default' || ! sed '/kusanagi:begin/,/kusanagi:end/d' "$f" | grep -q '<keybind'; then
      printf '%s\tlabwc default\t%s\n' "SUPER+RETURN" "terminal" "ALT+TAB" "next window" "ALT+F3" "menu" "ALT+F4" "close" \
        "SUPER+A" "toggle maximize" "ALT+SPACE" "window menu" "XF86AUDIORAISEVOLUME" "volume (amixer)" \
        "XF86AUDIOLOWERVOLUME" "volume (amixer)" "XF86AUDIOMUTE" "mute (amixer)" \
        "XF86MONBRIGHTNESSUP" "brightness (brightnessctl)" "XF86MONBRIGHTNESSDOWN" "brightness (brightnessctl)"
    fi
    ;;
  kde)
    # kglobalshortcutsrc: Action=current\tother,default,Friendly name — Kusanagi's own [services][kusanagi-*] left out
    f=$(conf_main kde)
    [ -f "$f" ] || return 0
    awk -v file="$f" "$AWK_NORM$AWK_QT"'
                /^\[/ { grp = $0; ours = ($0 ~ /^\[services\]\[kusanagi-key-/); next }
                ours || !/=/ || /^_k_friendly_name/ || /^#/ { next }
                { name = substr($0, 1, index($0, "=") - 1); v = substr($0, index($0, "=") + 1)
                  cur = v; if (index(cur, ",")) cur = substr(cur, 1, index(cur, ",") - 1)
                  if (cur == "" || cur == "none") next
                  n = split(cur, keys, /\\t/)
                  for (i = 1; i <= n; i++) { c = norm(fromqt(keys[i])); if (c != "") printf "%s\t%s:%d\t%s %s\n", c, file, NR, grp, name } }' "$f"
    ;;
  esac
}
# Qt key names (KDE) <-> the keysyms the installer stores: fromqt("Meta+Volume Up") = "Meta+XF86AudioRaiseVolume"
AWK_QT='
function qtmap(i) {
    split("XF86AudioRaiseVolume=Volume Up|XF86AudioLowerVolume=Volume Down|XF86AudioMute=Volume Mute|XF86AudioMicMute=Microphone Mute|XF86AudioPlay=Media Play|XF86AudioNext=Media Next|XF86AudioPrev=Media Previous|XF86AudioStop=Media Stop|XF86MonBrightnessUp=Monitor Brightness Up|XF86MonBrightnessDown=Monitor Brightness Down|space=Space|grave=`|Return=Return|Escape=Esc|BackSpace=Backspace|Delete=Del|Insert=Ins|Page_Up=PgUp|Page_Down=PgDown|slash=/|backslash=\\|comma=,|period=.|minus=-|equal==|semicolon=;|apostrophe=\047|bracketleft=[|bracketright=]", QT_PAIRS, "|")
    return QT_PAIRS[i]
}
function fromqt(s,   i, p, sym, qt) {
    for (i = 1; i <= 29; i++) { p = qtmap(i); sym = substr(p, 1, index(p, "=") - 1); qt = substr(p, index(p, "=") + 1)
        if (qt ~ / / && index(s, qt)) { sub(qt, sym, s); return s } }
    return s
}
function toqt(k,   i, p) {
    for (i = 1; i <= 29; i++) { p = qtmap(i); if (substr(p, 1, index(p, "=") - 1) == k) return substr(p, index(p, "=") + 1) }
    return (length(k) == 1) ? toupper(k) : k
}'
clash() { # clash <compositor> <stored combo>: print the user bind it collides with (if any)
  k=$(canon "$2")
  [ -z "$k" ] && return 1
  eval "list=\${SCAN_$1:-}"
  printf '%s\n' "$list" | awk -F'\t' -v k="$k" '$1 == k { print $3 "  (" $2 ")"; found = 1; exit } END { exit !found }'
}
getb() { eval "printf '%s' \"\${B_$1_$2:-}\""; }
setb() { eval "B_$1_$2=\$3"; }

# kusanagi.<ext> for a compositor, from the chosen binds (MODE_<c>: keep | win | none | fresh)
fmt_key() { # fmt_key <format> <stored combo>   format: mango hyprlua hyprconf niri sway labwc kde dwl
  printf '%s\n' "$2" | awk -F'+' -v c="$1" "$AWK_QT"'{
        key = $NF; mods = ""; sh = 0
        for (i = 1; i < NF; i++) {
            m = $i; if (m == "SHIFT") sh = 1
            if (c == "mango") mods = mods (mods ? "+" : "") m
            else if (c == "hyprlua") mods = mods m " + "
            else if (c == "hyprconf") mods = mods (mods ? " " : "") m
            else if (c == "sway") mods = mods (m == "SUPER" ? "Mod4" : m == "CTRL" ? "Control" : m == "ALT" ? "Mod1" : "Shift") "+"
            else if (c == "labwc") mods = mods (m == "SUPER" ? "W" : m == "CTRL" ? "C" : m == "ALT" ? "A" : "S") "-"
            else if (c == "kde") mods = mods (m == "SUPER" ? "Meta" : m == "CTRL" ? "Ctrl" : m == "ALT" ? "Alt" : "Shift") "+"
            else if (c == "dwl") mods = mods (mods ? "|" : "") "WLR_MODIFIER_" (m == "SUPER" ? "LOGO" : m)
            else mods = mods substr(m, 1, 1) tolower(substr(m, 2)) "+" }
        if (c == "kde") { printf "%s%s", mods, toqt(key); exit }
        if (length(key) == 1) key = (c == "mango" || c == "sway" || c == "labwc") ? tolower(key) : (c == "dwl" && !sh) ? tolower(key) : toupper(key)
        if (c == "mango") printf "%s,%s", (mods ? mods : "NONE"), key
        else if (c == "hyprlua") printf "%s%s", mods, key
        else if (c == "hyprconf") printf "%s, %s", mods, key
        else if (c == "dwl") printf "%s, XKB_KEY_%s", (mods ? mods : "0"), key
        else printf "%s%s", mods, key }'
}
is_media_key() { case "${1##*+}" in XF86*) return 0 ;; *) return 1 ;; esac; }
# every bound action of <compositor>: lines "id<TAB>combo<TAB>command<TAB>label<TAB>over" (over=1: wins a clash)
bound() {
  eval "bmode=\${MODE_$1:-fresh}"
  [ "$bmode" = none ] && return 0
  for id in $(action_ids); do
    b=$(getb "$1" "$id")
    [ -z "$b" ] && continue
    over=0
    [ "$bmode" = win ] && clash "$1" "$b" >/dev/null && over=1
    printf '%s\t%s\t%s\t%s\t%s\n' "$id" "$b" "$(action_field "$id" 4)" "$(action_field "$id" 2)" "$over"
  done
}
kbin() { printf '%s' "$BIN/kusanagi"; } # desktop files get the full path (no shell, no PATH guesswork)
kde_app_dir() { echo "${XDG_DATA_HOME:-$HOME/.local/share}/applications"; }
gen_conf() { # gen_conf <compositor> -> stdout (the file Kusanagi writes for it)
  c=$1
  lua=0
  [ "$c" = hyprland ] && [ -f "$CFG/hypr/hyprland.lua" ] && lua=1
  case "$c" in
  mango)
    echo "# Kusanagi for MangoWM — written by install.sh (re-run it to change keys)"
    echo "exec-once=kusanagi"
    echo "# no blur or open/close animation on the shell's own surfaces (with blur_optimized a blurred"
    echo "# panel would show the wallpaper instead of your windows); mango needs a flat pattern here"
    echo 'layerrule=noblur:1,layer_name:^kusanagi-.*$'
    echo 'layerrule=noanim:1,layer_name:^kusanagi-.*$'
    echo "# gaps / borders from Settings → Display → Windows (empty unless you turn that on)"
    echo "source-optional=~/.config/kusanagi/mango.conf"
    ;;
  niri)
    echo "// Kusanagi for niri — written by install.sh (re-run it to change keys)"
    echo 'spawn-at-startup "kusanagi"'
    ;;
  hyprland) if [ $lua = 1 ]; then
    echo "-- Kusanagi for Hyprland — written by install.sh (re-run it to change keys)"
    echo 'hl.on("hyprland.start", function() hl.exec_cmd("kusanagi") end)'
  else
    echo "# Kusanagi for Hyprland — written by install.sh (re-run it to change keys)"
    echo "exec-once = kusanagi"
  fi ;;
  sway)
    echo "# Kusanagi for sway — written by install.sh (re-run it to change keys)"
    echo "exec kusanagi"
    ;;
  labwc)
    echo "    <!-- kusanagi:begin (written by install.sh, re-run it to change keys) -->"
    ;;
  kde)
    echo "# Kusanagi for KDE Plasma — written by install.sh: command shortcuts for $(kde_app_dir)/kusanagi-*.desktop"
    ;;
  dwl)
    echo "/* Kusanagi for dwl — written by install.sh (re-run it to change keys)."
    echo " * In your config.h, add this line inside  static const Key keys[] = { … };  then rebuild dwl:"
    echo " *     #include \"$CFG/dwl/kusanagi.h\""
    echo " */"
    ;;
  esac
  [ "$c" = niri ] && bound niri | grep -q . && {
    echo ""
    echo "binds {"
  }
  bound "$c" | while IFS='	' read -r id b cmd label over; do
    case "$c" in
    mango) if is_media_key "$b"; then echo "bindl=$(fmt_key mango "$b"),spawn,$cmd"; else echo "bind=$(fmt_key mango "$b"),spawn,$cmd"; fi ;;
    hyprland) if [ $lua = 1 ]; then
      k=$(fmt_key hyprlua "$b")
      [ $over = 1 ] && echo "hl.unbind(\"$k\")"
      echo "hl.bind(\"$k\", hl.dsp.exec_cmd(\"$cmd\"))"
    else
      k=$(fmt_key hyprconf "$b")
      [ $over = 1 ] && echo "unbind = $k"
      if is_media_key "$b"; then echo "bindl = $k, exec, $cmd"; else echo "bind = $k, exec, $cmd"; fi
    fi ;;
    niri)
      args=$(printf '%s\n' "$cmd" | awk '{ for (i = 1; i <= NF; i++) printf "%s\"%s\"", (i > 1 ? " " : ""), $i }')
      lk=""
      is_media_key "$b" && lk=" allow-when-locked=true"
      echo "    $(fmt_key niri "$b")$lk hotkey-overlay-title=\"Kusanagi: $label\" { spawn $args; }"
      ;;
    # sway: a later bindsym on the same keys replaces the earlier one, and this file is included last
    sway) echo "bindsym $(fmt_key sway "$b") exec $cmd" ;;
    labwc) echo "    <keybind key=\"$(fmt_key labwc "$b")\"><action name=\"Execute\" command=\"$cmd\" /></keybind>" ;;
    kde)
      echo "[services][kusanagi-key-$id.desktop]"
      echo "_launch=$(fmt_key kde "$b")"
      ;;
    dwl)
      args=$(printf '%s\n' "$cmd" | awk '{ for (i = 1; i <= NF; i++) printf "\"%s\", ", $i }')
      echo "	{ $(fmt_key dwl "$b"), spawn, { .v = (const char *[]){ ${args}NULL } } }, /* $label */"
      ;;
    esac
  done
  [ "$c" = niri ] && bound niri | grep -q . && echo "}"
  [ "$c" = labwc ] && echo "    <!-- kusanagi:end -->"
  return 0
}
# the other files a compositor needs: gen_extra <compositor> <what>
gen_extra() {
  case "$1:$2" in
  labwc:autostart) echo "kusanagi >/dev/null 2>&1 &  $LABWC_AUTOSTART_TAG" ;;
  kde:autostart)
    printf '%s\n' "[Desktop Entry]" "Type=Application" "Name=Kusanagi" "Comment=Kusanagi desktop shell (written by install.sh)" \
      "Exec=$(kbin)" "Icon=preferences-desktop" "OnlyShowIn=KDE;" "X-KDE-autostart-phase=2" ;;
  kde:app) # kde:app <id>: the launcher entry a command shortcut runs
    printf '%s\n' "[Desktop Entry]" "Type=Application" "Name=Kusanagi: $(action_field "$3" 2)" \
      "Exec=$(kbin)$(action_field "$3" 4 | sed 's/^kusanagi//')" "NoDisplay=true" "StartupNotify=false" "X-KDE-GlobalAccel-CommandShortcut=true" ;;
  dwl:session)
    cat <<EOF
#!/bin/sh
# Kusanagi for dwl — written by install.sh. Start your dwl session with this script (from a TTY, or as the
# session command of your login manager): it starts dwl with Kusanagi as its startup command.
# Already start dwl with  -s <something>?  Put that in here instead, followed by:  kusanagi &
export XDG_CURRENT_DESKTOP="\${XDG_CURRENT_DESKTOP:-dwl}"
# exec keeps this PID: Kusanagi's "Log out" ends dwl through it (dwl has no IPC of its own)
export KUSANAGI_DWL_PID=\$\$
exec dwl -s "kusanagi" "\$@"
EOF
    ;;
  esac
}
include_line() {
  case "$1" in
  mango) echo "source=./kusanagi.conf" ;;
  hyprland) if [ -f "$CFG/hypr/hyprland.lua" ]; then
    echo 'dofile(os.getenv("HOME") .. "/.config/hypr/kusanagi.lua")'
  else echo "source = $CFG/hypr/kusanagi.conf"; fi ;;
  niri) echo 'include "kusanagi.kdl"' ;;
  sway) echo "include $CFG/sway/kusanagi.conf" ;;
  esac
}
gen_name() {
  case "$1" in
  mango | sway) echo kusanagi.conf ;; niri) echo kusanagi.kdl ;; labwc) echo kusanagi.xml ;; kde) echo kusanagi.kglobalshortcutsrc ;; dwl) echo kusanagi.h ;;
  hyprland) [ -f "$CFG/hypr/hyprland.lua" ] && echo kusanagi.lua || echo kusanagi.conf ;;
  esac
}

# ================================================================ the TUI
LOGO_T='             o
            (H)
            (h)
            (H)
          ==###==
KKKKK       [|]KKKKKK
KKKKK       [|]KKKK
KKKKK      K[|]KK
KKKKK    KKK[|]
KKKKKKKKKKKK[|]
KKKKKKKKKKKK[|]
KKKKK    KKKKKK
KKKKK      KKKKKK
KKKKK       [KKKKKK
KKKKK       [|]KKKKKK
             v'
LOGO_W=22
logo_colored() { # template letters -> coloured blocks (K = the letter, [|] = blade, (H)(h) = grip, o = pommel, = # = guard)
  printf '%s\n' "$LOGO_T" | awk -v W="$WHT" -v S="$STEEL" -v B="$BLOOD" -v R="$RED" -v G="$GOLD" -v Z="$R0" '
    BEGIN { c["K"] = W; g["K"] = "█"; c["["] = S; g["["] = "▐"; c["|"] = S; g["|"] = "█"; c["]"] = S; g["]"] = "▌"
            c["("] = B; g["("] = "▐"; c[")"] = B; g[")"] = "▌"; c["H"] = B; g["H"] = "█"; c["h"] = R; g["h"] = "▓"
            c["o"] = G; g["o"] = "▄"; c["="] = G; g["="] = "▀"; c["#"] = G; g["#"] = "█"; c["v"] = S; g["v"] = "▼" }
    { o = ""; cur = ""; n = split($0, ch, "")
      for (i = 1; i <= n; i++) { x = ch[i]
          if (x in g) { if (c[x] != cur) { o = o Z c[x]; cur = c[x] } o = o g[x] }
          else { if (cur != "") { o = o Z; cur = "" } o = o x } }
      print o Z }'
}

ST=""
W=70
MENU_TMP=$(mktemp)
tui_on() {
  ST=$(stty -g </dev/tty 2>/dev/null)
  stty -icanon -echo min 0 time 3 </dev/tty 2>/dev/null
  printf '%s' "$ESC[?1049h$ESC[?25l$ESC[2J"
  trap 'tui_off' EXIT
  trap 'tui_off; exit 130' INT TERM
  LASTGEO=""
}
tui_off() {
  [ -n "$ST" ] && stty "$ST" </dev/tty 2>/dev/null
  printf '%s' "$ESC[?25h$ESC[?1049l"
  ST=""
  [ -n "${KEEPALIVE:-}" ] && kill "$KEEPALIVE" 2>/dev/null
  rm -f "$MENU_TMP"
  trap - EXIT INT TERM
}
tui_size() {
  s=$(stty size </dev/tty 2>/dev/null)
  ROWS=${s% *}
  COLS=${s#* }
  case "$ROWS$COLS" in '' | *[!0-9]*)
    ROWS=${LINES:-30}
    COLS=${COLUMNS:-100}
    ;;
  esac
}
vlen() { printf '%s' "$1" | sed "s/$ESC\[[0-9;]*m//g" | wc -m; }
rep() {
  n=$2
  o=""
  while [ "$n" -gt 0 ]; do
    o="$o$1"
    n=$((n - 1))
  done
  printf '%s' "$o"
}
at() { OUT="$OUT$ESC[$1;$2H"; }

readkey() { # -> K; K=resize when the terminal changed size (reads time out every 0.3 s)
  while :; do
    k=$(
      dd bs=8 count=1 2>/dev/null <"$KEYS"
      echo x
    )
    k=${k%x}
    [ -n "$k" ] && break
    os="${ROWS:-}x${COLS:-}"
    tui_size
    [ "$os" != "${ROWS}x${COLS}" ] && {
      K=resize
      return
    }
  done
  case "$k" in
  "$ESC[A" | "${ESC}OA" | k) K=up ;;
  "$ESC[B" | "${ESC}OB" | j) K=down ;;
  "$ESC[C" | "${ESC}OC") K=right ;;
  "$ESC[D" | "${ESC}OD") K=left ;;
  "$ESC[3~") K=del ;;
  "
" | "
") K=enter ;;
  " ") K=space ;;
  "$ESC") K=esc ;;
  q | Q) K=q ;;
  *) K=$k ;;
  esac
}

# frame <title> <body> <hint>   — draws the logo (if it fits) and a centred box around body
frame() {
  title=$1
  body=$2
  hint=$3
  tui_size
  W=$((COLS - 8))
  [ $W -gt 78 ] && W=78
  [ $W -lt 30 ] && W=30
  n=$(printf '%s\n' "$body" | wc -l)
  H=$((n + 4))
  big=0
  [ $ROWS -ge $((H + 22)) ] && [ $COLS -ge 40 ] && big=1
  if [ $big = 1 ]; then HEAD=19; else HEAD=2; fi
  top=$(((ROWS - HEAD - H) / 2 + 1))
  [ $top -lt 1 ] && top=1
  left=$(((COLS - W - 2) / 2 + 1))
  geo="$ROWS:$COLS:$top:$H:$big:$title"
  OUT=""
  [ "$geo" != "$LASTGEO" ] && OUT="$ESC[2J"
  LASTGEO=$geo
  # header
  if [ $big = 1 ]; then
    lc=$(((COLS - LOGO_W) / 2 + 1))
    r=$top
    while IFS= read -r l; do
      at $r $lc
      OUT="$OUT$l"
      r=$((r + 1))
    done <<EOF
$(logo_colored)
EOF
    t="${WHT}K U S A N A G I${R0}  ${RED}草薙${R0}  ${DIM}$KVERSION${R0}"
    at $((top + 17)) $(((COLS - 25 - ${#KVERSION}) / 2 + 1))
    OUT="$OUT$t"
  else
    t="${STEEL}⚔${R0}  ${WHT}K U S A N A G I${R0}  ${RED}草薙${R0}  ${DIM}$KVERSION${R0}"
    at $top $(((COLS - 28 - ${#KVERSION}) / 2 + 1))
    OUT="$OUT$t"
  fi
  by=$((top + HEAD))
  # box
  tl=$(vlen "$title")
  at $by $left
  OUT="$OUT$LINE╭─ $R0$BO$title$R0 $LINE$(rep ─ $((W - tl - 3)))╮$R0"
  blank="$LINE│$R0$(rep ' ' $W)$LINE│$R0"
  i=1
  while [ $i -le $((H - 2)) ]; do
    at $((by + i)) $left
    OUT="$OUT$blank"
    i=$((i + 1))
  done
  hl=$(vlen "$hint")
  at $((by + H - 1)) $left
  OUT="$OUT$LINE╰$(rep ─ $((W - hl - 3))) $R0$DIM$hint$R0 $LINE─╯$R0"
  r=$((by + 2))
  while IFS= read -r l; do
    at $r $((left + 3))
    OUT="$OUT$l"
    r=$((r + 1))
  done <<EOF
$body
EOF
  BOX_TOP=$by
  BOX_LEFT=$left
  printf '%s' "$OUT"
}

# menu <single|multi> <title> <intro> <items> <hint> [checked]
#   items: lines "key<TAB>label<TAB>note";  checked: string of 0/1 per item (multi)
#   -> SEL (key, or space-separated keys for multi); returns 1 on q/esc
menu() {
  mmode=$1
  mtitle=$2
  mintro=$3
  mitems=$4
  mhint=$5
  CHK=${6:-}
  cnt=$(printf '%s\n' "$mitems" | wc -l)
  cur=1
  while :; do
    tui_size
    w=$((COLS - 8))
    [ $w -gt 78 ] && w=78
    printf '%s\n' "$mitems" >"$MENU_TMP"
    list=$(awk -F'\t' -v cur=$cur -v chk="$CHK" -v multi=$([ "$mmode" = multi ] && echo 1 || echo 0) -v w=$((w - 6)) \
      -v RED="$RED" -v R0="$R0" -v DIM="$DIM" -v WHT="$WHT" -v GRN="$GRN" -v TXT="$TXT" '
            NR == FNR { if (length($2) > lw) lw = length($2); next }
            { i = FNR; mark = (i == cur) ? RED "❯ " R0 : "  "
              pad = sprintf("%" (lw - length($2) + 1) "s", "")
              box = ""; if (multi) box = (substr(chk, i, 1) == "1") ? GRN "● " R0 : DIM "○ " R0
              lab = (i == cur) ? WHT $2 R0 pad : TXT $2 R0 pad
              room = w - lw - (multi ? 2 : 0) - 4; note = $3
              if (length(note) > room) note = (room > 1) ? substr(note, 1, room - 1) "…" : ""
              printf "%s%s%s %s%s%s\n", mark, box, lab, DIM, note, R0 }' "$MENU_TMP" "$MENU_TMP")
    if [ -n "$mintro" ]; then frame "$mtitle" "$mintro

$list" "$mhint"; else frame "$mtitle" "$list" "$mhint"; fi
    readkey
    case "$K" in
    up) cur=$((cur > 1 ? cur - 1 : cnt)) ;;
    down | tab) cur=$((cur < cnt ? cur + 1 : 1)) ;;
    space) if [ "$mmode" = multi ]; then
      mc=$(printf '%s' "$CHK" | cut -c$cur)
      [ "$mc" = 1 ] && mc=0 || mc=1
      CHK=$(printf '%s' "$CHK" | awk -v i=$cur -v c=$mc '{ print substr($0, 1, i - 1) c substr($0, i + 1) }')
    fi ;;
    enter)
      if [ "$mmode" = multi ]; then
        SEL=$(printf '%s\n' "$mitems" | awk -F'\t' -v chk="$CHK" 'substr(chk, NR, 1) == "1" { printf "%s ", $1 }')
        SEL=${SEL% }
      else SEL=$(printf '%s\n' "$mitems" | sed -n "${cur}p" | cut -f1); fi
      MENU_CUR=$cur
      return 0
      ;;
    q | esc) return 1 ;;
    [1-9]) [ "$K" -le "$cnt" ] && cur=$K ;;
    esac
  done
}

# input <title> <body> <label> -> INPUT (empty = cancelled)
input() {
  frame "$1" "$2

  $3 ${LINE}▏${R0}" "⏎ confirm · empty ⏎ cancels"
  lb=$(vlen "  $3 ▏")
  r=$((BOX_TOP + 2 + $(printf '%s\n' "$2" | wc -l) + 1))
  printf '%s' "$ESC[$r;$((BOX_LEFT + 3 + lb))H$ESC[?25h"
  stty icanon echo </dev/tty 2>/dev/null
  IFS= read -r INPUT <"$KEYS" || INPUT=""
  stty -icanon -echo min 0 time 3 </dev/tty 2>/dev/null
  printf '%s' "$ESC[?25l"
  LASTGEO=""
}

notice() { # notice <title> <body> — any key
  while :; do
    frame "$1" "$2" "press any key"
    readkey
    [ "$K" != resize ] && return
  done
}

quit_tui() {
  tui_off
  printf '%s\n' "Kusanagi installer: nothing changed."
  exit 0
}

# ---------------------------------------------------------------- screens
screen_welcome() {
  body="${TXT}A desktop shell for Wayland: bar, launcher, control panel,
notifications, wallpapers, lock screen — one program.${R0}

  ${DIM}distro${R0}      ${WHT}${PRETTY_NAME:-$ID}${R0} ${DIM}($FAM)${R0}
  ${DIM}init${R0}        ${WHT}$INIT${R0}
  ${DIM}privileges${R0}  ${WHT}${SU:-none needed}${R0}"
  [ $DRY = 1 ] && body="$body

  ${YEL}dry run — nothing will be changed${R0}"
  while :; do
    frame "Welcome" "$body" "⏎ begin · q quit"
    readkey
    case "$K" in enter | space) return ;; q | esc) quit_tui ;; esac
  done
}

screen_compositors() {
  items=""
  chk=""
  for c in $ALL_COMPS; do
    if have "$(comp_bin $c)"; then
      st="installed"
      on=1
    else
      st="will be installed"
      on=0
    fi
    case $c in
    mango) d="dwl-style tags, light" ;; hyprland) d="animations, eye candy" ;; niri) d="scrollable columns" ;;
    sway) d="i3-style tiling, rock solid" ;; labwc) d="Openbox-style floating windows" ;;
    kde) d="KWin from KDE Plasma (Kusanagi beside or instead of its panel)" ;; dwl) d="suckless tags — keys need a rebuild" ;;
    esac
    items="$items$c	$(comp_name $c)	$st · $d
"
    chk="$chk$on"
  done
  items=${items%
}
  case "$chk" in *1*) ;; *) chk=0010000 ;; esac # nothing installed: niri
  [ -n "$PICK" ] && chk=$(for c in $ALL_COMPS; do case " $PICK " in *" $c "*) printf 1 ;; *) printf 0 ;; esac done)
  while :; do
    menu multi "Compositors" "${TXT}Which compositors should Kusanagi run on?${R0}
${DIM}Missing ones get installed for you.${R0}" "$items" "space pick · ⏎ next · q quit" "$chk" || quit_tui
    chk=$CHK
    [ -n "$SEL" ] && {
      PICK=$SEL
      return
    }
    notice "Compositors" "${YEL}Pick at least one.${R0}"
  done
}

# defaults where they're free (keep) / everywhere (win)
init_binds() { # init_binds <c> <keep|win|fresh>
  for id in $(action_ids); do
    d=$(action_field "$id" 3)
    if [ "$2" = keep ] && clash "$1" "$d" >/dev/null; then setb "$1" "$id" ""; else setb "$1" "$id" "$d"; fi
  done
}

screen_binder() { # screen_binder <c>
  c=$1
  cur=1
  msg=""
  ids=$(binder_ids)
  cnt=$(printf '%s\n' "$ids" | wc -l)
  while :; do
    rows=""
    i=1
    for id in $ids; do
      b=$(getb "$c" "$id")
      lab=$(action_field "$id" 2)
      if [ -z "$b" ]; then
        st="${DIM}not bound${R0}"
      elif cl=$(clash "$c" "$b"); then
        st="${RED}✗ yours: ${cl%%  (*}${R0}"
      else st="${GRN}✓ free${R0}"; fi
      if [ $i = $cur ]; then
        mk="${RED}❯${R0}"
        lw="$WHT"
      else
        mk=" "
        lw="$TXT"
      fi
      k=$(pretty "$b")
      [ -z "$b" ] && k=""
      rows="$rows$(printf '%s %s%-19s%s %s%-20s%s %s' "$mk" "$lw" "$lab" "$R0" "$GOLD" "$k" "$R0" "$st")
"
      i=$((i + 1))
    done
    if [ $cur = $((cnt + 1)) ]; then rows="$rows
${RED}❯${R0} ${WHT}[ Save these keys ]${R0}"; else rows="$rows
  ${TXT}[ Save these keys ]${R0}"; fi
    frame "$(comp_name $c) keys" "${TXT}Your binds stay. Give Kusanagi the keys you want — clashes are blocked.${R0}
${DIM}Volume, mic, brightness and media keys go to Kusanagi where they're free.${R0}
${msg:- }

$rows" "⏎ change · x unbind · r default · s save"
    msg=""
    readkey
    case "$K" in
    up) cur=$((cur > 1 ? cur - 1 : cnt + 1)) ;;
    down | tab) cur=$((cur <= cnt ? cur + 1 : 1)) ;;
    s | S) return 0 ;;
    q | esc) return 0 ;;
    x | X | del) [ $cur -le $cnt ] && setb "$c" "$(printf '%s\n' "$ids" | sed -n "${cur}p")" "" ;;
    r | R) if [ $cur -le $cnt ]; then
      id=$(printf '%s\n' "$ids" | sed -n "${cur}p")
      d=$(action_field "$id" 3)
      if cl=$(clash "$c" "$d"); then msg="${YEL}$(pretty "$d") is yours: ${cl%%  (*}${R0}"; else setb "$c" "$id" "$d"; fi
    fi ;;
    enter)
      [ $cur = $((cnt + 1)) ] && return 0
      id=$(printf '%s\n' "$ids" | sed -n "${cur}p")
      lab=$(action_field "$id" 2)
      input "$(comp_name $c) keys" "${TXT}New key for ${WHT}$lab${R0}
${DIM}e.g.  super+shift+k   ·   alt+space   ·   super+f1   ·   print${R0}" "${GOLD}key:${R0}"
      [ -z "$INPUT" ] && continue
      nb=$(parse_combo "$INPUT")
      if [ -z "$nb" ]; then
        msg="${YEL}Didn't get \"$INPUT\" — mods + one key, like super+shift+k${R0}"
        continue
      fi
      if cl=$(clash "$c" "$nb"); then
        msg="${YEL}$(pretty "$nb") is already yours: ${cl%%  (*}${R0}"
        continue
      fi
      dup=""
      for o in $ids; do
        [ "$o" = "$id" ] && continue
        ob=$(getb "$c" "$o")
        [ -n "$ob" ] && [ "$(canon "$ob")" = "$(canon "$nb")" ] && dup=$(action_field "$o" 2)
      done
      if [ -n "$dup" ]; then
        msg="${YEL}$(pretty "$nb") is already Kusanagi's $dup${R0}"
        continue
      fi
      setb "$c" "$id" "$nb"
      ;;
    esac
  done
}

screen_keys() { # per compositor: keep mine / Kusanagi wins / no keys
  for c in $PICK; do
    f=$(conf_main "$c")
    if wired_comp "$c"; then
      eval "MODE_$c=wired"
      continue
    fi
    if [ ! -f "$f" ] && [ -z "$(seed_conf "$c")" ] && [ "$c" != labwc ]; then
      eval "MODE_$c=fresh"
      init_binds "$c" fresh
      continue
    fi
    SCAN=$(scan_binds "$c")
    eval "SCAN_$c=\$SCAN"
    n=$(printf '%s' "$SCAN" | grep -c .)
    if [ "$n" = 0 ]; then
      eval "MODE_$c=fresh"
      init_binds "$c" fresh
      continue
    fi
    clashes=0
    for id in $(action_ids); do clash "$c" "$(action_field "$id" 3)" >/dev/null && clashes=$((clashes + 1)); done
    if [ $TUI = 0 ]; then
      eval "MODE_$c=keep"
      init_binds "$c" keep
      continue
    fi
    items="keep	Keep my keybinds	pick Kusanagi's keys around them
win	Use Kusanagi's keys	they win where they clash ($clashes of yours)
none	No keybinds	just start Kusanagi with $(comp_name $c)"
    [ -f "$f" ] || f=$(seed_conf "$c")
    [ -f "$f" ] || f="$(comp_name $c)'s defaults"
    menu single "$(comp_name $c)" "${TXT}You already have a $(comp_name $c) config with ${WHT}$n${TXT} keybinds.
Kusanagi's default keys clash with ${WHT}$clashes${TXT} of them.${R0}
${DIM}$(short "$f")${R0}" "$items" "↑↓ move · ⏎ choose · q quit" || quit_tui
    eval "MODE_$c=$SEL"
    case "$SEL" in
    keep)
      init_binds "$c" keep
      screen_binder "$c"
      ;;
    win)
      init_binds "$c" win
      if [ $clashes -gt 0 ]; then
        lst=""
        for id in $(action_ids); do
          dk=$(action_field "$id" 3)
          cl=$(clash "$c" "$dk") && lst="$lst  ${GOLD}$(pretty "$dk")${R0}  ${DIM}was: ${cl%%  (*}${R0}
"
        done
        case "$c" in
        mango) note="Those lines get commented out (backup kept; --uninstall restores them)." ;;
        kde) note="KDE's own shortcuts on those keys get cleared (backup kept)." ;;
        labwc) note="Kusanagi's keybinds come last in rc.xml, so they take over." ;;
        *) note="Kusanagi's file is included last, so its keys take over." ;;
        esac
        notice "$(comp_name $c)" "${TXT}These keys switch to Kusanagi:${R0}

$lst
${DIM}$note${R0}"
      fi
      ;;
    none) ;;
    esac
  done
}

# ================================================================ running things
LOGF="$CACHE/install.log"
SPIN='⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏'
DONE_LIST=""
progress() { # progress <current label> <spinner> <log tail>
  body="$DONE_LIST"
  [ -n "$1" ] && body="$body${YEL}$2${R0} ${WHT}$1${R0}
"
  body="$body
${LINE}$(rep ─ $((W - 6)))${R0}
${DIM}$3${R0}"
  frame "Installing" "$body" "$([ $DRY = 1 ] && echo 'dry run' || echo 'log: ~/.cache/kusanagi/install.log')"
}
# task <label> <fg|bg> <shell command>
task() {
  label=$1
  how=$2
  cmd=$3
  mkdir -p "$CACHE"
  printf '\n### %s\n$ %s\n' "$label" "$cmd" >>"$LOGF"
  if [ $TUI = 0 ]; then
    printf '%s==>%s %s%s%s\n  %s$ %s%s\n' "$GRN" "$R0" "$BO" "$label" "$R0" "$DIM" "$cmd" "$R0"
    [ $DRY = 1 ] && return 0
    sh -c "$cmd"
    return $?
  fi
  if [ $DRY = 1 ]; then
    DONE_LIST="$DONE_LIST${GRN}✓${R0} ${TXT}$label${R0}  ${DIM}(dry run)${R0}
"
    progress "" "" "$(printf 'would run: %s\n' "$cmd" | awk -v w=$((W - 6)) '{ print (length($0) > w) ? substr($0, 1, w - 1) "…" : $0 }')"
    sleep 0.15
    return 0
  fi
  if [ "$how" = fg ]; then # needs the terminal (password prompt, AUR helper)
    printf '%s' "$ESC[2J$ESC[H$ESC[?25h"
    stty "$ST" </dev/tty 2>/dev/null
    printf '%s⚔  %s%s\n%s$ %s%s\n\n' "$RED" "$label" "$R0" "$DIM" "$cmd" "$R0"
    sh -c "$cmd" </dev/tty
    rc=$?
    stty -icanon -echo min 0 time 3 </dev/tty 2>/dev/null
    printf '%s' "$ESC[?25l"
    LASTGEO=""
  else
    tmp=$(mktemp)
    sh -c "$cmd" >"$tmp" 2>&1 </dev/null &
    pid=$!
    i=0
    while kill -0 $pid 2>/dev/null; do
      sp=$(printf '%s' "$SPIN" | cut -c$((i % 10 * 3 + 1))-$((i % 10 * 3 + 3)))
      tl=$(tail -c 3000 "$tmp" | tr '\r' '\n' | sed "s/$ESC\[[0-9;?]*[A-Za-z]//g" | grep -v '^[[:space:]]*$' | tail -n 5 | awk -v w=$((W - 6)) '{ print substr($0, 1, w) }')
      progress "$label" "$sp" "$tl"
      sleep 0.12
      i=$((i + 1))
    done
    wait $pid
    rc=$?
    cat "$tmp" >>"$LOGF"
    rm -f "$tmp"
  fi
  if [ $rc = 0 ]; then DONE_LIST="$DONE_LIST${GRN}✓${R0} ${TXT}$label${R0}
"; else DONE_LIST="$DONE_LIST${RED}✗ $label${R0} ${DIM}(exit $rc — see the log)${R0}
"; fi
  progress "" "" ""
  return $rc
}
say() { if [ $TUI = 0 ]; then printf '%s\n' "$*"; else DONE_LIST="$DONE_LIST$*
"; fi; }

authenticate() { # get privileges once, up front, so the TUI isn't broken by prompts
  [ -z "$SU" ] && return 0
  [ $DRY = 1 ] && return 0
  AUTH_FG=0
  if [ $TUI = 1 ]; then
    frame "Password" "${TXT}Installing packages and enabling services needs ${WHT}$SU${TXT}.${R0}

" "type your password"
    printf '%s' "$ESC[$((BOX_TOP + 4));$((BOX_LEFT + 3))H$ESC[?25h"
    stty "$ST" </dev/tty 2>/dev/null
  fi
  if [ "$SU" = sudo ]; then sudo -v; else doas true; fi || {
    [ $TUI = 1 ] && tui_off
    echo "couldn't get $SU — nothing changed"
    exit 1
  }
  if [ $TUI = 1 ]; then
    stty -icanon -echo min 0 time 3 </dev/tty 2>/dev/null
    printf '%s' "$ESC[?25l"
    LASTGEO=""
  fi
  if $SU -n true 2>/dev/null; then
    # keep sudo's ticket alive through long installs
    [ "$SU" = sudo ] && {
      while :; do
        sudo -n true 2>/dev/null
        sleep 50
      done &
      KEEPALIVE=$!
    }
  else AUTH_FG=1; fi # doas without persist: privileged steps run in the foreground
}
priv() { [ "${AUTH_FG:-0}" = 1 ] && echo fg || echo bg; }

backup() { [ $DRY = 1 ] || cp "$1" "$1.bak-kusanagi-$(date +%Y%m%d-%H%M%S)"; }

apply_compositor() { # writes kusanagi.<ext>, adds the include, validates (reverts if bad)
  c=$1
  eval "mode=\${MODE_$c:-fresh}"
  if [ "$mode" = wired ]; then
    say "${GRN}✓${R0} ${TXT}$(comp_name $c): already wired up — left alone${R0}"
    return 0
  fi
  case "$c" in labwc | kde | dwl)
    "apply_$c"
    return
    ;;
  esac
  d=$(conf_dir "$c")
  f=$(conf_main "$c")
  g="$d/$(gen_name "$c")"
  seeded=""
  if [ ! -f "$f" ] && [ -n "$(seed_conf "$c")" ]; then
    seeded=$(seed_conf "$c") # sway: your config starts as a copy of sway's default one
  elif [ ! -f "$f" ] && [ "$c" != mango ]; then
    say "${YEL}!${R0} ${TXT}$(comp_name $c): start it once to create $f, then re-run${R0}"
    return 0
  fi
  if [ $DRY = 1 ]; then
    task "$(comp_name $c): write $(basename "$g") + include${seeded:+ (config copied from $seeded)}" bg "true"
    return 0
  fi
  mkdir -p "$d"
  gen_conf "$c" >"$g"
  [ -n "$seeded" ] && cp "$seeded" "$f"
  [ -f "$f" ] || : >"$f"
  if ! grep -qF "$(include_line "$c")" "$f"; then
    case "$(basename "$f")" in *.kdl) cm="//" ;; *.lua) cm="--" ;; *) cm="#" ;; esac
    [ -z "$seeded" ] && backup "$f"
    printf '\n%s kusanagi include (added by install.sh)\n%s\n' "$cm" "$(include_line "$c")" >>"$f"
  fi
  # mango + "Kusanagi wins": comment out the user's clashing lines
  if [ "$mode" = win ] && [ "$c" = mango ]; then
    eval "list=\${SCAN_$c:-}"
    for id in $(action_ids); do
      b=$(getb "$c" "$id")
      [ -z "$b" ] && continue
      k=$(canon "$b")
      printf '%s\n' "$list" | awk -F'\t' -v k="$k" '$1 == k { print $2 }' | while IFS= read -r loc; do
        file=${loc%:*}
        ln=${loc##*:}
        [ -f "$file" ] && {
          backup "$file"
          sed -i "${ln}s/^/# [kusanagi] /" "$file"
        }
      done
    done
  fi
  ok=1
  case "$c" in
  mango) have mango && ! mango -c "$f" -p >/dev/null 2>&1 && ok=0 ;;
  niri) have niri && ! niri validate -c "$f" >/dev/null 2>&1 && ok=0 ;;
  sway) have sway && ! sway -C -c "$f" >/dev/null 2>&1 && ok=0 ;;
  esac
  if [ $ok = 0 ]; then
    if [ -n "$seeded" ]; then
      rm -f "$f"
    else
      b=$(ls -t "$f.bak-kusanagi-"* 2>/dev/null | head -1)
      [ -n "$b" ] && cp "$b" "$f"
    fi
    say "${RED}✗${R0} ${TXT}$(comp_name $c) rejected the config — reverted; check $g${R0}"
    return 1
  fi
  say "${GRN}✓${R0} ${TXT}$(comp_name $c): $(basename "$g") + include in $(basename "$f")${seeded:+ (copied from $seeded)}${R0}"
  [ "$c" = sway ] && [ -n "${SWAYSOCK:-}" ] && have swaymsg && swaymsg reload >/dev/null 2>&1
  return 0
}

# labwc: rc.xml has no include, so Kusanagi's keybinds live in a marked block at the end of <keyboard>
# (lib/labwc-keys.py: backup first, the result must parse as XML or nothing is written), and the start in autostart
apply_labwc() {
  d="$CFG/labwc"
  f="$d/rc.xml"
  a="$d/autostart"
  g="$d/kusanagi.xml"
  if [ $DRY = 1 ]; then
    task "labwc: keybinds into rc.xml (marked block) + a line in autostart" bg "true"
    return 0
  fi
  mkdir -p "$d"
  if ! grep -qsF "$LABWC_AUTOSTART_TAG" "$a"; then
    [ -f "$a" ] && backup "$a"
    printf '\n%s\n' "$(gen_extra labwc autostart)" >>"$a"
  fi
  gen_conf labwc >"$g"
  [ -f "$f" ] && backup "$f"
  if ! err=$(python3 "$ROOT/lib/labwc-keys.py" merge "$f" "$g" 2>&1); then
    say "${RED}✗${R0} ${TXT}labwc: rc.xml left alone — $err. Kusanagi's keybinds are in $g (paste them into <keyboard>)${R0}"
    return 1
  fi
  say "${GRN}✓${R0} ${TXT}labwc: keybinds in rc.xml + kusanagi in autostart${R0}"
  [ -n "${LABWC_PID:-}" ] && have labwc && labwc --reconfigure >/dev/null 2>&1
  return 0
}

# KDE Plasma: an autostart entry, and each key as a command shortcut — a hidden kusanagi-key-<id>.desktop
# plus its [services] group in kglobalshortcutsrc (what System Settings → Shortcuts → Add Command writes)
apply_kde() {
  f=$(conf_main kde)
  app=$(kde_app_dir)
  if [ $DRY = 1 ]; then
    task "KDE Plasma: autostart entry + command shortcuts (kglobalshortcutsrc)" bg "true"
    return 0
  fi
  mkdir -p "$CFG/autostart"
  gen_extra kde autostart >"$CFG/autostart/kusanagi.desktop"
  if [ ! -f "$f" ]; then
    say "${YEL}!${R0} ${TXT}KDE Plasma: autostart added; log into Plasma once (it creates $f), then re-run for the keys${R0}"
    return 0
  fi
  for id in $(action_ids); do rm -f "$app/kusanagi-key-$id.desktop"; done
  mkdir -p "$app"
  bound kde | while IFS='	' read -r id b cmd label over; do gen_extra kde app "$id" >"$app/kusanagi-key-$id.desktop"; done
  # "Kusanagi wins": KDE's own shortcuts on Kusanagi's keys are cleared (just that key; "none" when it was the only one)
  clr=""
  if [ "$mode" = win ]; then
    eval "list=\${SCAN_kde:-}"
    for id in $(action_ids); do
      b=$(getb kde "$id")
      [ -z "$b" ] && continue
      clr="$clr$(printf '%s\n' "$list" | awk -F'\t' -v k="$(canon "$b")" '$1 == k { n = split($2, p, ":"); print p[n] "\t" k }')
"
    done
  fi
  backup "$f"
  tmp=$(mktemp)
  awk -v clr="$clr" "$AWK_NORM$AWK_QT"'
        BEGIN { n = split(clr, L, "\n"); for (i = 1; i <= n; i++) if (L[i] != "") { split(L[i], q, "\t"); drop[q[1] SUBSEP q[2]] = 1; has[q[1]] = 1 } }
        /^\[/ { ours = ($0 ~ /^\[services\]\[kusanagi-key-/) }
        ours || /^# Kusanagi for KDE Plasma/ { next }
        !(NR in has) || !/=/ { print; next }
        { name = substr($0, 1, index($0, "=") - 1); v = substr($0, index($0, "=") + 1)
          rest = ""; cur = v; if (index(v, ",")) { cur = substr(v, 1, index(v, ",") - 1); rest = substr(v, index(v, ",")) }
          n = split(cur, keys, /\\t/); out = ""
          for (i = 1; i <= n; i++) if (!((NR SUBSEP norm(fromqt(keys[i]))) in drop)) out = out (out ? "\\t" : "") keys[i]
          print name "=" (out == "" ? "none" : out) rest }' "$f" >"$tmp"
  # drop trailing blank lines, then Kusanagi's groups at the end
  awk '{ l[NR] = $0 } END { n = NR; while (n > 0 && l[n] == "") n--; for (i = 1; i <= n; i++) print l[i] }' "$tmp" >"$tmp.2"
  { cat "$tmp.2"; echo ""; gen_conf kde; } >"$f"
  rm -f "$tmp" "$tmp.2"
  say "${GRN}✓${R0} ${TXT}KDE Plasma: autostart entry + $(bound kde | grep -c .) shortcuts — they work after your next Plasma login${R0}"
  pgrep -x plasmashell >/dev/null 2>&1 &&
    say "${DIM}  plasmashell draws its own panel next to Kusanagi's bar: to run Kusanagi alone, take plasmashell out (systemctl --user mask plasma-plasmashell on systemd)${R0}"
  return 0
}

# dwl is configured in config.h and compiled: Kusanagi writes the keys for it to #include, and a start script
apply_dwl() {
  d="$CFG/dwl"
  if [ $DRY = 1 ]; then
    task "dwl: write kusanagi.h (keys for config.h) + kusanagi.sh (starts dwl with Kusanagi)" bg "true"
    return 0
  fi
  mkdir -p "$d"
  gen_conf dwl >"$d/kusanagi.h"
  gen_extra dwl session >"$d/kusanagi.sh"
  chmod +x "$d/kusanagi.sh"
  say "${GRN}✓${R0} ${TXT}dwl: kusanagi.h + kusanagi.sh in $(short "$d")${R0}"
  say "${YEL}!${R0} ${TXT}dwl, by hand: add  #include \"$d/kusanagi.h\"  inside keys[] in your config.h and rebuild dwl; start your session with $(short "$d")/kusanagi.sh${R0}"
  return 0
}

# ================================================================ --print
if [ -n "$PRINT" ]; then
  case " $ALL_COMPS " in *" $PRINT "*) ;; *)
    echo "--print=$(echo $ALL_COMPS | tr ' ' '|')"
    exit 1
    ;;
  esac
  eval "MODE_$PRINT=fresh"
  init_binds "$PRINT" fresh
  case "$PRINT" in
  labwc)
    echo "<!-- rc.xml, inside <keyboard> (after <default /> if you use labwc's default keys): -->"
    gen_conf labwc
    echo "<!-- ~/.config/labwc/autostart: -->"
    gen_extra labwc autostart
    ;;
  kde)
    echo "# ~/.config/autostart/kusanagi.desktop:"
    gen_extra kde autostart
    echo ""
    echo "# ~/.config/kglobalshortcutsrc, each with $(kde_app_dir)/kusanagi-key-<id>.desktop, e.g. kusanagi-key-launcher.desktop:"
    gen_extra kde app launcher | sed 's/^/#   /'
    gen_conf kde
    ;;
  dwl)
    gen_conf dwl
    echo ""
    echo "/* ~/.config/dwl/kusanagi.sh: */"
    gen_extra dwl session
    ;;
  *) gen_conf "$PRINT" ;;
  esac
  exit 0
fi

# ================================================================ uninstall
if [ $UNINSTALL = 1 ]; then
  have kusanagi && kusanagi stop >/dev/null 2>&1
  rm -f "$BIN/kusanagi" "$BIN/kusanagi-shell"
  rm -rf "${XDG_DATA_HOME:-$HOME/.local/share}/kusanagi/assets"
  [ -L "$CFG/quickshell/kusanagi" ] && rm -f "$CFG/quickshell/kusanagi"
  for f in "$CFG/mango/config.conf" "$CFG/hypr/hyprland.lua" "$CFG/hypr/hyprland.conf" "$CFG/niri/config.kdl" "$CFG/sway/config"; do
    [ -f "$f" ] && grep -q "kusanagi" "$f" && sed -i '/kusanagi include (added by install.sh)/d;/kusanagi\.conf/d;/kusanagi\.lua/d;/kusanagi\.kdl/d' "$f" && echo "removed the include from $f"
  done
  grep -rlF '# [kusanagi] ' "$CFG/mango" 2>/dev/null | while IFS= read -r f; do
    sed -i 's/^# \[kusanagi\] //' "$f"
    echo "restored your binds in $f"
  done
  rm -f "$CFG/mango/kusanagi.conf" "$CFG/hypr/kusanagi.lua" "$CFG/hypr/kusanagi.conf" "$CFG/niri/kusanagi.kdl" "$CFG/sway/kusanagi.conf"
  # labwc: the marked block in rc.xml + the autostart line
  if [ -f "$CFG/labwc/rc.xml" ] && grep -q 'kusanagi:begin' "$CFG/labwc/rc.xml"; then
    cp "$CFG/labwc/rc.xml" "$CFG/labwc/rc.xml.bak-kusanagi-$(date +%Y%m%d-%H%M%S)"
    sed -i '/kusanagi:begin/,/kusanagi:end/d' "$CFG/labwc/rc.xml" && echo "removed Kusanagi's keybinds from $CFG/labwc/rc.xml"
  fi
  [ -f "$CFG/labwc/autostart" ] && grep -qF "$LABWC_AUTOSTART_TAG" "$CFG/labwc/autostart" &&
    sed -i "\\|$LABWC_AUTOSTART_TAG|d" "$CFG/labwc/autostart" && echo "removed kusanagi from $CFG/labwc/autostart"
  rm -f "$CFG/labwc/kusanagi.xml"
  # KDE: autostart entry, the shortcut groups and their .desktop files
  rm -f "$CFG/autostart/kusanagi.desktop" "$(kde_app_dir)"/kusanagi-key-*.desktop
  if [ -f "$CFG/kglobalshortcutsrc" ] && grep -q '^\[services\]\[kusanagi-key-' "$CFG/kglobalshortcutsrc"; then
    t=$(mktemp)
    awk '/^\[/ { ours = ($0 ~ /^\[services\]\[kusanagi-key-/) } ours || /^# Kusanagi for KDE Plasma/ { next } { print }' "$CFG/kglobalshortcutsrc" >"$t" &&
      cat "$t" >"$CFG/kglobalshortcutsrc" && echo "removed Kusanagi's shortcuts from $CFG/kglobalshortcutsrc"
    rm -f "$t"
  fi
  # dwl: Kusanagi's files (take the #include out of your config.h yourself)
  [ -f "$CFG/dwl/kusanagi.h" ] && echo "dwl: remove the #include of $CFG/dwl/kusanagi.h from your config.h before rebuilding"
  rm -f "$CFG/dwl/kusanagi.h" "$CFG/dwl/kusanagi.sh"
  echo "Kusanagi uninstalled — your settings are still in $CFG/kusanagi"
  exit 0
fi

# ================================================================ the flow
[ $TUI = 1 ] && tui_on

if [ $TUI = 1 ]; then
  screen_welcome
else
  printf '%s⚔  Kusanagi %s installer%s\n  distro: %s (%s)   init: %s   privileges: %s\n' "$BO" "$KVERSION" "$R0" "${PRETTY_NAME:-$ID}" "$FAM" "$INIT" "${SU:-none}"
  [ $DRY = 1 ] && echo "  ${YEL}dry run — nothing will be changed${R0}"
fi

if [ -z "$PICK" ] && [ $TUI = 1 ]; then
  screen_compositors
elif [ -z "$PICK" ]; then
  for c in $ALL_COMPS; do have "$(comp_bin $c)" && PICK="$PICK $c"; done
  [ -z "$PICK" ] && PICK=niri
elif [ $TUI = 1 ]; then
  screen_compositors
fi
PICK=$(echo $PICK)
for c in $PICK; do case " $ALL_COMPS " in *" $c "*) ;; *)
  [ $TUI = 1 ] && tui_off
  echo "unknown compositor '$c' ($(echo $ALL_COMPS | sed 's/ /, /g'))"
  exit 1
  ;;
esac done

[ $DO_CFG = 1 ] && screen_keys

# ---- the plan
[ $TUI = 1 ] && frame "Checking" "${TXT}Looking at what's installed…${R0}" ""
compute_packages
compute_services
if [ $TUI = 1 ]; then
  np=$(echo $MISSING | wc -w)
  pk="all installed"
  [ $np -gt 0 ] && pk="install $np: $MISSING"
  sv="all set"
  [ -n "$SVC_TODO" ] && sv="enable $SVC_TODO ($INIT)"
  [ "$INIT" = unknown ] && sv="unknown init — do it yourself"
  cf=""
  for c in $PICK; do
    eval "m=\${MODE_$c:-fresh}"
    case "$m" in wired) m="already wired" ;; keep) m="your keys kept" ;; win) m="Kusanagi's keys" ;; none) m="no keys" ;; fresh) m="default keys" ;; esac
    cf="$cf$(comp_name $c): $m · "
  done
  items="pkg	Packages	$pk
svc	Services	$sv
cfg	Compositors	${cf% · }"
  chk="$DO_PKG$DO_SVC$DO_CFG"
  extra=""
  [ -n "$UNPACKAGED" ] && extra="
${YEL}!${R0} ${DIM}not packaged on $FAM, install by hand: $UNPACKAGED${R0}"
  case "$FAM" in
  fedora) extra="$extra
${DIM}hyprland comes from COPR: solopasha/hyprland${R0}" ;;
  gentoo) extra="$extra
${DIM}niri needs the GURU overlay (eselect repository enable guru)${R0}" ;;
  esac
  case " $SVC_TODO " in *" NetworkManager "*) extra="$extra
${DIM}using dhcpcd/iwd/connman for networking? untick Services — they conflict${R0}" ;; esac
  menu multi "The plan" "${TXT}Here's what happens. Untick anything you'd rather do yourself.${R0}$extra" "$items" \
    "space toggle · ⏎ $([ $DRY = 1 ] && echo 'dry run' || echo install) · q quit" "$chk" || quit_tui
  DO_PKG=0
  DO_SVC=0
  DO_CFG=0
  for s in $SEL; do case $s in pkg) DO_PKG=1 ;; svc) DO_SVC=1 ;; cfg) DO_CFG=1 ;; esac done
fi

# ---- do it
need_su=0
{ [ $DO_PKG = 1 ] && [ -n "$MISSING" ]; } && need_su=1
{ [ $DO_SVC = 1 ] && [ -n "$SVC_TODO" ]; } && need_su=1
[ $need_su = 1 ] && authenticate
[ $TUI = 1 ] && progress "" "" ""

if [ $DO_PKG = 1 ] && [ -n "$MISSING" ]; then
  case "$FAM" in
  arch)
    REPO=""
    AUR=""
    for p in $MISSING; do pacman -Si "$p" >/dev/null 2>&1 && REPO="$REPO $p" || AUR="$AUR $p"; done
    [ -n "$REPO" ] && task "Installing packages" "$(priv)" "$SU pacman -S --needed --noconfirm$REPO"
    if [ -n "$AUR" ]; then
      if have paru; then
        task "AUR:$AUR" fg "paru -S --needed$AUR"
      elif have yay; then
        task "AUR:$AUR" fg "yay -S --needed$AUR"
      else say "${YEL}!${R0} ${TXT}from the AUR (no paru/yay found):$AUR${R0}"; fi
    fi
    ;;
  void) task "Installing packages" "$(priv)" "$SU xbps-install -Sy $MISSING" ;;
  fedora) task "Installing packages" "$(priv)" "$SU dnf install -y $MISSING" ;;
  gentoo) task "Installing packages" fg "$SU emerge --noreplace $MISSING" ;;
  debian) task "Installing packages" "$(priv)" "$SU apt-get install -y $MISSING" ;;
  suse) task "Installing packages" "$(priv)" "$SU zypper install -y $MISSING" ;;
  *) say "${YEL}!${R0} ${TXT}install these yourself: $MISSING${R0}" ;;
  esac
fi

if [ $DO_SVC = 1 ] && [ -n "$SVC_TODO" ]; then
  for s in $SVC_TODO; do task "Enabling $s ($INIT)" "$(priv)" "$(svc_cmd "$s" | paste -sd';' -)"; done
fi

if [ $DO_CFG = 1 ]; then
  for c in $PICK; do apply_compositor "$c"; done
fi

if [ $DRY = 0 ]; then
  mkdir -p "$BIN" "$CFG/kusanagi"
  ln -sfn "$ROOT/bin/kusanagi" "$BIN/kusanagi"
  say "${GRN}✓${R0} ${TXT}linked the kusanagi command into ~/.local/bin${R0}"
else task "Link the kusanagi command" bg "ln -sfn $ROOT/bin/kusanagi $BIN/kusanagi"; fi
# the shell itself: one native binary (native/), built here once — a few minutes
[ $DO_BUILD = 1 ] && { task "Building the shell (kusanagi-shell)" bg "sh '$ROOT/lib/build-native.sh'" \
  || say "${YEL}!${R0} ${TXT}the shell didn't build — see $LOGF, then: sh lib/build-native.sh${R0}"; }
case ":$PATH:" in *":$BIN:"*) ;; *) say "${YEL}!${R0} ${TXT}~/.local/bin isn't on your PATH — add it (e.g. in ~/.profile)${R0}" ;; esac

# ---- done
keys=""
c1=$(echo $PICK | cut -d' ' -f1)
eval "m1=\${MODE_$c1:-fresh}"
for id in launcher wallpaper clipboard settings power; do
  b=$(getb "$c1" "$id")
  [ -z "$b" ] && [ "$m1" != keep ] && b=$(action_field "$id" 3)
  [ -z "$b" ] && continue
  keys="$keys  ${GOLD}$(printf '%-18s' "$(pretty "$b")")${R0} ${TXT}$(action_field "$id" 2)${R0}
"
done
[ "$m1" = none ] && keys="  ${TXT}no keybinds added — bind kusanagi msg … yourself${R0}
"
[ "$m1" = wired ] && keys="  ${TXT}your own keybinds (already set up)${R0}
"
if [ $TUI = 1 ]; then
  body="$DONE_LIST
${WHT}Done.${R0} ${TXT}Log into $(for c in $PICK; do comp_name $c; done | paste -sd/ - | sed 's|/| / |g') — Kusanagi starts with it.${R0}

$keys"
  if [ $DRY = 0 ] && [ -n "${WAYLAND_DISPLAY:-}" ] && ! "$BIN/kusanagi" status -q 2>/dev/null; then
    while :; do
      frame "Ready" "$body" "⏎ start Kusanagi now · q quit"
      readkey
      [ "$K" != resize ] && break
    done
    tui_off
    [ "$K" = enter ] && "$BIN/kusanagi" start
  else
    notice "Ready" "$body"
    tui_off
  fi
  [ $DRY = 0 ] && "$BIN/kusanagi" doctor 2>/dev/null | tail -n 4
else
  [ $DRY = 0 ] && [ -x "$BIN/kusanagi" ] && "$BIN/kusanagi" doctor || true
  printf '\n%sDone.%s Log into %s— Kusanagi starts with it. Or right now:  kusanagi\n' "$BO" "$R0" "$(for c in $PICK; do printf '%s ' "$c"; done)"
fi
