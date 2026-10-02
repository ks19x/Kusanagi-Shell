-- Kusanagi for Hyprland (Lua config, 0.55+) — autostart + keybinds.
-- install.sh adds `dofile(os.getenv("HOME") .. "/.config/hypr/kusanagi.lua")` to hyprland.lua.
hl.on("hyprland.start", function() hl.exec_cmd("kusanagi") end)
local k = function(keys, cmd) hl.bind(keys, hl.dsp.exec_cmd(cmd)) end
k("SUPER + SPACE", "kusanagi msg launcher toggle")
k("SUPER + A", "kusanagi msg wallpaper toggle")
k("SUPER + V", "kusanagi msg clipboard toggle")
k("SUPER + N", "kusanagi msg notifs toggle")
k("SUPER + I", "kusanagi msg settings toggle")
k("SUPER + L", "kusanagi msg lock lock")
k("SUPER + G", "kusanagi msg gamemode toggle")
k("SUPER + GRAVE", "kusanagi msg power toggle")
k("Print", "kusanagi screenshot full")
k("SUPER + SHIFT + S", "kusanagi screenshot region")
k("SUPER + SHIFT + C", "kusanagi colorpick")
