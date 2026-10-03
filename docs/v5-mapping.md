# Kusanagi v5 — mapping the Quickshell shell (0.2.0) to a native C++23 shell

Source of truth: `shell/*.qml` (64 files), `shell/scripts/`, `shell/hyprquickpaper/`, `bin/kusanagi`,
`lib/palette.py`, `scripts/`, `compositors/`, `~/.config/kusanagi/{settings,colors,presets}.json` as of 2026-10-03.
Target: one Wayland client (`kusanagi-shell`), Meson, C++23, own widget tree + GLES 3 renderer, layer-shell,
multi-monitor, TOML config, Luau, Unix-socket IPC. No Qt/QML/GTK.

Legend — layer `B/T/O` = Background/Top/Overlay; anchors `TBLR`; kb focus `none/ondemand/exclusive`;
"single" = one surface on the compositor-chosen output (today that is *every* window except the wallpaper and lock).

---

## 1. Component inventory

### 1a. Windows / surfaces

| File | Kind | Shows / does | Surface(s) created | Per-screen? |
|---|---|---|---|---|
| `shell.qml` | root | Instantiates everything; `OnDemand` LazyLoader (preload ↔ unload 600 ms after close + `gc()` 300 ms later); 11 `IpcHandler`s; remembers settings page | — | — |
| `Bar.qml` | window | Workspaces island (+window title), clock island (calendar tooltip, click→panel, scroll action), right island: media (marquee), CPU, RAM, GPU, temp, volume, network, tray, power. Shared tooltip popup, media popover, tray-menu loader | PanelWindow, ns `quickshell-bar`, layer T, anchors T(or B)+L+R, height `bar.height`, exclusive zone `h` (Mango+top: `h-5`), kb none. PopupWindow tooltip (anchored to hovered module, edges Bottom/Top, gravity same) | **single** (no `screen:`) |
| `MediaPopup.qml` | popup | Cover 104², player identity, title, artist, seek bar (click), times, prev/play/next. Hover keeps it open | PopupWindow anchored to bar media module, 384×156, `PopupAdjustment.Slide` | follows bar |
| `TrayMenu.qml` | window | Kusanagi-drawn DBusMenu: separators, check/radio, icons, inline-expanding submenus, disabled state, mnemonic strip; click outside/Esc closes | PanelWindow ns `quickshell-traymenu`, O, TBLR, exclusion Ignore, kb ondemand, full-screen click-catcher | single |
| `ControlPanel.qml` | window | Morphs out of the clock island; header (clock, ~/.face, settings/lock/power buttons), segmented tabs Home/System/Inbox(n)/Quick; Tab cycles, Esc/click-outside closes; `runClosed(cmd)` runs after ms(300) | PanelWindow ns `quickshell-panel`, O, TBLR, Ignore, kb ondemand while showing else none, input mask = backdrop only while showing | single |
| `Launcher.qml` | window | Spotlight: fuzzy apps (usage-sorted), `=` calc (copies via wl-copy), `>` run (Shift+Enter in terminal), app actions (→ / Alt+Enter / button), list or grid, keyboard nav (↑↓ Tab Ctrl+J/K PgUp/PgDn ←→ in grid) | PanelWindow ns `quickshell-launcher`, O, TBLR, Ignore, kb **exclusive** while showing, mask backdrop | single |
| `Clipboard.qml` | window | cliphist history: list (text/colour swatch/URL icon/image thumb) + preview pane (wrapped text in Flickable, colour swatch, image); search, Enter copy, Del/middle-click remove, Clear all | PanelWindow ns `quickshell-clipboard`, O, TBLR, Ignore, kb exclusive, 820×500 card | single |
| `WallpaperPicker.qml` | window | Thumbnail grid of `wallpaper.folder` (thumbs via magick into `~/.cache/kusanagi/thumbs`), filter, arrows, Enter/click apply, Ctrl+R / button random, current marker | PanelWindow ns `quickshell-wallpaper`, O, TBLR, Ignore, kb exclusive | single |
| `PowerMenu.qml` | window | Lock / Log out / Suspend / Reboot / Shut down; risky ones need 2nd click within 3 s; ←→/Tab, Enter/Space, Esc | PanelWindow ns `quickshell-power`, O, TBLR, Ignore, kb exclusive | single |
| `Osd.qml` | window | Volume / mic / game-mode pill (pill or minimal; horizontal at top/bottom, vertical at right edge); ignores first 2 s of PipeWire churn; `preview(kind)` | PanelWindow ns `quickshell-osd`, O, anchors T+L+R (top) / B+L+R (bottom) / T+B+R (right), 110 px band, Ignore, kb none, **empty input region**, unmapped when idle | single |
| `NotificationPopups.qml` | window | Toast stack (max N, newest first), arrival springs, move transition, countdown (pauses on hover) | PanelWindow ns `notifications`, O, anchors T + R or L (top-center = T only), margins T 6/10 R/L 10, 400 wide, exclusion **Normal**, kb none, input mask = column | single |
| `ScreenshotOsd.qml` | window | Corner preview card after a screenshot: click opens, Copy/Edit/Folder/Delete chips, swipe to dismiss, countdown line; IPC `screenshot notify` | PanelWindow ns `quickshell-screenshot`, O, anchored to configured corner, margins 6/14, 360×300, exclusion Normal, kb none, mask = card while showing | single |
| `Wallpaper.qml` | window | Draws wallpaper with 2 slots + 7 transitions, fill modes, parallax by active workspace, dim, slideshow, kills awww/swaybg once drawn | PanelWindow ns `kusanagi-wallpaper`, layer **B**, TBLR, Ignore, kb none, empty input region | **per screen** (`Variants` over `Quickshell.screens`), only if renderer = kusanagi |
| `Lock.qml` | service+window | Lock engine: hyprlock fallback or own `WlSessionLock`; PAM (`/etc/pam.d/hyprlock`); test mode (Esc unlocks, 30 s failsafe); unlock animation delay ms(420) | `WlSessionLock` → one `WlSessionLockSurface` per output (black bg) | **per screen** (by protocol) |
| `LockScreen.qml` | widget | Per-output lock content: blurred/dimmed/desaturated wallpaper, 128 px clock + date, avatar, greeting, password pill (dots, spinner, shake), error, now-playing pill, test banner | inside lock surface | per screen |
| `Settings.qml` | window | Settings app: sidebar (brand, search field, grouped nav with sliding highlight), page header, scrolled page Loader, slim scrollbar, Esc hides | **FloatingWindow** (xdg_toplevel) "Kusanagi Settings", 1080×720, min 820×520, translucent bg | single |
| `Shortcuts.qml` | service | Hyprland global shortcuts appid `kusanagi`: mediaToggle/Next/Prev/Stop, session, showall, screenshot, screenshotFreeze | none (protocol objects) | — (Hyprland only, LazyLoader) |

### 1b. Singletons / services

| File | Kind | Role |
|---|---|---|
| `Config.qml` | singleton | settings.json ↔ live object (FileView watch + JsonAdapter, 250 ms coalesced write, self-write suppression, writes defaults on first run); `ms()`, `bounce()`, `reset(section)`, `allTiles` |
| `Theme.qml` | singleton | colors.json (watched) + 9 built-in palettes; derived colours (`bg`, `surfaceBorder`, …); `alpha()`; consts radius 5, anim 120/220/380 (unused) |
| `Wm.qml` | singleton | Compositor abstraction: kind (env sniff), workspaces list `{n,active,occupied,urgent,ref}`, activeIndex, fullscreen, focusWorkspace, scroll, setEffects, quit, monitorsCommand/parseMonitors, configFile; niri event-stream follower |
| `SysInfo.qml` | singleton | /proc + /sys stats with demand-driven timers (bar 2/3/5 s, detailed 1.5 s, df 30 s), hwmon/amdgpu discovery, 60-sample histories |
| `Notifs.qml` | singleton | Notification server (persistence, body, markup, actions, images), popups list, DND, history, timestamps/`ago()`, clearAll, activate |
| `GameMode.qml` | singleton | auto on fullscreen / manual; effects off via Wm, `gamemoded -r` held, quiet flag, grace timer; IpcHandler `gamemode` |
| `Presets.qml` | singleton | 10 built-in looks, `lookKeys`, apply / applyNamed / next / snapshot / save / remove → presets.json |

### 1c. Widgets (no surfaces)

