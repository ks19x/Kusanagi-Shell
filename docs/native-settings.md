# Native Settings: framework and page guide

Kusanagi's Settings window is part of `kusanagi-shell`.
The window, its control kit and the pages live in `native/src/shell/kusanagi/settings/`:

| File | What |
|---|---|
| `settings_app.h/.cpp` | `kusanagi::SettingsApp`, the xdg toplevel: sidebar, search, page header, page host and scroll, Escape, IPC |
| `sp_pages.h/.cpp` | the page table (id, group, name, icon, description, search keys, builder) and the placeholder for a page without a builder |
| `sp_kit.h` | the control kit API, everything a page needs. Read this header, it is the reference |
| `sp_data.cpp` | settings.json access: `value / get / set / write / reset / bind`, the defaults, the 250 ms save timer |
| `sp_kit.cpp` | containers, text, SpGroup, SpFold, CpRow, CpSwitch, CpSlider, CpSegmented, CpStepper, CpIconButton, CpChip, CpField, swatches, colour pick, path field |
| `sp_style_picker.cpp` | SpStylePicker (all ten drawing kinds) |
| `sp_pickers.cpp` | SpFontPicker, SpAppPicker, `installedFonts()` |
| `sp_page_<id>.cpp` | one file per page |
| `bar_preview.h/.cpp` | a bar spec drawn by the bar's own module widgets (Settings > Bar, preset cards) |

All 19 pages are native: Presets, Appearance, Bar, Workspaces, Control panel, Wallpaper, Launcher & clipboard,
Lock & power, Login screen, Notifications & OSD, Game mode, Recording, and the SYSTEM pages Sound, Bluetooth,
Display, Network, Storage, Updates and About. The Bar page is split in two: `sp_page_bar.cpp` (tabs, templates,
dock, the classic options) and `sp_page_bar_editor.cpp` (the bar editor).

## Opening it

`kusanagi settings [page]` opens it with the native shell. Underneath is the IPC
`kusanagi-settings [toggle|open|page|hide] [page]`:

```
kusanagi-shell msg kusanagi-settings open appearance   # show (or bring forward) on a page
kusanagi-shell msg kusanagi-settings page sound        # same, page required
kusanagi-shell msg kusanagi-settings toggle            # close if open (on that page), else open
kusanagi-shell msg kusanagi-settings hide
```

It answers `open <page>` or `closed`. Without a page it reopens the last one (Presets the first time). The
generic `SettingsWindow::open` opens this window too.

The window keeps the app id `org.quickshell`, from when Kusanagi was a Quickshell config, on purpose:
existing window rules such as mango's `windowrule=isfloating:1,appid:^(...|org.quickshell|...)$` float it.
Closing it frees the whole scene; nothing stays in memory.

## The setup wizard

`setup_app.h/.cpp` is the first-run setup in its own window (1000x700, title "Kusanagi Setup", app id
`org.quickshell`). The steps are welcome, look, wallpaper, bar, lock & idle, extras, login screen and done.
Steps that are Settings pages are built from the page table by id (`presets`, `wallpaper`, `bar`, `lock`,
`login`). It opens by itself 2.5 s after the shell starts without a settings.json (the defaults are written out
first; the generic setup wizard never opens). `kusanagi setup` opens it, and so does the IPC:

```
kusanagi-shell msg kusanagi-setup [open|toggle|hide]   # open (default) starts at the first step; answers open <step> or closed
```

A second window that shows pages registers itself with `sp::addHost(owner, Host)` and `removeHost(owner)`, so
writes and `refresh()` keep it in sync too.

## Adding a page

1. Write `native/src/shell/kusanagi/settings/sp_page_<id>.cpp` with `void build<Name>(Column& page)`. The page
   Column already has `spacing: 22` and the 760 px max width.
2. Declare it in `sp_pages.h` and put it in the `build` slot of its row in `sp_pages.cpp`.
3. Add the .cpp to `native/meson.build` next to the other `src/shell/kusanagi/settings/*` files.

A whole page usually needs nothing but the kit:

