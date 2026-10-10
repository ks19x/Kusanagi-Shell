#!/bin/bash
# Run under dbus-run-session as a regular user in a disposable test container.
set -euo pipefail
bundle=$(realpath "$1")
work=$(mktemp -d)
export XDG_RUNTIME_DIR="$work/runtime" XDG_CONFIG_HOME="$work/config" XDG_CACHE_HOME="$work/cache"
export XDG_STATE_HOME="$work/state" XDG_SESSION_TYPE=wayland
mkdir -p "$XDG_RUNTIME_DIR" "$XDG_CONFIG_HOME/kusanagi"
chmod 700 "$XDG_RUNTIME_DIR"
printf '{"polkit":{"enabled":false},"idle":{"enabled":false}}\n' > "$XDG_CONFIG_HOME/kusanagi/settings.json"
printf 'output * mode 1280x720\n' > "$work/sway.conf"
compositor= shell_pid=
cleanup() {
  [ -z "$shell_pid" ] || kill "$shell_pid" 2>/dev/null || true
  [ -z "$compositor" ] || kill "$compositor" 2>/dev/null || true
  cat "$work/sway.log" "$work/shell.log" 2>/dev/null || true
  find "$work/cache" -type f -name '*.log' -exec tail -80 {} \; 2>/dev/null || true
  rm -rf "$work"
}
trap cleanup EXIT
WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1 WLR_LIBINPUT_NO_DEVICES=1 \
  WLR_RENDERER=pixman sway -c "$work/sway.conf" > "$work/sway.log" 2>&1 &
compositor=$!
for attempt in {1..50}; do
  socket=$(find "$XDG_RUNTIME_DIR" -maxdepth 1 -type s -name 'wayland-*' -print -quit)
  [ -z "$socket" ] || break
  kill -0 "$compositor"
  sleep 0.2
done
test -n "$socket"
export WAYLAND_DISPLAY="${socket##*/}"
export LIBGL_ALWAYS_SOFTWARE=1
"$bundle/usr/bin/kusanagi-shell" > "$work/shell.log" 2>&1 &
shell_pid=$!
for attempt in {1..50}; do
  if "$bundle/usr/bin/kusanagi-shell" msg status; then
    sleep 2
    "$bundle/usr/bin/kusanagi-shell" msg status
    exit 0
  fi
  kill -0 "$shell_pid"
  sleep 0.2
done
exit 1
