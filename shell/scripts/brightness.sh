#!/bin/sh
# brightness.sh — what Brightness.qml can dim, and the levels. Prints one fact per line:
#   bl|<device>|<current>|<max>          a laptop backlight (/sys/class/backlight)
#   mon|<i2c bus>|<connector>|<model>    a monitor that answers DDC/CI
#   val|<i2c bus>|<current>|<max>        its brightness (VCP 0x10)
#   ddc|ok|off|missing|noi2c|noaccess    how DDC went
# usage: brightness.sh detect <ddc 0|1>
#        brightness.sh read <bus|bl:device>…     (val| lines for buses, bl| lines for backlights)

backlight() {
    d=/sys/class/backlight/$1
    [ -r "$d/max_brightness" ] || return
    echo "bl|$1|$(cat "$d/actual_brightness" 2>/dev/null || cat "$d/brightness")|$(cat "$d/max_brightness")"
}
vcp() {
    # "VCP 10 C 50 100" → val|bus|50|100
    set -- "$1" $(ddcutil --bus "$1" --brief getvcp 10 2>/dev/null)
    [ "${2:-}" = VCP ] && echo "val|$1|$5|$6"
}

case "${1:-}" in
detect)
    for d in /sys/class/backlight/*; do [ -e "$d" ] && backlight "${d##*/}"; done
    [ "${2:-1}" = 1 ] || { echo "ddc|off"; exit 0; }
    command -v ddcutil >/dev/null || { echo "ddc|missing"; exit 0; }
    ls /dev/i2c-* >/dev/null 2>&1 || { echo "ddc|noi2c"; exit 0; }
    ok=0
    for b in /dev/i2c-*; do [ -r "$b" ] && [ -w "$b" ] && { ok=1; break; }; done
    [ $ok = 1 ] || { echo "ddc|noaccess"; exit 0; }
    # ddcutil 1.x says "DRM connector", 2.x "DRM_connector"; --brief has "Monitor: MFG:Model:Serial"
    mons=$(ddcutil detect --brief 2>/dev/null | awk '
        function out() { if (ok && bus != "") print "mon|" bus "|" con "|" mon; ok = 0 }
        /^Display [0-9]/ { out(); ok = 1; bus = ""; con = ""; mon = ""; next }
        /^[A-Za-z]/      { out(); next }                       # Invalid display, Phantom display…
        /I2C bus:/       { b = $0; sub(/.*i2c-/, "", b); bus = b }
        /DRM[_ ]connector:/ { c = $0; sub(/.*card[0-9]+-/, "", c); con = c }
        /Monitor:/       { m = $0; sub(/.*Monitor: */, "", m); split(m, f, ":"); mon = f[2] }
        /Model:/         { if (mon == "") { m = $0; sub(/.*Model: */, "", m); mon = m } }
        END              { out() }')
    [ -n "$mons" ] && echo "$mons"
    for b in $(echo "$mons" | cut -d'|' -f2); do vcp "$b"; done
    echo "ddc|ok" ;;
read)
    shift
    for t in "$@"; do
        case $t in bl:*) backlight "${t#bl:}" ;; *) vcp "$t" ;; esac
    done ;;
*)
    echo "usage: brightness.sh detect <0|1> | read <bus|bl:device>…" >&2; exit 2 ;;
esac
exit 0