```cpp
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"

namespace kusanagi::sp {
  void buildWorkspaces(Column& page) {
    auto* g = page.add<Group>("Behaviour");
    g->add<Row>("Always shown", "more appear while they have windows",
                std::make_unique<Stepper>(bind("workspaces.shown"), StepperOpts{.from = 1, .to = 9}));
    g->add<Row>("Glow on the active one", std::make_unique<Switch>(bind("workspaces.glow")));
    g->add<Row>("Active colour", std::make_unique<Segmented>(bind("workspaces.activeColor"),
        std::vector<Option>{{"Accent", "accent"}, {"Second accent", "accent2"}, {"Text", "text"}}, 300.0F));
    page.add<Chip>("Reset workspaces", 0xf0709)->onClick([]() { reset("workspaces"); });
  }
}
```

`add<T>(args...)` constructs a T inside any container (Column, Flow, HRow, Group, Fold) and returns `T*`, so
setters chain: `flow->add<Chip>("Pills")->onWhen(...)->onClick(...)`. A `Row` takes its control as a
`std::unique_ptr<Item>`.

To show an item only sometimes, use `->showIf([]() { return ...; })`. It is re-evaluated after every change, and
a hidden item takes no room and no spacing.

## Values: reading, writing, bindings

- `value("osd.timeout")` returns the `json` at that dotted path in settings.json, else the default
  (`defaults()` holds the defaults table), else null. `get<T>(path, fallback)` converts.
  `truthy(json)` is JS truthiness.
- `set("look.radius", 18)` writes one key; `write({{"look.animSpeed", 1.4}, {"look.bounce", 0.4}})` writes
  several at once. A `null` value removes the key. `reset("panel")` and `reset()` reset a section or everything.
- Writes go to memory at once (`kusanagi::settings()` and every control see them immediately) and to the file
  250 ms after the last one, so a slider drag is one file write and one live reload. The file is re-read before
  writing and only the written keys change. It is written atomically with sorted keys, a 4-space indent and
  whole-number reals as integers. Closing the window flushes.
- The shell's config watcher reloads settings.json, and the bar, panel, OSD and the rest apply it live. The
  window resyncs on that reload too, so hand edits show up, and rebuilds itself if `look.font` changed (every
  label carries its font).
- `bind("path", after)` gives a `Binding {get, set}` for a settings path; `after(value)` runs after each write
  (e.g. sending a test notification after picking a notification style). For values that don't live in
  settings.json, make the Binding yourself:

```cpp
Binding dnd{
    .get = []() -> json { return ipc("notification-dnd-status").starts_with("on"); },
    .set = [](const json& v) { (void)ipc(std::string("notification-dnd-set ") + (truthy(v) ? "on" : "off")); },
};
```

- After every write and every reload the window calls `syncTree()` on the page: bound controls re-read
  `binding.get()` and animate to it, and `showIf` predicates and `bindText` / `labelFrom` / `onWhen` lambdas are
  re-evaluated. For state outside settings.json (game mode, DND, /proc), update it and call `refresh()`.
- `ipc("line")` runs one of the shell's own IPC commands in-process (`osd-preview volume`,
  `notification-show {json}`, `gamemode toggle`, `panel-open launcher`). `spawn({argv...})` runs a command
  detached. `expandHome("~/x")` expands the home directory.
- `Poll(ms, fn)` (an invisible item) runs `fn` now and every `ms` while the page is open, e.g. the About page's
  memory readout and the Game mode page's state. Services with a single change callback (PipeWire, BlueZ,
  brightness) are followed the same way: keep a copy of the service state and `refresh()` when it differs
  (the Sound page polls every 150 ms).
- `run({argv...}, [](const std::string& out, int code){ ... })` runs the command off the UI thread and hands
  stdout to the callback on the UI thread. The page may be gone by then, so capture a `weak_ptr` to the page's
  state, never `this` (Storage's df, Network's nmcli, Display's monitors).
- `services()` is the shell's `ControlCenterServices`, the same set the control panel gets (`audio` for
  PipeWire, `bluetooth` and `bluetoothAgent`, `brightness`, `network`, `sysmon`, `platform` and more). It is set
  once at startup and any member may be null.

## Controls

All are `Item`s (sp_kit.h has the exact signatures).