| File | What |
|---|---|
| `WsIndicator.qml` | Workspace row: pills / dots / numbers / roman / kanji / custom / dwl; glow (RectangularShadow), urgent, hover, underline; used by bar + SpWorkspaces preview |
| `NotificationCard.qml` | One notification (popup or inbox variant): app icon+name+ago, ×, summary (2 lines), StyledText body (1–6 lines), image, action chips, critical red edge, countdown hairline, swipe/drag dismiss, tap = default action, right-click dismiss |
| `PresetPreview.qml` | Miniature bar drawing for a preset (gradient desktop, styles, ws styles, clock, status) |
| `CpHome.qml` | Panel Home: tiles grid (12 kinds, configurable order), output/mic sliders, now-playing card (seek), 4 mini-stat cards (→ System tab); gammastep check |
| `CpSystem.qml` | Panel System: 4 ring gauges, CPU sparkline + load, network rates + sparkline, VRAM/disk bars, user@host·uptime·kernel |
| `CpInbox.qml` | Panel Inbox: count, DND switch, Clear all, ListView of NotificationCards (max 470 px, add/removeDisplaced transitions), empty state |
| `CpCustomize.qml` | Panel Quick: preset chips, accent swatches, bar style, workspace style chips, motion speed, "All settings" |
| `CpCard` `CpChip` `CpField` `CpGauge` `CpIcon` `CpIconButton` `CpRow` `CpSection` `CpSegmented` `CpSlider` `CpSpark` `CpStepper` `CpSwitch` `CpText` `CpTile` | Design-system primitives (see §6 for their animations). CpGauge = 270° arc (Shape/CurveRenderer); CpSpark = Canvas polyline + gradient fill; CpField = TextInput pill; CpIcon = Nerd glyph by codepoint; CpText = full hinting + native rendering |
| `SpGroup.qml` | Settings titled card |
| `SpPresets` `SpAppearance` `SpBar` `SpWorkspaces` `SpPanel` `SpWallpaper` `SpLauncher` `SpLock` `SpNotifications` `SpGameMode` `SpSound` `SpDisplay` `SpNetwork` `SpStorage` `SpAbout` | The 15 settings pages (PERSONALIZE / SYSTEM groups). Sound = PipeWire devices/default/per-app; Display = monitors via compositor + night light; Network = nmcli devices + SysInfo; Storage = df + xbps cleanup in terminal; About = distro/CPU/GPU/WM/qs version + own PSS |

### 1d. Scripts & leftovers

| File | Status |
|---|---|
| `shell/scripts/cliplist.sh` (dup of `scripts/cliplist.sh`) | cliphist → `id\tkind\tvalue`, decodes ≤40 image entries to `~/.cache/rice/clip`, prunes. Keep as-is or reimplement natively (just `cliphist list/decode`). |
| `shell/scripts/start` | legacy shim → `bin/kusanagi start`. Drop in v5. |
| `shell/hyprquickpaper/{shell.qml,cache.sh,commands.sh,config.json}` | **Standalone legacy** Quickshell config (horizontal carousel picker with smoothstep scale, shear, drop shadows via MultiEffect, SmoothedAnimation scrolling; `Qt.quit()` on pick; calls `~/.config/rices/zei/wallpaper`). Not loaded by `shell.qml`. Recommend: drop, or later add as `wallpaper.picker_layout = "carousel"`. |
| `scripts/screenshot`, `scripts/colorpick` | grim/slurp/wl-copy/magick; screenshot calls `kusanagi msg screenshot notify <file>`. Keep. |

---

## 2. Quickshell / Qt dependency inventory → native replacement

### 2a. Quickshell modules & types

| Used (where) | Native replacement |
|---|---|
| `PanelWindow` + `WlrLayershell.{layer,namespace,keyboardFocus}`, `anchors`, `margins`, `exclusiveZone`, `exclusionMode` (Ignore/Normal), `implicitWidth/Height` | **wlr-layer-shell-unstable-v1** (v4+: `on_demand` keyboard interactivity). Exclusion Ignore → `exclusive_zone = -1`; Normal → 0. **Keep the namespaces** — Mango config has `layerrule=noanim:1 … ^(quickshell.*)$` and `noblur:1 … quickshell.*` (`~/.config/mango/{config,rice}.conf`). |
| `mask: Region {}` / `Region { item }` | `wl_surface.set_input_region` (empty region or rect of item; recompute when item geometry changes) |
| `visible: showing \|\| opacity>0.01` | Unmap = attach null buffer + commit; remap = redo the initial (bufferless) commit and wait for a fresh `configure` (or destroy/re-create when output/layer/anchors change). Keep GL resources alive while unmapped when `look.preload`. |
| `PopupWindow` (`anchor.item/rect/edges/gravity/adjustment Slide`) | **xdg-shell** `xdg_popup` via `zwlr_layer_surface_v1.get_popup` + `xdg_positioner` (anchor_rect, anchor, gravity, `constraint_adjustment = slide_x\|slide_y`) |
| `FloatingWindow` (title, minimumSize) | **xdg-shell** `xdg_toplevel` (set_title, set_app_id `kusanagi-settings`, set_min_size); optional **xdg-decoration-unstable-v1** (prefer server-side / none) |
| `WlSessionLock` / `WlSessionLockSurface` | **ext-session-lock-v1** (one `get_lock_surface` per wl_output, must ack_configure + commit buffer before `locked`; handle output hot-plug & `finished`) |
| `Quickshell.screens`, `Variants`, `screen:` | `wl_output` v4 (name/description) + **xdg-output-unstable-v1** fallback for logical geometry; per-output surface factory |
| HiDPI (implicit in Qt) | **wp-fractional-scale-v1** + **wp-viewporter**; `wl_surface.set_buffer_scale` fallback |
| `cursorShape: Qt.PointingHandCursor` (everywhere) | **wp-cursor-shape-v1**; fallback libwayland-cursor + XCURSOR_THEME/SIZE |
| `ToplevelManager.activeToplevel` (title, fullscreen) — Bar, Wm | **wlr-foreign-toplevel-management-unstable-v1** (`title`, `state` activated/fullscreen). (ext-foreign-toplevel-list-v1 has no state → not enough) |
| `Quickshell.WindowManager.windowsets` (`name, active, shouldDisplay, urgent, activate()`) — Wm (Mango / other) | **ext-workspace-v1** (`ext_workspace_handle_v1`: name, state active/urgent/hidden, `activate` + manager `commit`; groups ↔ outputs for per-monitor) |
| `Quickshell.Hyprland` (`Hyprland.workspaces`, `focusedWorkspace.{id,hasFullscreen}`, `toplevels`, `urgent`) | Hyprland IPC sockets: `$XDG_RUNTIME_DIR/hypr/$HIS/.socket2.sock` (events: workspace, createworkspace, destroyworkspace, openwindow, closewindow, movewindow, urgent, fullscreen, focusedmon, activewindow) + `.socket.sock` (`j/workspaces`, `j/activeworkspace`, `j/clients`, `j/monitors`). Commands still via `hyprctl` or the socket. |
| `GlobalShortcut { appid, name }` — Shortcuts | **hyprland-global-shortcuts-v1** (Hyprland only). Also expose same actions via IPC (`media …`, `power open`, `panel home`) for other compositors. |
| niri (Process `niri msg -j event-stream`, SplitParser) | Same command (or connect to `$NIRI_SOCKET` and send `"EventStream"`); JSON line parser; retry 2 s on exit |
| `Quickshell.Io.Process` / `StdioCollector` / `SplitParser` / `execDetached` | `posix_spawn` + pipes on the event loop (pidfd/signalfd for exit); detached = double-fork/`setsid`, close fds, reset signal mask |
| `FileView` (`watchChanges`, `blockLoading`, `reload`, `text()`) | read-on-demand + **inotify** (watch parent dir, handle atomic rename/replace) |
| `JsonAdapter` / `JsonObject` | TOML (toml++) for settings; JSON parser (simdjson/glaze/nlohmann) for colors.json, presets.json, launcher-usage.json, niri/hyprctl/mmsg output |
| `IpcHandler` + `qs ipc call/show` | Unix socket (§4) |
| `SystemClock` (Seconds/Minutes precision) | `timerfd` with `TFD_TIMER_ABSTIME` aligned to next second/minute; also re-arm on `TFD_TIMER_CANCEL_ON_SET` (clock change / resume) |
| `Quickshell.env`, `shellDir`, `shellPath` | getenv; install prefix data dir (`/usr/share/kusanagi`, assets/logo.svg) |
| `LazyLoader` / `Loader` / `OnDemand`, `gc()` | Widget-tree construct/destroy; preload = keep surface objects + trees alive but unmapped; "unload" = destroy tree, drop textures, `malloc_trim(0)` |
| `Quickshell.Services.Notifications` (`NotificationServer`, `trackedNotifications`, `urgency`, `actions[].invoke`, `image`, `appIcon`, `dismiss`, `keepOnReload`) | **D-Bus server `org.freedesktop.Notifications`** at `/org/freedesktop/Notifications`: Notify (replaces_id, hints `urgency`, `image-data`/`image_data`/`icon_data`, `image-path`, `desktop-entry`), CloseNotification, GetCapabilities (`body`, `body-markup`, `actions`, `persistence`, `icon-static`), GetServerInformation; signals NotificationClosed (1 expired / 2 dismissed / 3 closed), ActionInvoked. Name request with `DO_NOT_QUEUE`; report owner if taken (mako/dunst/swaync). |
| `Quickshell.Services.Mpris` (`players`, `isPlaying`, `trackTitle/Artist/ArtUrl`, `identity`, `position`, `length`, `canSeek`, `togglePlaying/next/previous/stop`, `positionChanged()`) | **D-Bus MPRIS2**: watch `NameOwnerChanged` for `org.mpris.MediaPlayer2.*`; `org.mpris.MediaPlayer2.Player` props PlaybackStatus, Metadata (`xesam:title`, `xesam:artist`[], `mpris:artUrl`, `mpris:length`, `mpris:trackid`), Position (Get, polled 1 s while visible+playing), CanSeek; `org.mpris.MediaPlayer2.Identity`; methods PlayPause/Next/Previous/Stop/SetPosition |
| `Quickshell.Services.SystemTray` (`items`, `icon`, `status`, `tooltipTitle`, `title`, `onlyMenu`, `hasMenu`, `menu`, `activate`, `secondaryActivate`, `scroll`) | **StatusNotifierWatcher** (`org.kde.StatusNotifierWatcher`, own it if free) + **StatusNotifierHost** registration; **StatusNotifierItem** props IconName, IconPixmap (ARGB32 big-endian), IconThemePath, AttentionIconName/Pixmap, Status, ToolTip, Title, ItemIsMenu, Menu; methods Activate(x,y), SecondaryActivate, Scroll(delta,"vertical") |
| `QsMenuOpener` / `QsMenuButtonType` (children, text, icon, enabled, visible, isSeparator, buttonType, checkState, hasChildren, `triggered()`) | **com.canonical.dbusmenu**: GetLayout(parent, depth, props), AboutToShow, Event(id,"clicked"/"opened"/"closed"), signals LayoutUpdated / ItemsPropertiesUpdated; props label, enabled, visible, type=separator, toggle-type checkmark/radio, toggle-state, icon-name, icon-data(PNG), children-display=submenu |
| `Quickshell.Services.Pipewire` (`defaultAudioSink/Source`, `preferredDefaultAudio*`, `nodes`, `audio.volume/muted`, `isSink`, `isStream`, `properties`, `nickname`, `description`, `name`, `PwObjectTracker`, `ready`) | **libpipewire-0.3** on its own thread loop: registry (Node, Metadata), `default` metadata (`default.audio.sink/source` read; `default.configured.audio.*` write), node Props param (`channelVolumes`, `mute`) read + set; volume mapping cubic like Quickshell/wpctl (verify against current values); marshal changes to main loop via eventfd |
| `Quickshell.Services.Pam` (`PamContext config/configDirectory`, `pamMessage responseRequired respond`, `completed PamResult`, `error`) | **libpam**: `pam_start_confdir("hyprlock", user, conv, "/etc/pam.d")` (or `pam_start`), `pam_authenticate` + `pam_acct_mgmt` on a worker thread; conversation answers every `PAM_PROMPT_ECHO_OFF/ON` with the password; result posted back |
| `DesktopEntries.applications` (`id,name,genericName,keywords,comment,icon,noDisplay,runInTerminal,command,actions[{name,icon,execute}],execute()`) | XDG Desktop Entry spec parser over `$XDG_DATA_HOME:$XDG_DATA_DIRS/applications` (id = path-derived, override precedence, `Hidden`, `NoDisplay`, `OnlyShowIn/NotShowIn`, localized keys, `Exec` field-code stripping, `Path`, `Terminal`, `[Desktop Action]`); inotify for live updates |
| `Quickshell.iconPath(name, true)`, `IconImage` | XDG Icon Theme spec lookup (theme from `gtk-3.0/settings.ini` = breeze here, `Inherits` chain, hicolor, `/usr/share/pixmaps`, absolute paths, `IconThemePath` for tray); SVG via **lunasvg/plutosvg or resvg**; PNG/XPM; cache by (name,size,scale) |
| `Quickshell.Widgets.ClippingRectangle` | Rounded-rect **SDF mask** applied in the image/child shader (or stencil for arbitrary children) |
| `Quickshell.Widgets.IconImage` | Textured quad from icon cache |

