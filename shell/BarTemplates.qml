pragma Singleton
// BarTemplates.qml — ready-made bar layouts (Config.bars values) for Settings → Bar and the presets.
// Each is just a spec (docs/bar.md): apply one, then change anything in the editor.
import Quickshell
import QtQuick

Singleton {
    id: root

    function get(id) { return list.find(t => t.id === id) ?? null }
    // a fresh copy (the editor mutates what it is given)
    function bars(id) { const t = get(id); return t ? JSON.parse(JSON.stringify(t.bars)) : [] }

    readonly property var list: [
        {
            id: "aurora", name: "Aurora", note: "floating glass islands, a gradient clock, soft outlines",
            bars: [{
                position: "top", size: 38, padding: 10, spacing: 8,
                group: { bg: "bg/0.55", radius: 14, inset: [7, 3], padding: [6, 6], border: "text/0.1", borderWidth: 1 },
                module: { padding: [8, 8], gap: [0, 0] },
                start: [{ type: "group", modules: [{ type: "launcher", logo: true, padding: [6, 4] }, { type: "workspaces", style: "pills", padding: [6, 6] }] },
                        { type: "group", modules: [{ type: "title", maxLength: 42, fg: "dim" }] }],
                center: [{ type: "group", bg: ["accent", "accent2"], borderWidth: 0,
                           modules: [{ type: "clock", fg: "bg", bold: true, format: "{time}", formatAlt: "{date}", click: "alt", padding: [14, 14] }] }],
                end: [{ type: "group", modules: [{ type: "media", width: 22, fg: "dim" }] },
                      { type: "group", modules: [{ type: "cpu", format: "\u{f0ee0}  {usage}%" }, { type: "ram", format: "\u{f035b}  {percent}%" }, { type: "volume", format: "{icon}  {volume}%" }] },
                      { type: "group", modules: [{ type: "network" }, { type: "tray" }, { type: "power", fg: "danger" }] }]
            }]
        },
        {
            id: "material", name: "Material You", note: "tonal, fully rounded chips in your accent",
            bars: [{
                position: "top", size: 40, padding: 8, spacing: 6, fontSize: 12,
                module: { bg: "accent/0.14", fg: "accent", radius: 20, inset: [6, 2], padding: [14, 14], gap: [2, 2], bold: true, hoverBg: "accent/0.26" },
                start: [{ type: "launcher", logo: true, padding: [10, 10] },
                        { type: "workspaces", style: "pills", padding: [10, 10], colors: { active: "accent", occupied: "accent/0.7", empty: "accent/0.3" } },
                        { type: "title", bg: "transparent", fg: "text", bold: false, hoverBg: "" }],
                center: [{ type: "clock", bg: "accent", fg: "bg", timeFormat: "HH:mm", format: "{time}", formatAlt: "{date}", click: "alt", hoverBg: "accent" }],
                end: [{ type: "media", width: 20 }, { type: "volume", format: "{icon}  {volume}%" }, { type: "network" },
                      { type: "tray", bg: "accent/0.08" }, { type: "power", bg: "danger/0.18", fg: "danger", hoverBg: "danger/0.3" }]
            }]
        },
        {
            id: "candy", name: "Candy", note: "colourful rounded segments, one colour per module",
            bars: [{
                position: "top", size: 32, padding: 8, spacing: 6,
                module: { fg: "bg", bold: true, radius: 15, inset: [5, 2], padding: [12, 12], gap: [0, 0] },
                start: [{ type: "workspaces", style: "numbers", bg: "card", padding: [10, 10], colors: { active: "accent", occupied: "text", empty: "faint" } },
                        { type: "title", bg: "card", fg: "text", bold: false, maxLength: 40 }],
                center: [{ type: "clock", bg: "accent", format: "\u{f0150}  {time}", formatAlt: "\u{f00ed}  {date}", click: "alt" }],
                end: [{ type: "cpu", bg: "ok", format: "\u{f0ee0} {usage}%" }, { type: "ram", bg: "warn", format: "\u{f035b} {percent}%" },
                      { type: "volume", bg: "accent2", format: "{icon} {volume}%" }, { type: "tray", bg: "card" }, { type: "power", bg: "danger" }]
            }]
        },
        {
            id: "line", name: "Line", note: "quiet strip, dots in the middle, an accent hairline",
            bars: [{
                position: "top", size: 26, bg: "bg/0.7", line: { pos: "bottom", width: 1, color: "accent/0.35" }, fg: "dim",
                module: { padding: [10, 10], gap: [0, 0], hoverFg: "text" },
                start: [{ type: "clock", timeFormat: "ddd d MMM", fg: "text" }, { type: "title", maxLength: 50 }],
                center: [{ type: "workspaces", style: "dots" }],
                end: [{ type: "media", format: "{title}", width: 28 }, { type: "volume", format: "vol {volume}%" },
                      { type: "clock", timeFormat: "HH:mm", fg: "text", bold: true }, { type: "tray" }]
            }]
        },
        {
            id: "notch", name: "Notch", note: "a black island hanging from the top, like a phone's",
            bars: [{
                position: "top", size: 32, length: "auto", padding: 16, bg: "#000000", radius: [0, 0, 18, 18],
                module: { padding: [8, 8], gap: [0, 0], fg: "#ffffff" },
                center: [{ type: "workspaces", style: "dots", colors: { active: "#ffffff", occupied: "#ffffff/0.7", empty: "#ffffff/0.3" } },
                         { type: "clock", bold: true, format: "{time}", formatAlt: "{date}", click: "alt", padding: [14, 14] },
                         { type: "media", width: 24, fg: "#ffffff/0.75" }, { type: "volume", format: "{icon}" }]
            }]
        },
        {
            id: "win11", name: "Windows 11", note: "centred taskbar — pin apps under Dock & taskbar",
            bars: [{
                position: "bottom", size: 48, bg: "bg/0.88", line: { pos: "top", width: 1, color: "text/0.08" },
                module: { padding: [8, 8], gap: [0, 0], radius: 6, inset: [5, 5], hoverBg: "text/0.08" },
                center: [{ type: "launcher", logo: true, fontSize: "+4", padding: [10, 10] },
                         { type: "taskbar", iconSize: 24, spacing: 4, indicator: "line", hoverBg: "" }],
                end: [{ type: "tray", hoverBg: "" },
                      { type: "group", radius: 6, inset: [5, 5], click: "panel",
                        modules: [{ type: "network", padding: [6, 4] }, { type: "volume", format: "{icon}", padding: [4, 8] }] },
                      { type: "clock", format: "{time}\n{date}", dateFormat: "d/M/yyyy", fontSize: "-1", padding: [10, 12] },
                      { type: "notifications", padding: [8, 12] }]
            }]
        },
        {
            id: "cyber", name: "Cyber", note: "neon slants, kanji tags, seconds ticking",
            bars: [{
                position: "top", size: 30, bg: "bg/0.92", line: { pos: "bottom", width: 2, color: "accent" }, fg: "accent", padding: 4, spacing: 4,
                group: { bg: "accent/0.14", capStart: "slant", capEnd: "slant-back", inset: [5, 3], padding: [4, 4] },
                module: { padding: [8, 8], gap: [0, 0], bold: true },
                start: [{ type: "group", modules: [{ type: "workspaces", style: "kanji", colors: { active: "accent", occupied: "accent2", empty: "accent/0.35" } }] },
                        { type: "group", bg: "accent2/0.14", modules: [{ type: "title", fg: "accent2", maxLength: 40 }] }],
                center: [{ type: "group", bg: "accent", modules: [{ type: "clock", fg: "bg", timeFormat: "HH:mm:ss" }] }],
                end: [{ type: "group", modules: [{ type: "cpu", format: "CPU {usage:3}%" }, { type: "ram", format: "MEM {percent:3}%" }, { type: "temp", format: "{temp}°" }] },
                      { type: "group", bg: "accent2/0.14", modules: [{ type: "volume", fg: "accent2", format: "VOL {volume}" }, { type: "tray" }] }]
            }]
        },
        {
            id: "zen", name: "Zen", note: "one small floating pill at the bottom, nothing else",
            bars: [{
                position: "bottom", size: 36, length: "auto", margin: [10, 0, 0], padding: 14, bg: "bg/0.7", radius: 18,
                border: "text/0.08", borderWidth: 1, exclusive: false,
                module: { padding: [8, 8], gap: [0, 0] },
                center: [{ type: "workspaces", style: "dots" }, { type: "sep" }, { type: "clock", bold: true }, { type: "sep" },
                         { type: "volume", format: "{icon}  {volume}%" }, { type: "tray" }]
            }]
        },
        {
            id: "powerline", name: "Powerline", note: "agnoster-style touching arrow segments",
            bars: [{
                position: "top", size: 24, bg: "bg",
                module: { padding: [8, 8], gap: [0, 0], fg: "bg", bold: true },
                start: [
                    { type: "launcher", bg: "accent", capEnd: "arrow", capEndBg: "card", padding: [10, 8] },
                    { type: "workspaces", style: "numbers", bg: "card", capEnd: "arrow", colors: { active: "accent", occupied: "text", empty: "faint" } },
                    { type: "title", fg: "dim", bold: false, padding: [10, 10] }
                ],
                end: [
                    { type: "media", fg: "dim", bold: false, format: "  {track}" },
                    { type: "cpu", format: "\u{f0ee0} {usage}%", bg: "card", fg: "text", capStart: "arrow",
                      states: { warning: 60, critical: 90 }, when: { warning: { fg: "warn" }, critical: { fg: "danger" } } },
                    { type: "ram", format: "\u{f035b} {percent}%", bg: "accent2", capStart: "arrow", capStartBg: "card" },
                    { type: "volume", format: "{icon} {volume}%", bg: "accent", capStart: "arrow", capStartBg: "accent2",
                      when: { muted: { format: "\u{f075f} muted" } } },
                    { type: "clock", format: "\u{f0150} {time}", formatAlt: "\u{f00ed} {date}", click: "alt", bg: "text", capStart: "arrow", capStartBg: "accent" },
                    { type: "tray", bg: "text", padding: [4, 8] },
                    { type: "power", bg: "danger", capStart: "arrow", capStartBg: "text", padding: [8, 10] }
                ]
            }]
        },
        {
            id: "slants", name: "Slants", note: "parallelogram islands, two-tone",
            bars: [{
                position: "top", size: 28, padding: 6,
                module: { padding: [6, 6], gap: [0, 0], bold: true },
                group: { bg: "bg/0.85", capStart: "slant", capEnd: "slant-back", inset: [4, 2], gap: [2, 2], padding: [2, 2] },
                start: [{ type: "group", modules: [{ type: "workspaces", style: "roman", colors: { active: "accent2" } }] },
                        { type: "group", bg: "accent", modules: [{ type: "title", fg: "bg", maxLength: 40 }] }],
                center: [{ type: "group", bg: "accent2", modules: [{ type: "clock", fg: "bg", timeFormat: "HH:mm", format: "{time}", formatAlt: "{date}", click: "alt" }] }],
                end: [{ type: "group", modules: [{ type: "cpu", format: "cpu {usage}%" }, { type: "ram", format: "mem {percent}%" }, { type: "temp" }] },
                      { type: "group", bg: "accent", modules: [{ type: "volume", fg: "bg", format: "vol {volume}%" }, { type: "tray" }] }]
            }]
        },
        {
            id: "dwm", name: "dwm", note: "flat suckless strip: tags, title, status text",
            bars: [{
                position: "top", size: 20, bg: "bg",
                module: { padding: [6, 6], gap: [0, 0] },
                start: [{ type: "workspaces", style: "dwl", padding: [0, 0] }, { type: "text", text: "[]=", fg: "dim", padding: [8, 8] },
                        { type: "title", bg: "accent", fg: "bg", padding: [10, 10], maxLength: 80 }],
                end: [{ type: "cpu", format: "cpu {usage}%" }, { type: "sep", format: "|" }, { type: "ram", format: "mem {percent}%" },
                      { type: "sep", format: "|" }, { type: "volume", format: "vol {volume}%", when: { muted: { format: "vol muted" } } },
                      { type: "sep", format: "|" }, { type: "clock", timeFormat: "ddd d MMM HH:mm" }, { type: "tray", padding: [6, 4] }]
            }]
        },
        {
            id: "polybar", name: "Underlines", note: "polybar-like: clear bar, a coloured underline per module",
            bars: [{
                position: "top", size: 30, bg: "bg/0.85",
                module: { padding: [12, 12], gap: [0, 0] },
                start: [{ type: "workspaces", style: "numbers", padding: [10, 10] }, { type: "sep" }, { type: "title", fg: "dim" }],
                center: [{ type: "clock", timeFormat: "HH:mm", line: { color: "accent", width: 2 }, bold: true }],
                end: [{ type: "cpu", format: "\u{f0ee0} {usage}%", line: { color: "accent", width: 2 } },
                      { type: "ram", format: "\u{f035b} {percent}%", line: { color: "accent2", width: 2 } },
                      { type: "temp", format: "\u{f050f} {temp}°C", line: { color: "warn", width: 2 }, states: { critical: 80 }, when: { critical: { fg: "danger" } } },
                      { type: "volume", format: "{icon} {volume}%", line: { color: "ok", width: 2 } },
                      { type: "network", format: "{icon} {ifname}", line: { color: "accent", width: 2 } },
                      { type: "tray" }]
            }]
        },
        {
            id: "macos", name: "Menu bar", note: "macOS-like: translucent strip, title left, status + date right",
            bars: [{
                position: "top", size: 26, bg: "bg/0.55", fontSize: 12,
                module: { padding: [9, 9], gap: [0, 0], radius: 5, inset: [3, 3], hoverBg: "text/0.12" },
                start: [{ type: "launcher", text: "", format: "", fontSize: "+3", padding: [14, 10] },
                        { type: "title", bold: true, maxLength: 50 }],
                end: [{ type: "tray", iconSize: 15 }, { type: "battery" }, { type: "network" }, { type: "volume", format: "{icon}" },
                      { type: "notifications" }, { type: "clock", timeFormat: "ddd d MMM  HH:mm" }]
            }]
        },
        {
            id: "gnome", name: "GNOME", note: "black top bar, Activities, centred clock, one status pill",
            bars: [{
                position: "top", size: 30, bg: "#000000", fontSize: 12, fg: "#ffffff",
                module: { padding: [12, 12], gap: [0, 0], radius: 15, inset: [3, 3], hoverBg: "#ffffff/0.15", bold: true },
                start: [{ type: "text", text: "Activities", click: "launcher" }, { type: "workspaces", style: "dots", hoverBg: "" }],
                center: [{ type: "clock", timeFormat: "ddd d MMM  HH:mm" }],
                end: [{ type: "tray", hoverBg: "" },
                      { type: "group", radius: 15, inset: [3, 3], modules: [
                          { type: "network", padding: [6, 4], hoverBg: "" }, { type: "volume", format: "{icon}", padding: [4, 4], hoverBg: "" },
                          { type: "battery", padding: [4, 4], hoverBg: "" }, { type: "power", padding: [4, 10], hoverBg: "" }],
                        click: "panel" }]
            }]
        },
        {
            id: "pills", name: "Floating pills", note: "every module its own pill, spaced out",
            bars: [{
                position: "top", size: 36, padding: 8, spacing: 6,
                module: { bg: "bg/0.8", radius: 13, inset: [6, 4], padding: [12, 12], gap: [3, 3], border: "text/0.08", borderWidth: 1 },
                start: [{ type: "launcher", fg: "accent" }, { type: "workspaces", style: "pills", padding: [8, 8] }, { type: "title", maxLength: 40 }],
                center: [{ type: "clock", bold: true, format: "{time}", formatAlt: "{date}", click: "alt" }, { type: "media", width: 24 }],
                end: [{ type: "cpu", format: "\u{f0ee0} {usage}%" }, { type: "ram", format: "\u{f035b} {percent}%" }, { type: "volume" },
                      { type: "network" }, { type: "tray" }, { type: "power", fg: "danger" }]
            }]
        },
        {
            id: "text", name: "Text only", note: "no backgrounds, lowercase words, dots between",
            bars: [{
                position: "top", size: 24, fg: "dim",
                module: { padding: [6, 6], gap: [0, 0] },
                start: [{ type: "workspaces", style: "numbers", padding: [10, 6] }, { type: "sep", format: "·" }, { type: "title", fg: "text" }],
                end: [{ type: "media", format: "{title}", width: 30 }, { type: "sep", format: "·" },
                      { type: "cpu", format: "cpu {usage}" }, { type: "ram", format: "mem {percent}" }, { type: "volume", format: "vol {volume}" },
                      { type: "sep", format: "·" }, { type: "clock", fg: "text", timeFormat: "HH:mm" }, { type: "tray", padding: [8, 10] }]
            }]
        },
        {
            id: "sidebar", name: "Sidebar", note: "vertical bar on the left: pills, turned clock, stacked stats",
            bars: [{
                position: "left", size: 40, bg: "bg/0.9", padding: 8, line: { pos: "right", width: 1, color: "accent/0.35" },
                module: { padding: [6, 6], gap: [2, 2] },
                start: [{ type: "launcher", fg: "accent", fontSize: "+6" },
                        { type: "group", bg: "card", radius: 10, inset: [5, 5], gap: [6, 0], modules: [{ type: "workspaces", style: "pills", padding: [6, 6] }] }],
                center: [{ type: "clock", timeFormat: "HH:mm", rotate: true, bold: true, fontSize: "+2" }],
                end: [{ type: "cpu", format: "\n{usage}", states: { warning: 60 }, when: { warning: { fg: "warn" } } },
                      { type: "ram", format: "\n{percent}" }, { type: "sep", format: "──" },
                      { type: "volume", format: "{icon}\n{volume}" }, { type: "tray", iconSize: 16 }, { type: "power", fg: "danger", fontSize: "+4" }]
            }]
        },
        {
            id: "dock", name: "Dock", note: "slim top bar + a floating dock of open apps",
            bars: [
                { position: "top", size: 24, bg: "bg/0.35", module: { padding: [8, 8], gap: [0, 0] },
                  start: [{ type: "workspaces", style: "dots", padding: [10, 6] }, { type: "title", fg: "dim" }],
                  center: [{ type: "clock", timeFormat: "ddd d  HH:mm", bold: true }],
                  end: [{ type: "tray" }, { type: "network" }, { type: "volume", format: "{icon}  {volume}%" }, { type: "power" }] },
                { position: "bottom", size: 56, length: "auto", margin: [8, 0, 0], bg: "bg/0.6", radius: 18, border: "text/0.1", borderWidth: 1,
                  padding: 8, exclusive: false, layer: "top",
                  center: [{ type: "launcher", logo: true, fontSize: "+14", padding: [10, 10] },
                           { type: "sep", fontSize: "+12" },
                           { type: "taskbar", iconSize: 34, spacing: 6, padding: [4, 4] }] }
            ]
        },
        {
            id: "taskbar", name: "Taskbar", note: "bottom bar with a start button, window list and tray",
            bars: [{
                position: "bottom", size: 40, bg: "bg/0.95", line: { pos: "top", width: 1, color: "text/0.08" },
                module: { padding: [8, 8], gap: [0, 0] },
                start: [{ type: "launcher", fontSize: "+8", fg: "accent", padding: [14, 12], hoverBg: "text/0.08" },
                        { type: "taskbar", titles: true, titleWidth: 22, iconSize: 18 }],
                end: [{ type: "tray" }, { type: "network" }, { type: "volume", format: "{icon}" }, { type: "battery" },
                      { type: "clock", format: "{time}\n{date}", dateFormat: "d/M/yyyy", fontSize: "-1", padding: [12, 12] },
                      { type: "notifications", padding: [8, 12] }]
            }]
        },
        {
            id: "neon", name: "Neon frames", note: "outlined accent boxes on a dark strip",
            bars: [{
                position: "top", size: 30, bg: "bg/0.92", padding: 6, spacing: 6, fg: "accent",
                group: { border: "accent", borderWidth: 1, radius: 4, inset: [5, 5], padding: [2, 2], gap: [0, 0] },
                module: { padding: [8, 8], gap: [0, 0], bold: true },
                start: [{ type: "group", modules: [{ type: "workspaces", style: "kanji", colors: { active: "accent", occupied: "accent2", empty: "faint" } }] },
                        { type: "group", border: "accent2", modules: [{ type: "title", fg: "accent2", maxLength: 40 }] }],
                center: [{ type: "group", modules: [{ type: "clock", timeFormat: "HH:mm:ss" }] }],
                end: [{ type: "group", modules: [{ type: "cpu", format: "CPU {usage:3}%" }, { type: "ram", format: "RAM {percent:3}%" }, { type: "gpu", format: "GPU {usage:3}%" }] },
                      { type: "group", border: "accent2", modules: [{ type: "volume", fg: "accent2" }, { type: "tray" }] }]
            }]
        }
    ]
}