| Control | Notes |
|---|---|
| `Column(spacing)` | children full width, top to bottom |
| `Flow(spacing = 6)` | children at their own width, wrapped |
| `HRow(spacing = 8)` | side by side, vertically centred |
| `Group(title, hint = {}, icon = 0)` | a settings card; `add<...>` goes into the card (spacing 14); `bindHint(fn)` for a hint that changes ("" hides it); `bindTitle(fn)` for a changing title (the Updates page's "3 updates waiting") |
| `Fold(title, hint, open)` | a card that folds; `bindHint(fn)`, `setOpen()`; body spacing 6 |
| `Row(label, hint, std::unique_ptr<Item>)` or `Row(label, control)` | a labelled control; height is max(34, control); `bindHint(fn)` for a live hint ("Idle: armed") |
| `Text(text, TextOpts{px, bold, color, wrap, elide, topPadding, letterSpacing, family})` | `bindText(fn)`, `bindColor(fn)`, `setText()` |
| `Heading(text, topPadding = 4)` | 11 px bold dim headings (the "Design" headings) |
| `Switch(Binding)` | writes true or false; `setFixedWidth(w)` stretches the track |
| `Slider(Binding, SliderOpts{icon, label, toUnit, fromUnit, text, step, height = 30})` | `toUnit(json)` gives 0..1, `fromUnit(0..1)` gives json; drag, click, wheel |
| `SliderOpts{..., iconFn, labelFn, muted, onIconClicked}` | icon or label from state; for values outside settings.json (volumes, brightness) use a hand-made `Binding`; `muted()` greys the fill (180 ms); with `onIconClicked` the icon circle is its own button |
| `Repeater(keysFn, makeFn(key), spacing = 14)` | a changing list: rebuilt when the keys change; items read their entry's live state in `sync()`; takes no room while empty |
| `item->enabledIf(fn)` | dimmed and ignores the pointer while false (Display's gap sliders) |
| `Segmented(Binding, std::vector<Option>{{label, value, icon}}, width, fontPx = 11)` | one-of-N picker; value is any json (strings, ints) |
| `Stepper(Binding, StepperOpts{from, to, step, suffix, scale})` | shows value / scale (e.g. ms as s with scale 1000) |
| `Chip(label, icon = 0, fontFamily = {})` | `->onClick(fn)->onWhen(fn)->labelFrom(fn)` |
| `choiceChip(label, path, value, fontFamily = {})` | a chip that sets a value (styles, fonts); lit while `path == value` |
| `Field(std::optional<Binding>, FieldOpts{width, placeholder, icon, applyOnEdit, convert, onEdited, onAccepted})` | shows the value (not while typing); writes on Enter, or on every edit; `convert(text)` returns json, or `nullopt` to refuse |
| `textField(path, width, placeholder = {}, allowEmpty = false)` | text field for one path; trims; refuses empty unless allowed |
| `IconButton(icon, onClick, size = 34, iconPx = 16)` | `setFilled()`, `enabledWhen(fn)` (0.3 opacity when off) |
| `Swatch(colorFn, onFn, onClick, size = 28, ring = 3)` | accent swatch circle; ring in the text colour, grows on hover |
| `ColorPick(Binding)` | the bar editor's colour row (dot, field, token dots); tokens accent accent2 text dim faint bg card danger warn ok transparent; "a, b" is a gradient array |
| `PathField(path, width, placeholder = {})` | folder or file path: field plus a button that opens it (xdg-open) |
| `StylePicker(kind, std::vector<Option>{{label, value, 0, note}}, Binding, cardW = 168)` | kinds launcher panel notifications osd corner edge tiles panelLook lock power |
| `FontPicker(Binding)` | fonts via fc-list off the UI thread; only visible rows exist |
| `AppPicker(picked, exclude = {}, max = 8)` | desktop entries, icons from the icon theme |
| `BarPreview(barFn, heightFn, BarPreviewOpts{snapshot, pickable, screenW, screenH, desktop, fixedScale})` | `->selKey(fn)->onPicked(fn)->onMoved(fn)`; uses the real bar widgets (`WidgetFactory::current()`) laid out like the bar; with snapshot it lays out once per spec, look and width |
| a small `Item` subclass in your page file | anything custom; see below |

There is no dropdown, since no page needs one; use `Segmented` or chips.

Colours: `textA(a)`, `dim()`, `accent(a)`, `accent2(a)`, `bgPanel(a)`, `ok(a)` and `danger(a)` are the theme
roles as `ColorSpec`s that follow the live palette. `resolved(spec)` gives a fixed `Color`, and
`darker(color, f)` darkens one. `makeText(s, px, bold, color, family)` and `makeIcon(codepoint, px, color)`
make raw labels in the look font or the Nerd Font (with `LabelBaselineMode::FontLine`), and
`setSpacedText(label, s, px)` sets letter spacing.

## Writing a custom Item

```cpp
class MyThing : public Item {
public:
  MyThing() {                         // build all child nodes once
    m_bg = static_cast<Box*>(addChild(ui::box({})));
    m_label = static_cast<Label*>(addChild(makeText("", 12.0F)));
    setOnClick([](const PointerData&) { set("x.y", true); });
    sync();
  }
  void sync() override {              // re-read values; call requestLayout() if a size may change
    if (m_label->setText(get<std::string>("x.name", ""))) requestLayout();
  }
  float place(Renderer& r, float width) override {   // lay out by hand
    width = widthFor(width);                          // honours setFixedWidth()
    m_label->measure(r);
    m_label->setPosition(12.0F, 0.0F);
    m_bg->setSize(width, 40.0F);
    setSize(width, 40.0F);
    return 40.0F;
  }
};
```

- `place()` runs on every layout pass: position and size things, measure labels. Don't create nodes there
  unless something changed (keep a dirty flag, as `WsPreview` in sp_page_workspaces.cpp does).
- Natural-width items set their own width in `place()` (Chip, Stepper); fill-width items use the offered width.
- Items in a row that the old layout didn't centre sit at the row's top (e.g. the sidebar names, the About
  page's key and value lines). Check the original before centring anything.
- Hover and press: `setOnEnter/Leave/Click/Press`, `hovered()`, `tweenColor(node, from, to, ms, apply)`,
  `tweenScale(node, to, ms, overshoot)` (OutBack scaled by look.bounce), and `outCubic / outQuint / outBack`.
  Durations are base values that the shell scales by look.animSpeed. Use `AnimationManager::animateTimer` for
  a deliberately fixed duration, e.g. the motion preset demo.
- Animations need frames. The window wakes its frame loop after input and after every layout pass, so anything
  started from a click or a `sync()` just works. An animation started from a timer must call `requestLayout()`.
- Don't put `setClipChildren(true)` on a `Box` that has a gradient style (its children would draw with it);
  clip with a plain `Node` above a background `Box` instead.
- Nested scrollables: give an `InputArea` a `setOnAxisHandler` that returns true when it used the wheel
  (FontPicker). Unhandled wheel events go up to the page scroller.

## Testing

```
native/tools/kdev/kb                                    # build
native/tools/kdev/kt <slot> up                          # private headless mango + the dev shell
native/tools/kdev/kt <slot> msg kusanagi-settings open <page>
native/tools/kdev/kt <slot> shot /tmp/n.png             # look at it with the Read tool
native/tools/kdev/kin <slot> click X Y wait 500 type text key Return scroll X Y down 3
```

- To see whole pages without scrolling, put a tall output in your slot's mango config before `up`, e.g.
  `echo 'monitorrule=name:HEADLESS-1,width:1920,height:3000,refresh:60' > /tmp/kusanagi-dev/s<slot>/test.conf`.
  The window then shows the full page.
- Writes: watch `/tmp/kusanagi-dev/home<slot>/.config/kusanagi/settings.json` and the shell log
  (`kt <slot> log | grep "config changed"` shows one reload per burst of changes).
- Never click "Restart Kusanagi", "Clear history" or other actions that reach outside the test slot.

## Known limitations

- The test notification goes through `notification-show`, so it also lands in the history.
- Search fields don't select all on focus, and the caret uses the accent colour (the shared Input control).
- Sound: devices are labelled by node.nick, then description, then name, and listed in the PipeWire
  service's order rather than registry order, so two inputs can swap places.
- Display: brightness comes from the shell's brightness service, which has no "detecting" signal, so "Look
  again" shows its busy label for 4 s. A DDC monitor's name is its Wayland output model rather than ddcutil's
  model. The gaps override is applied by `kusanagi::wm` (wm_layout.h) on startup and on every windows.* change,
  but not after a Hyprland `configreloaded` event.
- Bluetooth: BlueZ devices have no "pairing" state natively, so a row never shows a pairing state or Cancel.
  A test shell never scans with the real adapter.
- Settings > Bar: the preview draws the bar's native module widgets, so it shows what the native bar shows
  (e.g. the tray and taskbar widgets: no window titles in the Taskbar template's preview, the native network
  glyph). The raw JSON box lists keys sorted rather than in insertion order, in the look font rather than
  JetBrainsMono.
