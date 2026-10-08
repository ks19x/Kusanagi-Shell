# The bar engine

Kusanagi's bar is data. `settings.json → "bars"` is a list of bars; each bar is a list of
groups and modules with a style that cascades bar → group → module. An empty list (`[]`)
means the classic bar, built from the Settings → Bar options.

You can build one three ways:

- **Settings → Bar → Custom layout**: a live editor with a preview, templates, and raw JSON.
- **Templates**: `kusanagi msg bar templates` lists them, `kusanagi msg bar template powerline`
  applies one, and `kusanagi msg bar classic` goes back to the classic bar.
- **By hand**: edit `~/.config/kusanagi/settings.json`. It is watched, so changes show on save.

Data sources only run for modules that are on a bar (CPU/RAM/GPU/net/disk polling, the mic
tracker, weather, and so on), and module code is only loaded for the types you use.

## A bar

```json
{
  "position": "top",          // top | bottom | left | right
  "screen": "",               // "" = every screen, "DP-1", or ["DP-1", "HDMI-A-1"]
  "size": 28,                 // thickness
  "length": 0,                // 0 = full, 0.5 = half the screen, 600 = px, "auto" = fit the content
  "align": "center",          // start | center | end (for short bars)
  "margin": [4, 0, 8],        // [edge, inner, sides]: gap to the screen edge, extra room before windows, side gaps
  "padding": 6,               // room at both ends
  "spacing": 4,               // between entries of a section
  "bg": "bg/0.8",             // colour, or ["accent", "accent2"] for a gradient
  "radius": 12,               // or [tl, tr, br, bl]
  "border": "accent", "borderWidth": 1,
  "line": { "pos": "bottom", "width": 1, "color": "accent/0.4" },
  "fg": "text", "font": "", "fontSize": 0,   // 0 = Settings → Appearance
  "exclusive": true,          // false = float over windows; a number = reserve exactly that
  "layer": "top",             // top | overlay | bottom
  "autohide": false,          // slide away; touch the edge to bring it back
  "group":  { ... },          // style every group starts from
  "module": { ... },          // style every module starts from
  "start":  [ entries ], "center": [ entries ], "end": [ entries ]
}
```

On a side bar, `start` is the top and `end` is the bottom.

## Entries

```json
"clock"                                         // a module with its defaults
{ "type": "cpu", "format": " {usage}%" }       // a module with options
{ "type": "group", "bg": "card", "radius": 8,   // an island holding modules
  "modules": [ "cpu", "ram" ] }
```

## Style (groups and modules)

