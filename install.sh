#!/bin/sh
# Install Kusanagi for this user (safe to re-run):
#   ~/.local/bin/kusanagi           → bin/kusanagi
#   ~/.config/quickshell/kusanagi   → shell/   (so Quickshell tooling finds it too)
#   ~/.config/kusanagi/             your settings (created on first start)
# ./install.sh --uninstall removes the links (your settings stay).
set -e
ROOT=$(cd "$(dirname "$0")" && pwd)
BIN="$HOME/.local/bin"; QS="${XDG_CONFIG_HOME:-$HOME/.config}/quickshell"

if [ "${1:-}" = --uninstall ]; then
    kusanagi stop 2>/dev/null || true
    rm -f "$BIN/kusanagi"; [ -L "$QS/kusanagi" ] && rm -f "$QS/kusanagi"
    echo "removed the links — settings are still in ~/.config/kusanagi"; exit 0
fi

mkdir -p "$BIN" "$QS" "${XDG_CONFIG_HOME:-$HOME/.config}/kusanagi"
ln -sfn "$ROOT/bin/kusanagi" "$BIN/kusanagi"
if [ -e "$QS/kusanagi" ] && [ ! -L "$QS/kusanagi" ]; then
    echo "~/.config/quickshell/kusanagi exists and isn't a link — leaving it alone"
else
    ln -sfn "$ROOT/shell" "$QS/kusanagi"
fi
case ":$PATH:" in *":$BIN:"*) ;; *) echo "note: add $BIN to your PATH" ;; esac
echo "installed kusanagi $(cat "$ROOT/VERSION")"; echo
"$BIN/kusanagi" doctor || echo "(install whatever is marked ✗)"
echo; echo "start it with:  kusanagi"