### 2b. QtQuick features with rendering/interaction impact

| Feature (where) | Native replacement |
|---|---|
| `Rectangle` radius + border + colour (everywhere) | **Rounded-rect SDF shader** (fill + inner border, AA via fwidth), per-corner radius not needed |
| `RectangularShadow` (blur, offset, radius, colour, opacity, scale) — every surface, WsIndicator glow | **Analytic rounded-box shadow** (erf/Gaussian approximation, Evan Wallace style) — no blur pass |
| `MultiEffect` blur (+brightness, saturation): Lock bg (blur = cfg·64, brightness −dim, saturation 0.1), Wallpaper "blur" transition (blur 1−p, blurMax 64), hyprquickpaper shadow | **Dual-Kawase blur** (downsample N levels by radius) + **colour-matrix pass** (brightness/saturation); cache lock result per output (static) |
| `ShaderEffectSource` (Wallpaper slots, wipe/grow/blinds reveal) | Wallpaper images are GPU textures already; reveals = scissor (wipe/blinds) or circle SDF mask (grow); "freeze" = free CPU pixels after upload |
| `Gradient` (vertical PresetPreview, horizontal SpAbout hero), Canvas `createLinearGradient` (CpSpark) | 2-stop linear-gradient fill in the rect shader |
| `Shape`/`PathAngleArc` round caps (CpGauge) | **Arc SDF** (ring segment with round caps) |
| `Canvas` polyline stroke (1.6 px, round join) + gradient area fill (CpSpark) | CPU tessellate to triangle strip (or small AA rasterizer → texture, repaint only on new sample) |
| `Text` (`NativeRendering`, `PreferFullHinting`, bold, `letterSpacing`, `capitalization AllUppercase`, `elide` Right/Middle, `wrapMode` Word/Wrap/WrapAnywhere, `maximumLineCount`, H/V align) | **fontconfig** (match + fallback for CJK kanji/Nerd PUA/emoji) + **FreeType** (full hinting, grayscale AA, integer advances — matches GTK, see Bar padR comments) + **HarfBuzz** shaping; glyph atlas per (face,px); line breaking (UAX#14 subset or libunibreak); ellipsis insertion right/middle |
| `textFormat: StyledText/RichText` (bar `<font color>`, calendar `<pre><b><u><small>`, notification body markup) | Mini markup parser → attributed runs: `b i u font color small pre br a img(ignore)`; escape everything else |
| `TextInput` (selection, `selectionColor`, `echoMode Password`, `cursorPosition`, `forceActiveFocus`, Keys) | Own line-edit: **xkbcommon** keymap/state, key repeat from `wl_keyboard.repeat_info`, compose (xkb_compose), selection + clipboard paste/copy via `wl_data_device` (Ctrl+V/C), optional **text-input-unstable-v3** for IME |
| `Image` (`asynchronous`, `sourceSize`, `fillMode` Crop/Fit/Stretch/Pad/Tile, `cache:false`, `status`) — wallpapers, thumbs, covers (incl. **http(s) art URLs**), ~/.face, clip previews (png/jpg/webp/gif/bmp), screenshots | Decode thread pool: libjpeg-turbo (DCT scaling), libpng, libwebp, giflib (1st frame), stb for bmp; scale to sourceSize on decode; libcurl + disk cache for http art; status callbacks |
| `opacity` on subtrees, `scale` + `transformOrigin`, `rotation`, `Translate` | Per-node affine transform + inherited alpha multiply (Qt semantics: per-item multiply, not group opacity) |
| `clip: true` | Scissor rect (Qt clip is rectangular even on rounded Rectangles) |
| `Flickable`/`ListView`/`GridView` (`StopAtBounds`, highlight w/ move+resize duration, `highlightMoveVelocity -1`, `currentIndex` kept visible, `positionViewAtIndex Center`, `add`/`removeDisplaced` transitions, `contentHeight`, `visibleArea`) | Scroll view widget: wheel + touchpad (axis_value120/axis_discrete), clamp, kinetic optional; virtualised list/grid with highlight animation; insert/remove transitions |
| `Column move: Transition` (notification stack) | Layout-position animation on reflow |
| `Row/Column/Flow/Grid/anchors/Repeater`, `childrenRect`, implicit sizes | Own layout engine: box layouts + flow + grid + anchors (left/right/center/fill with margins) and implicit-size propagation |
| `MouseArea` (hover, pressed, containsMouse, acceptedButtons L/R/M, wheel), `HoverHandler`, `TapHandler`, `DragHandler` (x-axis, min −20), `WheelHandler`, `Keys` | Pointer/keyboard dispatch with hit-testing, hover enter/leave, implicit grab on press, drag threshold; wheel normalized to ±1 steps (angleDelta/120) |
| `Behavior` / `NumberAnimation` / `ColorAnimation` / `ParallelAnimation` / `SequentialAnimation` / `ScriptAction` / `RotationAnimation` / `SmoothedAnimation`; easings Linear (default!), OutCubic, InCubic, OutQuint, OutBack(+overshoot, default 1.70158), BezierSpline | Animation engine (§6) driven by `wl_surface.frame` callbacks per surface |
| `Timer` | timerfd/monotonic deadline heap in the event loop |
| `Qt.formatDateTime` with user-supplied Qt format (`HH:mm`, `h:mm AP`, `ddd`, `dddd, d MMMM`, `d MMM`), `Qt.locale()` firstDayOfWeek / month & day names | Qt-format-compatible formatter (tokens d dd ddd dddd M MM MMM MMMM yy yyyy h hh H HH m mm s ss AP ap) using `nl_langinfo` names; first weekday via `_NL_TIME_FIRST_WEEKDAY` (glibc; Monday fallback on musl) |
| `FolderListModel` (name filters, sort by name case-insensitive) | `opendir` + filter + sort; inotify on the folder |
| Launcher calculator (`Function("with (Math) …")`) | Sandboxed **Luau** expression (math lib only) or hand-written recursive-descent parser |

---

## 3. External commands & files

### 3a. Commands (all can be spawned as-is from v5)

| Where | Command |
|---|---|
| Lock.lock (engine ≠ kusanagi) | `sh -c 'pidof hyprlock \|\| { c="$HOME/.config/rices/zei/generated/hyprlock.conf"; [ -f "$c" ] && hyprlock -c "$c" \|\| hyprlock; } \|\| swaylock -f'` |
| PowerMenu | `kusanagi msg lock lock`; `Wm.quit()`; `sh -c "loginctl suspend \|\| systemctl suspend"`; `… reboot`; `… poweroff` |
| Wm.focusWorkspace | Mango: ext-workspace `activate`; Hyprland: `hyprctl dispatch 'hl.dsp.focus({workspace="N"})'` (Lua cfg) / `hyprctl dispatch workspace N`; niri: `niri msg action focus-workspace N` |
| Wm.scroll | `mmsg dispatch viewtoleft,0` / `viewtoright,0`; `hyprctl dispatch … workspace e±1`; `niri msg action focus-workspace-up/down` |
| Wm (Mango defaults) | `sh -c 'cat "$1/mango/config.conf" "$1/mango/rice.conf" \| grep -E "^(blur\|shadows\|animations\|layer_animations)="'` |
| Wm.setEffects | Mango: `mmsg dispatch setoption,{blur,shadows,animations,layer_animations},V` (4×, one sh); Hyprland Lua: `hyprctl eval 'hl.config({decoration={blur={enabled=B},shadow={enabled=B}},animations={enabled=B}})'`; classic: `hyprctl --batch 'keyword decoration:blur:enabled …; keyword decoration:shadow:enabled …; keyword animations:enabled …'` |
| Wm.quit | `mmsg dispatch quit`; `hyprctl dispatch hl.dsp.exit()` / `exit`; `niri msg action quit --skip-confirmation`; `loginctl terminate-session "$XDG_SESSION_ID"` |
| Wm.monitorsCommand (SpDisplay) | `mmsg get all-monitors` / `hyprctl monitors -j` / `niri msg -j outputs` |
| Wm (niri) | `niri msg -j event-stream` (long-lived) |
| GameMode | `gamemoded -r` (held for as long as active) |
| SysInfo | discovery `sh -c` (hwmon names, drm cards with gpu_busy_percent, temp labels, nproc, osrelease, hostname); `ip -4 -o addr show dev IF`; `df -B1 --output=used,size /` |
| Bar | `kusanagi msg power toggle` (power module) |
| CpHome | `pgrep -x gammastep`; `sh -c "pkill -x gammastep"` / `sh -c "setsid -f gammastep -O T >/dev/null 2>&1; sleep 0.3"`; `kusanagi screenshot region`; `gsr-ui-cli toggle-record`; `kusanagi colorpick`; `kusanagi msg {wallpaper toggle, clipboard toggle, lock lock, settings open, launcher open}` |
| ControlPanel | `kusanagi msg lock lock` (via runClosed) |
| Launcher | `e.execute()`; `$terminal -e <cmd…>` (Terminal=true); `action.execute()`; `wl-copy <result>`; `sh -c <cmd>` / `$terminal -e sh -c "<cmd>; exec $SHELL"` |
| Clipboard | `shell/scripts/cliplist.sh`; `sh -c 'cliphist decode "$1" \| wl-copy'`; `sh -c "printf '%s\t\n' \"$1\" \| cliphist delete"`; `cliphist wipe` |
| Wallpaper | `sh -c "pkill -x awww-daemon; pkill -x swaybg"`; slideshow `kusanagi wallpaper <file>` |
| WallpaperPicker | `sh -c` loop: `magick "$f[0]" -thumbnail 400x225^ -gravity center -extent 400x225 -quality 85 "$thumbs/<name>.jpg"`; `kusanagi wallpaper <path>` |
| ScreenshotOsd | `xdg-open <file>`; `sh -c 'wl-copy --type image/png < "$1"'`; `sh -c '<editor> "$1"'` (default `swappy -f`); `xdg-open <dir>`; `rm -f <file>` |
| Shortcuts | `kusanagi screenshot region` |
| SpAbout | `sh -c` (`/etc/os-release`, `/proc/cpuinfo` model, `lspci -mm`, `mango -v` / `niri --version` / `hyprctl version -j`, `qs --version`); `thunar <shellDir>`; `mousepad settings.json`; `kusanagi restart` |
| SpDisplay | monitors cmd; `pgrep -x gammastep`; `sh -c "pkill -x gammastep; sleep 0.2; setsid -f gammastep -O T …"` |
| SpLauncher | `kusanagi msg launcher open`; `sh -c "printf '{\"counts\": {}}\n' > …/launcher-usage.json"`; `kusanagi msg clipboard toggle`; `cliphist wipe` |
| SpLock | `kusanagi msg lock test` / `lock lock` |
| SpNetwork | `nmcli -t -f DEVICE,TYPE,STATE,CONNECTION device` (every 5 s); `nm-connection-editor` |
| SpNotifications | `notify-send -a Kusanagi "Hello from Kusanagi" "…"`; `kusanagi msg osd preview volume` |
| SpSound | `pavucontrol` |
| SpStorage | `df -B1 --output=source,fstype,size,used,target -x tmpfs -x devtmpfs -x efivarfs -x overlay`; `du -sh /var/cache/xbps`; `xbps-query -O`; `sh -c "vkpurge list \| wc -l"`; `$terminal -e sh -c "sudo xbps-remove -O / -o / sudo vkpurge rm all; …read"` |
| SpWallpaper | `sh -c` random neighbour → `kusanagi wallpaper f`; `kusanagi msg wallpaper toggle`; `sh -c "sleep 0.4; kusanagi wallpaper restore"` |
| bin/kusanagi | `qs -p shell …`, `qs ipc call/show/kill`, `qs log`; `python3 lib/palette.py img colors.json` (→ `magick … -colors 16 histogram:info:-`); `$CONF/hooks/wallpaper <file>`; `awww-daemon`, `awww img f -t random --transition-duration 1`; `scripts/screenshot`, `scripts/colorpick`; `$VISUAL/$EDITOR/mousepad`; `fc-list`; doctor probes (`qs wl-copy cliphist grim slurp magick gammastep gamemoded swappy hyprlock nmcli pavucontrol foot`) |
| hyprquickpaper (legacy) | `bash cache.sh` (`jq`, `convert -thumbnail x500`), `bash commands.sh f` → `~/.config/rices/zei/wallpaper` |

### 3b. Files read / written

- **/proc**: `stat`, `meminfo`, `cpuinfo` (cpu MHz, model), `uptime`, `loadavg`, `net/route` (default route iface), `sys/kernel/{osrelease,hostname}`, `self/smaps_rollup` (Pss).
- **/sys**: `class/hwmon/hwmon*/{name,temp*_label,temp*_input,temp1_input}` (amdgpu, k10temp/coretemp/zenpower; Tctl/Tdie/Package id 0), `class/drm/card[0-9]/device/{gpu_busy_percent,mem_info_vram_used,mem_info_vram_total,pp_dpm_sclk}`, `class/net/IF/{statistics/rx_bytes,statistics/tx_bytes,uevent}` (DEVTYPE=wlan).
- **Config/state**: `~/.config/kusanagi/settings.json` (rw, watched), `colors.json` (r, watched; written by palette.py), `presets.json` (rw), `wallpaper` (path; r watched; written by CLI), `launcher-usage.json` (rw), `hooks/wallpaper` (CLI). `~/.config/hypr/hyprland.lua` (existence ⇒ Lua dispatchers), `$XDG_CONFIG_HOME/mango/{config,rice}.conf`, niri `config.kdl` (opened by Display page).
- **Media**: `~/.face`, wallpaper folder (`~/Pictures/Wallpapers`), `~/.cache/kusanagi/thumbs/*.jpg`, `~/.cache/rice/clip/*`, `~/Pictures/Screenshots` (`$KUSANAGI_SCREENSHOTS`), `assets/logo.svg`.
- **System**: `/etc/pam.d/hyprlock`, `/etc/os-release`.
- **Env**: `MANGO_INSTANCE_SIGNATURE`, `HYPRLAND_INSTANCE_SIGNATURE`, `NIRI_SOCKET`, `HOME`, `USER`, `XDG_{CONFIG,CACHE}_HOME`, `XDG_SESSION_ID`, `VISUAL/EDITOR`.

---

## 4. IPC surface

### 4a. Today (`kusanagi msg <target> <fn> [args]` → `qs -p shell ipc call …`)

| Target | Functions (args) | Defined in |
|---|---|---|
| `panel` | `toggle` · `home` · `system` · `inbox` · `quick` (tab toggles if already on it) | shell.qml |
| `notifs` | `toggle` (= panel Inbox) · `open` · `close` · `dnd` (toggle) · `clear` | shell.qml |
| `settings` | `toggle` · `open` · `show` · `hide` · `page(name: string)` · `sound` · `network` | shell.qml |
| `launcher` | `toggle` · `open` · `close` · `search(text: string)` · `actions` | shell.qml |
| `wallpaper` | `toggle` (picker) | shell.qml |
| `power` | `toggle` · `open` | shell.qml |
| `clipboard` | `toggle` | shell.qml |
| `lock` | `lock` · `test` | shell.qml |
| `preset` | `apply(name: string)` · `next` | shell.qml |
| `osd` | `preview(kind: string)` volume\|mic\|game | shell.qml |
| `bar` | `media` (pin popover) · `traymenu(n: int)` | shell.qml |
| `gamemode` | `toggle` · `on` · `off` · `auto(on: bool)` | GameMode.qml |
| `screenshot` | `notify(path: string)` | ScreenshotOsd.qml |

Hyprland global shortcuts (appid `kusanagi`): `mediaToggle mediaNext mediaPrev mediaStop session showall screenshot screenshotFreeze` (user keybinds.lua also references `sidebar clearNotifs brightnessUp/Down` which are **not** implemented today).

### 4b. Callers

- **compositors/** (mango.conf, hyprland.conf, hyprland.lua, niri.kdl — identical sets): `launcher toggle`, `wallpaper toggle`, `clipboard toggle`, `notifs toggle`, `settings toggle`, `lock lock`, `gamemode toggle`, `power toggle`; plus `kusanagi screenshot full|region`, `kusanagi colorpick`, autostart `kusanagi`.
- **scripts/screenshot**: `screenshot notify <file>` (falls back to notify-send on failure).
- **bin/kusanagi**: `preset next`, `preset apply <n>`, `settings page <p>` / `settings open`, `wallpaper toggle` (`kusanagi wallpaper pick`); liveness via `qs ipc show`; `preset list` **sed-parses Presets.qml** (must change).
- **shell internal** (execDetached): Bar → `power toggle`; PowerMenu → `lock lock`; ControlPanel → `lock lock`; CpHome → `wallpaper toggle`, `clipboard toggle`, `lock lock`, `settings open`, `launcher open`; SpLauncher → `launcher open`, `clipboard toggle`; SpLock → `lock test|lock`; SpNotifications → `osd preview volume`; SpWallpaper → `wallpaper toggle`. In v5 these become direct in-process calls (no spawn).

### 4c. Proposed v5 socket

- Path: `$XDG_RUNTIME_DIR/kusanagi/$WAYLAND_DISPLAY.sock` (one instance per session; lock file `…/.lock` with `flock` = single-instance guarantee, replaces `pgrep`/`qs ipc show`).
- Wire: request = one line, JSON array of argv (`["launcher","search","fire fox"]`); reply = one JSON line `{"ok":true,"result":…}` / `{"ok":false,"error":"…"}`. Typed args coerced from strings (`int`, `bool` accepts `true/false/1/0/on/off`).
- **Every existing target/function keeps its name and arity** (table 4a) → `kusanagi msg launcher toggle` etc. unchanged; compositor files need no edits.
- New/meta commands: `ipc show` (list, replaces `kusanagi ipc`), `ping`, `version`, `quit`, `reload` (config+colors), `preset list` (JSON), `media toggle|next|prev|stop` (same as global shortcuts, usable from Mango/niri binds), `wallpaper set <file>` (in-process; CLI keeps doing palette + hook), `subscribe <topic…>` (stream events: workspace, notification, gamemode, volume, config) for scripts/Luau.
- Luau scripts can register extra targets (`ipc.register("mytarget", {fn=…})`), so IPC namespace = built-ins ∪ script targets.
- CLI: keep `bin/kusanagi` POSIX sh UX; replace `qs` calls with the binary: `kusanagi-shell` (daemon) and `kusanagi-shell msg …` (tiny client, or `socat`-free C++ subcommand). `log` → `$XDG_STATE_HOME/kusanagi/log`; `status` → `ping`.

---

## 5. Config mapping (settings.json → `~/.config/kusanagi/config.toml`)

Rules: table per section; keys converted to snake_case; values/semantics unchanged; missing keys = defaults below (Config.qml); first run with only settings.json → auto-migrate (keep the .json as `.json.bak`). The settings app keeps writing live with a 250 ms coalesce and self-write suppression; writes must be **format-preserving** (patch the key's value in place, append missing keys to their table) so hand comments survive.

| JSON key | TOML `[table] key` | Type / allowed | Default | Live value |
|---|---|---|---|---|
| look.font | `[look] font` | string family | "JetBrainsMono Nerd Font" | "Lilex Nerd Font" |
| look.radius | `[look] radius` | int px (UI 4–28) | 16 | 16 |
| look.accent | `[look] accent` | "" \| "#rrggbb" | "" | "" |
| look.palette | `[look] palette` | wallpaper\|catppuccin-mocha\|catppuccin-latte\|gruvbox\|nord\|rose-pine\|tokyo-night\|everforest\|kanagawa\|mono | wallpaper | |
| look.animSpeed | `[look] anim_speed` | 0 \| 0.7 \| 1 \| 1.4 (any float) | 1.0 | 1 |
| look.shadows | `[look] shadows` | bool | true | |
| look.preload | `[look] preload` | bool | true | |
| look.bounce | `[look] bounce` | 0–2 | 1.0 | |
| look.backdrop | `[look] backdrop` | 0–0.5 | 0.25 | |
| look.borders / borderAccent | `[look] borders` / `border_accent` | bool | true / false | |
| bar.style | `[bar] style` | islands\|solid\|floating\|clear | islands | |
| bar.position | `[bar] position` | top\|bottom | top | |
| bar.height | `[bar] height` | 24–40 | 28 | |
| bar.layout | `[bar] layout` | classic\|centered | classic | |
| bar.accentLabels | `[bar] accent_labels` | bool | false | |
| bar.opacity | `[bar] opacity` | 0–1 | 0.5 | |
| bar.radius | `[bar] radius` | 0–12 | 10 | |
| bar.fontSize | `[bar] font_size` | 9–14 | 11 | |
| bar.outline | `[bar] outline` | bool | false | |
| bar.clock | `[bar] clock` | Qt date format | "HH:mm" | |
| bar.clockBold | `[bar] clock_bold` | bool | true | |
| bar.trayIconSize | `[bar] tray_icon_size` | 10–22 | 14 | |
| bar.hoverGrow | `[bar] hover_grow` | bool | true | |
| bar.scrollClock / scrollStats | `[bar] scroll_clock` / `scroll_stats` | volume\|workspaces\|none | volume / volume | |
| bar.volumeStep | `[bar] volume_step` | 1–20 % | 5 | |
| bar.mediaPopup / marquee | `[bar] media_popup` / `marquee` | bool | true / true | |
| bar.mediaWidth / titleWidth | `[bar] media_width` / `title_width` | chars | 20 / 60 | |
| bar.modules.{title,media,cpu,ram,gpu,temp,volume,network,tray,power} | `[bar.modules] …` same names | bool | F,T,T,T,F,F,T,T,T,T | |
| workspaces.style | `[workspaces] style` | pills\|dots\|numbers\|roman\|kanji\|custom\|dwl | pills | |
| workspaces.shown | `[workspaces] shown` | 1–9 | 5 | |
| workspaces.glow | `[workspaces] glow` | bool | true | |
| workspaces.icons | `[workspaces] icons` | space-separated string (trimmed) | "" | "○ ○ ○ ○ ○\n" |
| workspaces.activeColor | `[workspaces] active_color` | accent\|accent2\|text | accent | |
| panel.opacity | `[panel] opacity` | 0.5–1 (also used by all surfaces) | 0.95 | |
| panel.width | `[panel] width` | 480–720 | 560 | |
| panel.defaultTab | `[panel] default_tab` | 0 home\|1 system\|2 inbox\|3 quick (accept names too) | 0 | |
| panel.showMedia / showStats | `[panel] show_media` / `show_stats` | bool | true / true | |
| panel.morph | `[panel] morph` | island\|drop\|fade | island | |
| panel.tiles | `[panel] tiles` | array ⊂ allTiles (ordered) | 8 tiles | |
| osd.position | `[osd] position` | top\|bottom\|right | top | |
| osd.timeout | `[osd] timeout` | ms | 1400 | |
| osd.volume / mic / gamemode | `[osd] volume` / `mic` / `gamemode` | bool | true | |
| osd.style | `[osd] style` | pill\|minimal | pill | |
| osd.showValue | `[osd] show_value` | bool | true | |
| notifications.position | `[notifications] position` | top-right\|top-center\|top-left | top-right | |
| notifications.timeout / max | `timeout` (ms) / `max` | | 5000 / 5 | |
| notifications.style | `style` | comfortable\|compact | comfortable | |
| notifications.progress / images | `progress` / `images` | bool | true / true | |
| launcher.position | `[launcher] position` | upper\|center | upper | center |
| launcher.width / rows / iconSize | `width` / `rows` / `icon_size` | | 640 / 7 / 32 | |
| launcher.descriptions / sortByUsage | `descriptions` / `sort_by_usage` | bool | true / true | |
| launcher.terminal | `terminal` | command | foot | |
| launcher.layout | `layout` | list\|grid | list | |
| wallpaper.folder | `[wallpaper] folder` | path (~ expanded) | ~/Pictures/Wallpapers | |
| wallpaper.columns | `columns` | 3–6 | 4 | |
| wallpaper.renderer | `renderer` | kusanagi\|awww | kusanagi | |
| wallpaper.transition | `transition` | fade\|blur\|wipe\|grow\|slide\|zoom\|blinds\|random | random | |
| wallpaper.duration | `duration` | ms | 1100 | |
| wallpaper.fill | `fill` | fill\|fit\|stretch\|center\|tile | fill | |
| wallpaper.parallax / dim | `parallax` (0–0.12) / `dim` (0–0.6) | float | 0.04 / 0 | |
| wallpaper.slideshow | `slideshow` | minutes, 0 off | 0 | |
| lock.engine | `[lock] engine` | hyprlock\|kusanagi | hyprlock | kusanagi |
| lock.blur / dim | `blur` (0–1) / `dim` (0–0.8) | float | 0.8 / 0.35 | 0.8 / 0 |
| lock.clock | `clock` | Qt format | HH:mm | |
| lock.avatar / media | `avatar` / `media` | bool | true / true | |
| lock.greeting | `greeting` | "" = $USER | "" | |
| display.nightTemp | `[display] night_temp` | K (2500–6500, step 100) | 4000 | |
| gamemode.auto/effects/feral/quiet/dnd | `[gamemode] auto` … `dnd` | bool | true ×5 | |
| gamemode.grace | `grace` | ms | 800 | |
| gamemode.announce | `announce` | manual\|always\|never | manual | never |
| screenshot.position | `[screenshot] position` | bottom-right\|bottom-left\|top-right\|top-left | bottom-right | |
| screenshot.timeout | `timeout` | ms | 6000 | |
| screenshot.editor | `editor` | command, file appended | "swappy -f" | |

New in v5 (optional, defaults reproduce today's single-output behaviour on 1 monitor):
`[bar] outputs = ["*"]` (names or `"*"`), `[overlays] output = "focused"` (`focused|primary|<name>`), `[notifications] output = "focused"`, `[osd] output = "focused"`, `[wallpaper.outputs.<name>] file = …` (per-output override, else shared `wallpaper` file), `[scripting] scripts = ["~/.config/kusanagi/scripts/*.luau"]`.

Other files:
- **colors.json** — keep the exact JSON schema (12 keys: text, textDim, danger, accent, accent2, border, bgPanel, bgCard, borderAccent, textFaint, ok, trackBg) and keep watching it; palette.py keeps writing it. Later: native port of palette.py (OKLCH k-means on our own decoder → drop python+ImageMagick dependency), same output. Built-in palettes move to data (`share/kusanagi/palettes/*.toml`).
- **presets.json** — user presets: migrate to `~/.config/kusanagi/presets.toml` (`[[preset]] id name note` + section tables restricted to `lookKeys`); read old JSON if TOML absent. Built-in 10 presets ship as data files; `kusanagi preset list` uses IPC `preset list` (no more sed on QML). Preset `id` "user:<name>" semantics kept; `lastApplied` → state file.
- **launcher-usage.json** → `$XDG_STATE_HOME/kusanagi/launcher-usage.json` (import old one).
- `wallpaper` (path file) + `hooks/wallpaper` unchanged.

---

## 6. Animations catalogue

Scaling: `Config.ms(base) = max(1, round(base × look.anim_speed))` (anim_speed 0 ⇒ 1 ms ≈ instant). `Config.bounce(base) = base × look.bounce` (OutBack overshoot; 0 ⇒ plain decelerate). Engine must support: Linear (QML default when no easing given), OutCubic, InCubic, OutQuint, OutBack(s), cubic-bezier, colour lerp (straight RGBA), integer-stepped props (font.pixelSize), sequences, infinite loops, pausable timelines, "Behavior" = retarget from current value on change, and enable/disable predicates.

| # | Where | Property | Duration (scaled?) | Easing |
|---|---|---|---|---|
| 1 | Launcher / Clipboard card | opacity 0↔1 (backdrop alpha follows) | 220 in / 140 out (ms) | linear |
| 2 | 〃 | scale 0.95→1 / 1→0.95 | 380 in / 160 out (ms) | OutBack bounce(1.3) / InCubic |
| 3 | 〃 | y −14→0 | 380 / 160 (ms) | OutQuint |
| 4 | Launcher card | height (result count) | 220 (ms) | OutQuint |
| 5 | WallpaperPicker card | opacity / scale 0.96 / y +16 | 220·140 / 400·160 / 400·160 (ms) | lin / OutBack 1.2·InCubic / OutQuint |
| 6 | PowerMenu card | opacity / scale 0.92 | 200·140 / 420·160 (ms) | lin / OutBack 1.4·InCubic |
| 7 | PowerMenu button | scale (pressed .92, current 1.05), colour | 220 / 160 (ms) | OutBack 2 / lin |
| 8 | ControlPanel `open` 0→1 | drives width/x (wP=clamp(1.5·o)), height (hP=clamp((o−.08)/.92)), content (cP=clamp((o−.42)/.58), y +10→0), radius bar→look, bg alpha bar→panel, border alpha, backdrop alpha, shadow alpha; fade morph: opacity min(1,1.4·o), scale .95+.05·o | 560 open / 300 close (ms) | bezier(0.05,0.7,0.1,1) / bezier(0.3,0,0.8,0.15) |
| 9 | ControlPanel page box | height | 380 (ms), only when open==1 | OutQuint |
| 10 | ControlPanel tab switch | x ±28→0 (direction by tab order) + opacity 0→1 | 380 / 260 (ms) | OutQuint / OutCubic |
| 11 | Bar clock island | opacity → 0 while panel open | 140 (**unscaled**) | linear |
| 12 | Bar island | colour | 250 (unscaled) | linear |
| 13 | Bar module hover-grow | font px +4 (integer steps) | 150 (unscaled) | OutCubic |
| 14 | Bar tooltip | show delay | 500 (unscaled) | — |
| 15 | Bar marquee | char shift tick | 150 ms timer | — |
| 16 | Media popover | open delay 280 (0 if pinned), close delay 260, unload ms(200); card y 4→12 & opacity | 260 / 180 (ms) | OutQuint / lin |
| 17 | Media progress (popover + CpHome) | width | 900 (ms) | linear |
| 18 | WsIndicator | slot width, dot w/h/colour, glow opacity, label colour, underline width | 250 (unscaled) | OutCubic |
| 19 | WsIndicator dwl | block colour | 160 (unscaled) | linear |
| 20 | OSD pill | slide 18→0 | 420 in / 200 out (ms) | OutBack bounce(1.4) / InCubic |
| 21 | 〃 | scale .88→1 | 420 / 200 (ms) | OutBack bounce(1.6) / InCubic |
| 22 | 〃 | opacity | 200 / 180 (ms) | linear |
| 23 | 〃 | width (kind morph) | 280 (ms) | OutQuint |
| 24 | 〃 | value bar width/height; icon disc colour | 160 OutCubic / 200 lin (ms) | |
| 25 | Notification toast arrival | translate x ±60 (or y −24 centred) → 0 | 480 (ms) | OutBack bounce(1.1) |
| 26 | 〃 | scale .94→1; opacity 0→1 | 480 OutBack bounce(1.4); 220 lin (ms) | |
| 27 | Toast stack reflow | y | 380 (ms) | OutQuint |
| 28 | Toast countdown | remaining 1→0 (hairline) | `notifications.timeout` (unscaled), paused on hover, not for critical | linear |
| 29 | NotificationCard dismiss | x → width+40, opacity → 0, then dismiss | 260 InCubic / 240 lin (ms) | |
| 30 | NotificationCard swipe release | x spring back to 0 (if < 30 % width) ; opacity = 1 − x/(1.2w) | 320 (ms) | OutBack bounce(1.2) |
| 31 | NotificationCard hover | bg colour, border colour; × opacity | 160 / 140 (ms) | linear |
| 32 | Inbox ListView | add opacity; removeDisplaced y | 240 / 260 (ms) | lin / OutCubic |
| 33 | ScreenshotOsd card | opacity; scale .86 from its corner | 200·160 / 460·180 (ms) | lin / OutBack 1.3·InCubic |
| 34 | 〃 | x swipe spring; countdown | 320 OutBack 1.2 (ms); `screenshot.timeout` linear, pause on hover | |
| 35 | TrayMenu card | opacity; scale .92 (origin top/bottom) | 160 / 300·160 (ms) | lin / OutBack 1.6·InCubic |
| 36 | TrayMenu entry | hover colour; submenu chevron rotation −90→0 | 100 lin / 180 OutCubic (ms) | |
| 37 | Lock bg | scale 1.08→1; opacity | 700 OutQuint / 420 OutCubic (ms) | |
| 38 | Lock content | opacity; scale .96→1 (unlock → 1.06) | 380 OutCubic / 520 OutQuint (ms); unlock delay ms(420) | |
| 39 | Lock pill | border colour | 200 (ms) | linear |
| 40 | Lock pill shake | x: −12(50) 10(70) −6(70) 3(60) 0(60) | unscaled sequence | linear |
| 41 | Lock spinner | rotation 0→360 loop | 900 (unscaled) | linear |
| 42 | Lock password dot | scale 0→1 on appear | 180 (ms) | OutBack bounce(3) |
| 43 | Wallpaper transition | p 0→1: fade (opacity), zoom (opacity + scale 1.12→1), slide (in from right, old −25 %), blur (blur 1→0, opacity min(1,1.6p)), wipe (4 dirs), grow (circle Ø = diag·p), blinds (12 slats, e = clamp(1.7p − .06i)) | `wallpaper.duration` (1 ms if anim_speed 0) | bezier(0.65,0,0.35,1) |
| 44 | Wallpaper parallax | stage x = −w·par·(min(9,ws)−1)/8 | 700 (ms) | OutQuint |
| 45 | CpChip | colour, border; scale .94 pressed | 180 lin; 160 OutBack bounce(2.5) (ms) | |
| 46 | CpIconButton | colour, icon colour; scale .9 | 160 lin; 160 OutBack 2.5 (ms) | |
| 47 | CpTile | colour (OutCubic), icon colour; scale .95 | 180; 160 OutBack 2.5 (ms) | |
| 48 | CpSwitch | track colour; knob x | 200 lin; 260 OutBack bounce(1.6) (ms) | |
| 49 | CpSegmented | highlight x; label/icon colour | 320 OutQuint; 200 lin (ms) | |
| 50 | CpSlider | fill width (disabled while dragging); colour | 200 OutCubic; 180 lin (ms) | |
| 51 | CpField / WallpaperPicker filter | focus border colour | 160 (ms) | linear |
| 52 | CpGauge | arc value | 700 (ms) | OutCubic |
| 53 | CpHome stat bar / card hover / cover fade | width; colour; opacity | 600 OutCubic; 160; 300 (ms) | |
| 54 | CpSystem usage bars | width | 700 (ms) | OutCubic |
| 55 | Accent swatch (CpCustomize, SpAppearance), palette card | scale 1.12 / 1.04 hover | 160 (ms) | OutBack **default overshoot 1.70158, not bounce-scaled** |
| 56 | SpPresets card | colour; scale .98 pressed | 140; 160 OutBack bounce(2) (ms) | |
| 57 | Settings nav highlight | move + resize | 220 (ms) | (ListView default) |
| 58 | Settings page in | opacity 0→1; y 14→0 | 240 OutCubic; 340 OutQuint (ms) | |
| 59 | Launcher list/grid highlight; hint opacity; grid icon scale 1.08 | move 160 (ms); 120 (ms); 180 OutBack bounce(2) (ms) | | |
| 60 | Clipboard highlight | move + resize | 160 (ms) | |
| 61 | WallpaperPicker cell | scale .96/.985/1; thumb fade; highlight | 220 OutBack bounce(1.4); 250; 180 (ms) | |
| 62 | SpGameMode badge | colour | 200 (ms) | linear |
| 63 | (legacy) hyprquickpaper | contentX SmoothedAnimation velocity 5000 dur 1000; smoothstep scale/edge compression | — | — |

---

## 7. Per-monitor behaviour today

- **Per output**: Wallpaper (one Background layer per `Quickshell.screens`, each with own slots, decode size = output size × (1+parallax)); Lock (one lock surface + LockScreen per output, shared password/PAM state, focus in each).
- **Single, compositor-chosen output** (layer surface with no output ⇒ usually the focused one at map time): Bar (!), control panel, launcher, clipboard, wallpaper picker, power menu, OSD, notification popups, screenshot flyout, tray menu, popups; Settings is a normal toplevel.
- Workspaces: Mango/ext-workspace — all windowsets named 1–9, *not* filtered per output (multi-monitor would mix groups); Hyprland — global ids 1..shown + any extra positive ids, no per-monitor split; niri — filtered to the **focused output's** workspaces.
- Parallax uses the global `Wm.activeIndex` on every output.
- Exclusive zone / panel morph assume the one bar; the panel reads `shell.barItem.clockIsland` geometry.
- Display page lists all monitors (compositor command, else `Quickshell.screens`).

v5 target: bar on every output in `[bar] outputs` (each with that output's workspace group: ext-workspace group↔output, Hyprland `monitor` field, niri `output`), popups/panel open on the output of the bar that was clicked, keyboard-invoked overlays on the focused output (Hyprland `focusedmon`, niri focused output, else let the compositor pick with `output = NULL`), parallax per output, hot-plug: create/destroy per-output surfaces, cancel animations on removed outputs, lock surfaces for outputs added while locked.

---

## 8. Risks / hard parts (ranked) and approach

1. **Lock screen safety.** Crash while locked = session stays locked (protocol); PAM blocks; outputs can appear while locked; must never unlock except PAM success / test mode. → Run the lock in a **separate small process** (`kusanagi-lock`, same renderer lib, minimal widget set) spawned by the shell; shell crash can't take it down, lock crash is recoverable by re-spawn (ext-session-lock allows a new client to take over only if the old one died — document TTY recovery as today). PAM on a worker thread; zero password copies (mlock + explicit_bzero); commit a frame on every lock surface before acknowledging; handle `finished`; 30 s failsafe only in test mode. Test headless (wlheadless/`cage`) first — never on the live session (see memory: no destructive tests).
2. **Text rendering parity.** The look depends on GTK-like full-hinted grayscale text with whole-pixel advances (Bar comments tune `padR` for Qt's wider Nerd-glyph advances — these hacks will need re-tuning). Needs fontconfig fallback (kanji, ○, ♪, ⏻, Nerd PUA, emoji in notifications — colour emoji = CBDT/COLRv1 via FreeType+cairo-less path or skip), HarfBuzz shaping, ellipsis right/middle, wrapping + max lines, letter spacing, uppercase, StyledText subset. → FreeType `FT_LOAD_TARGET_NORMAL` + hinting, round advances, per-size glyph atlas (R8), libunibreak, own markup parser; golden-image tests vs screenshots of v4.
3. **Blur / effects cost.** Wallpaper blur transition = full-res blur every frame on every output for ~1.1 s; lock blur; analytic shadows everywhere. → Dual-Kawase with radius→levels mapping that approximates MultiEffect `blur × blurMax 64`; downsample to ≤ 1/4; lock blur rendered once per output and cached. Analytic shadows (no FBO).
4. **System tray + DBusMenu.** Electron/Qt/Chromium items differ (ItemIsMenu, pixmaps in network byte order, IconThemePath, LayoutUpdated while open, AboutToShow, submenus lazily populated); must own `org.kde.StatusNotifierWatcher` if absent and register as host. → sd-bus (libsystemd / libelogind / **basu** — Void/runit has no systemd; Meson option), test against Vesktop, Mullvad, nm-applet, Steam.
5. **Notification server ownership & semantics.** Name conflicts (mako/dunst/swaync started by the session); replaces_id; image-data hint decoding; markup sanitising; actions + default; history kept across shell restarts (Quickshell `keepOnReload`) → v5 must persist history to state file or accept loss on restart. Note current quirk: a popup **timing out calls `dismiss()`**, so it leaves the Inbox too (Inbox only keeps unexpired/held ones) — decide: preserve exactly (default) or fix behind a flag. Client `expire_timeout` is ignored today (uses config).
6. **Icon lookup + SVG.** Launcher, tray, notification app icons, menu icons, logo.svg. Qt used platform theme (`QT_QPA_PLATFORMTHEME=qtengine`; gtk says breeze). → Full XDG icon spec with inherits + hicolor + pixmaps; SVG via lunasvg/resvg; size/scale cache; `iconPath(name, true)` "missing ⇒ glyph fallback" semantics.
7. **Widget system & animation engine breadth.** ~60 components rely on anchors, implicit sizes, Behaviors on almost every property, list/grid views with animated highlights, flow layouts, drag/tap/hover handlers. → Retained tree with dirty-flag layout, property objects that own an optional `Behavior` (animator retargets from current value), per-surface frame-callback driven ticking, damage tracking (`wl_surface.damage_buffer`) so the idle bar costs ~0 CPU.
8. **Multi-monitor & compositor matrix.** ext-workspace only on Mango (and others); Hyprland via sockets; niri via event stream; fullscreen detection differs (Hyprland `hasFullscreen` vs foreign-toplevel state); Hyprland Lua vs classic dispatchers; layer **namespaces must stay `quickshell-*`** for the user's Mango `layerrule`s; per-output fractional scales. → Keep the `Wm` façade as a C++ interface with 4 backends; integration test each under nested sessions.
9. **PipeWire.** Volume curve (cubic) must match current numbers; default-node metadata; stream nodes for per-app volume; thread hand-off. → libpipewire thread loop + eventfd queue; compare with `wpctl get-volume`.
10. **Image pipeline & memory.** Async decode with downscale, HTTP(S) cover art (`mpris:artUrl` from Spotify etc. is https), GIF/WebP, freeing CPU copies after upload; current launcher sets jemalloc decay to 0 for a lean RSS. → Thread pool, libjpeg-turbo DCT scaling, libcurl with small disk cache, `malloc_trim` after unloads.
11. **Text input.** Launcher/clipboard/picker/settings fields/lock: xkb keymaps, repeat, compose, Ctrl+J/K/Tab/Backtab/PageUp/Down semantics, selection/paste (wl_data_device), optional IME (text-input-v3). Exclusive vs on-demand keyboard focus per surface.
12. **Config write-back preserving comments** (TOML) and live reload without feedback loops. → Format-preserving patcher (own minimal TOML editor or toml++ parse + line patch), inotify with self-write token.
13. **Qt date-format & locale parity** (user-typed formats in `bar.clock`, `lock.clock`; calendar tooltip first weekday). → Own formatter; musl lacks `_NL_TIME_FIRST_WEEKDAY`.
14. **Calculator safety** (JS `Function` today). → Luau sandbox with only `math`, or a tiny parser.

---

## 9. Proposed build order (each milestone = compiling, usable binary)

| M | Deliverable | Contents |
|---|---|---|
| **M0** | `kusanagi-shell` skeleton | Meson + C++23; epoll loop (timerfd/signalfd/eventfd/inotify); wl registry, outputs (v4 + xdg-output, hot-plug), layer-shell surface per output, EGL/GLES3 clear, fractional-scale + viewporter, frame callbacks; Unix socket with `ping`/`ipc show`/`quit`; single-instance lock; `kusanagi msg` client; log file |
| **M1** | Bar on every output: clock + workspaces | Renderer v1 (rounded-rect SDF + border, analytic shadow, scissor, transforms/alpha), text stack (fontconfig/FreeType/HarfBuzz/atlas, Nerd glyphs, kanji fallback), widget tree + Row/anchors layout, animation engine (all easings, ms()/bounce()), TOML config load + watch + JSON→TOML migration, colors.json + palettes; `Wm` backends (ext-workspace, Hyprland sockets, niri stream) incl. per-output groups; WsIndicator all 7 styles; bar styles/position/layout/exclusive zone; clock formats; namespaces `quickshell-bar` |
| **M2** | Complete bar | SysInfo (demand-driven timers), CPU/RAM/GPU/temp/network modules, PipeWire volume (scroll/mute/click), MPRIS media + marquee, foreign-toplevel window title, tooltips (xdg_popup) incl. calendar markup, hover-grow, scroll actions, SNI tray + icon theme + SVG, tray menus (dbusmenu, overlay surface), media popover, power module; IPC `bar media/traymenu` |
| **M3** | Notifications, OSD, game mode | `org.freedesktop.Notifications` server, toast stack (arrival/move/countdown/swipe/actions/images/markup), DND; OSD pill (3 positions, 2 styles); ScreenshotOsd + `screenshot notify`; GameMode (fullscreen detect, effects backends, `gamemoded -r`, grace, quiet); IPC `notifs`, `osd`, `gamemode`, `screenshot` — **at this point v5 can replace v4 for daily use except overlays** |
| **M4** | Overlays | Keyboard (xkbcommon, repeat, compose), TextInput widget, scroll/list/grid views with highlight, image decode pool; Launcher (desktop entries, fuzzy score, usage, calc, run, actions, list/grid), Clipboard (cliphist, previews), Wallpaper picker (thumbs), Power menu; backdrop + exclusive/on-demand focus; preload/unload policy; IPC `launcher clipboard wallpaper power` |
| **M5** | Wallpaper engine | Background surface per output, fill modes, 2-slot swap with GPU-only retention, 7 transitions (dual-Kawase for blur, SDF circle for grow, scissor wipes/blinds), parallax per output, dim, slideshow, awww hand-over |
| **M6** | Control panel + presets | Island morph from the clicked output's bar, 4 tabs (tiles, sliders, now playing w/ seek, stats; gauges = arc SDF; sparklines; inbox list transitions; quick settings), presets engine (built-ins as data, user presets TOML), IPC `panel`, `preset`, media global shortcuts (hyprland-global-shortcuts-v1) + IPC `media` |
| **M7** | Lock | `kusanagi-lock` process: ext-session-lock, per-output surfaces + hot-plug, PAM worker, blurred wallpaper (cached), clock/avatar/greeting/media/shake/dots/spinner, test mode with failsafe; hyprlock engine fallback; IPC `lock lock/test` |
| **M8** | Settings app | xdg_toplevel window, sidebar search + nav highlight, scroll view, all 15 pages incl. PresetPreview, Sound (PipeWire devices/defaults/streams), Display (monitor list + gammastep), Network (nmcli), Storage (df/xbps), About (own PSS); format-preserving config writes; IPC `settings` — **feature parity with v4 reached** |
| **M9** | Luau + polish | Luau VM: config read/write, IPC target registration, event subscriptions, timers, exec, custom bar modules (text/icon/tooltip/click) and tiles; sandboxed calculator; `subscribe` IPC; native palette generator (drop python/ImageMagick dep); `kusanagi` CLI parity (`log`, `doctor`, `preset list` via IPC); remove `qs`; golden-image regression suite vs v4 screenshots |