| key | |
|---|---|
| `bg`, `fg`, `border`, `borderWidth`, `radius`, `opacity` | the look; `bg` may be a two-colour gradient |
| `padding` | `[before, after]` inside, along the bar |
| `gap` | `[before, after]` outside, along the bar |
| `inset` | `[edge, inner]` across the bar (shorter islands) |
| `capStart`, `capEnd` | `none` `round` `arrow` `arrow-in` `slant` `slant-back` (powerline) |
| `capStartBg`, `capEndBg` | colour behind a cap, so touching segments show their neighbour |
| `line` | `{ "pos": "bottom", "width": 2, "color": "accent", "length": 1, "round": false }` |
| `font`, `fontSize` (`14`, or `"+2"` relative to the bar), `bold`, `italic` | text |
| `hoverBg`, `hoverFg`, `hoverGrow` | hover |
| `rotate` | side bars: turn the text (default: only when it doesn't fit; `false` = never) |
| `spacing` | groups: room between modules |

## Colours

Theme tokens follow the wallpaper or palette: `accent` `accent2` `text` `dim` `faint` `bg`
`card` `danger` `warn` `ok`. You can also use `#rrggbb` or `transparent`. Add `/0.5` for
alpha (`"bg/0.6"`, `"#ffffff/0.15"`).

## Formats

`{name}` inserts a value; `{name:4}` pads it left and `{name:-4}` pads it right. Formats may
use markup: `<b>`, `<i>`, `<u>`, `<br>`, and `<font color="accent">` (tokens work there too).
`\n` makes a second line. `{icon}` picks from `icons`: either a list chosen by level
(0–100, like volume), or a map by state.

## States

Modules have states. Some are built in (`muted`, `paused`, `playing`, `charging`, `wifi`,
`dnd`, and so on), plus `hover`, plus `alt` (toggled by the `alt` action), plus threshold
states:

```json
{ "type": "cpu", "states": { "warning": 60, "critical": 90 },
  "when": { "warning": { "fg": "warn" }, "critical": { "bg": "danger", "fg": "bg" },
            "hover": { "format": "{usage}% busy" } } }
```

`when` can override any key, including `format` and `icons`. `formatAlt` is shorthand for
`when.alt.format`. Battery thresholds count down (`warning` at 30 or less).

## Actions

These module and group keys take an action: `click`, `rightClick`, `middleClick`,
`scrollUp`, `scrollDown`.

The built-in actions are:

- `panel`, `panel:home`, `panel:sound`, `panel:network`, `panel:system`, `panel:inbox`, `panel:quick`
- `launcher`, `settings`, `settings:<page>`, `power`, `wallpaper`, `clipboard`, `lock`, `notifs`
- `dnd`, `caffeine`, `gamemode`, `preset:next`, `preset:<name>`, `alt`, `setup`
- `record` (replay → save a clip, recording → stop, off → record), `record:replay`, `record:save`, `record:record`, `record:stream`, `record:stop`
- `updates:upgrade`, `updates:check`
- `media:toggle`, `media:next`, `media:prev`, `media:popup`
- `volume:up`, `volume:down`, `volume:mute`, `mic:up`, `mic:down`, `mic:mute`
- `workspace:prev`, `workspace:next`
- `none`

Anything else runs as a shell command (`"click": "pavucontrol"`).

## Modules

| type | format values · options |
|---|---|
| `workspaces` | `style` pills dots numbers roman kanji custom dwl, `icons`, `glow`, `colors {active occupied empty urgent onActive}` |
| `title` | `{title}` `{app}` · `maxLength` |
| `taskbar` | open windows · `titles`, `titleWidth`, `iconSize`, `spacing`, `colors {active hover text}` |
| `clock` | `{time}` `{date}` · `timeFormat`, `dateFormat` (Qt formats); tooltip is a calendar |
| `media` | `{track}` (scrolling) `{title}` `{artist}` `{album}` `{player}` · `width`, `marquee`, `popup` |
| `cpu` | `{usage}` |
| `ram` | `{percent}` `{used}` `{total}` `{free}` (GiB) |
| `gpu` | `{usage}` `{vramUsed}` `{vramTotal}` |
| `temp` | `{temp}` `{cpu}` `{gpu}` · `sensor` cpu or gpu |
| `disk` | `{percent}` `{used}` `{total}` `{free}` (root filesystem) |
| `network` | `{icon}` `{ifname}` `{ip}` `{down}` `{up}` · states wifi ethernet disconnected |
| `volume`, `mic` | `{icon}` `{volume}` · `step` · state muted |
| `battery` | `{icon}` `{capacity}` `{time}` · hidden without a battery |
| `tray` | `iconSize`, `spacing` |
| `notifications` | `{icon}` `{count}` · states dnd unread none |
| `weather` | `{icon}` `{temp}` `{unit}` `{feels}` `{desc}` `{place}` |
| `uptime` | `{uptime}` `{load}` |
| `caffeine`, `gamemode` | shown while on; `"always": true` to show them always (states on/off) |
| `recorder` | `{icon}` `{time}` `{mode}` `{backend}` · shown while recording, replaying or streaming (`always`) · states off replay record stream |
| `updates` | `{count}` · hidden when up to date (`always`) · states none some many unknown · click upgrades, right click checks |
| `launcher`, `power` | buttons |
| `text` | `text`: anything |
| `sep` | `format`: the glyph (default │) |
| `spacer` | `size` in px |
| `custom` | see below |

### custom: your own module

```json
{ "type": "custom", "exec": "~/bin/vpn-status", "interval": 10 }
{ "type": "custom", "exec": "playerctl -F metadata title", "stream": true }
```

The output is the text, or JSON
`{ "text", "tooltip", "class", "percentage", ...anything }`, the same as waybar. Every
JSON field becomes a `{var}`; `class` becomes a state for `when`, and `percentage`
becomes the level for `icons` and thresholds.

| option | |
|---|---|
| `interval` | seconds between runs (`0` = once) |
| `stream: true` | keep it running; every line is an update |
| `refresh: true` | re-run right after a click action |
| `game: true` | keep running in game mode (it pauses otherwise) |

## Example: a small powerline

```json
"bars": [{
  "position": "top", "size": 24, "bg": "bg",
  "module": { "padding": [8, 8], "gap": [0, 0], "fg": "bg", "bold": true },
  "start":  [ { "type": "workspaces", "style": "numbers", "bg": "card", "capEnd": "arrow" } ],
  "end": [
    { "type": "cpu", "format": "{usage}%", "bg": "accent2", "capStart": "arrow" },
    { "type": "clock", "bg": "accent", "capStart": "arrow", "capStartBg": "accent2",
      "formatAlt": "{date}", "click": "alt" }
  ]
}]
```
