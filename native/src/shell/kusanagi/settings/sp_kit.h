#pragma once

// Control kit for the Settings pages (groups, rows, switches, sliders, pickers and so on). See
// docs/native-settings.md for how to build a page with it.
//
// A page is a tree of sp::Items under a Column. The window calls place(renderer, width) on every layout
// pass, where each Item sizes itself and returns its height, and syncTree() after every settings write or
// reload so bound controls re-read their values.
//
// Writes land in memory at once and reach settings.json 250 ms after the last change, so a slider drag
// is one write and one live reload of the shell.

#include "render/scene/input_area.h"
#include "ui/palette.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class Box;
class Image;
class Input;
class Label;
class Renderer;
class Timer;
struct ControlCenterServices;

namespace kusanagi::sp {

  using json = nlohmann::json;

  // Settings

  [[nodiscard]] const json& defaults();
  // Value at a dotted path ("look.radius", "bar.modules.cpu"), else the default, else null.
  [[nodiscard]] json value(std::string_view path);
  template <typename T> [[nodiscard]] T get(std::string_view path, T fallback) {
    const json v = value(path);
    if (v.is_null()) return fallback;
    try {
      return v.get<T>();
    } catch (...) {
      return fallback;
    }
  }
  // JavaScript-style truthiness: false, 0, "" and null are false.
  [[nodiscard]] bool truthy(const json& v);
  // Writes dotted paths; a null value removes the key. Other keys and the file format (sorted keys,
  // 4-space indent) are left as they are.
  bool write(std::vector<std::pair<std::string, json>> changes);
  inline bool set(std::string path, json v) { return write({{std::move(path), std::move(v)}}); }
  // Resets a section, or everything when empty, to the defaults.
  bool reset(std::string_view section = {});
  // Writes anything still waiting for the save timer (e.g. when the window closes).
  void flush();
  // After a settings.json reload, re-applies writes still waiting for the save timer.
  void reapplyPending();

  // Host window

  struct Host {
    std::function<void()> changed;                       // After every write, to sync the page.
    std::function<void()> requestLayout;                 // Something changed size.
    std::function<std::string(const std::string&)> ipc; // The shell's own IPC, in-process.
  };
  void setHost(Host host);
  // Another window that shows pages (the setup window). It gets changed and requestLayout too, and
  // serves ipc while the Settings window is closed.
  void addHost(const void* owner, Host host);
  void removeHost(const void* owner);
  void requestLayout();
  // The same services the control center gets (PipeWire, BlueZ, brightness, NetworkManager, ...), set once
  // at startup. Members are null for services that aren't running.
  void setServices(const ::ControlCenterServices& services);
  [[nodiscard]] const ::ControlCenterServices& services();
  // Same as `kusanagi-shell msg <line>`, without going through the socket.
  std::string ipc(const std::string& line);
  // Starts a detached command.
  void spawn(std::vector<std::string> argv);
  // Runs a command off the UI thread, then calls `done(stdout, exit code)` on the UI thread. The page may
  // be gone by then, so capture shared state (shared_ptr / weak_ptr), never `this`.
  void run(std::vector<std::string> argv, std::function<void(const std::string& out, int code)> done);
  // Expands a leading ~ to $HOME.
  [[nodiscard]] std::string expandHome(const std::string& path);
  // Syncs the page now, for state that lives outside settings.json (game mode, DND, system info).
  void refresh();
  // Installed font families from fc-list, loaded once off the UI thread. Null until known; the page syncs
  // when they arrive.
  [[nodiscard]] const std::vector<std::string>* installedFonts();

  // A two-way link between a control and a value: get() is re-read on every sync, set() writes.
  struct Binding {
    std::function<json()> get;
    std::function<void(const json&)> set;
  };
  // Binds a settings path. `after` runs after each write (e.g. to send a test notification).
  [[nodiscard]] Binding bind(std::string path, std::function<void(const json&)> after = {});

  // Colours and text

  [[nodiscard]] inline ColorSpec textA(float a = 1.0F) { return colorSpecFromRole(ColorRole::OnSurface, a); }
  [[nodiscard]] inline ColorSpec dim() { return colorSpecFromRole(ColorRole::OnSurfaceVariant); }
  [[nodiscard]] inline ColorSpec accent(float a = 1.0F) { return colorSpecFromRole(ColorRole::Primary, a); }
  [[nodiscard]] inline ColorSpec accent2(float a = 1.0F) { return colorSpecFromRole(ColorRole::Secondary, a); }
  [[nodiscard]] inline ColorSpec bgPanel(float a = 1.0F) { return colorSpecFromRole(ColorRole::Surface, a); }
  [[nodiscard]] inline ColorSpec ok(float a = 1.0F) { return colorSpecFromRole(ColorRole::Tertiary, a); }
  [[nodiscard]] inline ColorSpec danger(float a = 1.0F) { return colorSpecFromRole(ColorRole::Error, a); }
  [[nodiscard]] Color resolved(const ColorSpec& c);
  // Divides the HSV value by `factor`.
  [[nodiscard]] Color darker(const Color& c, float factor);

  [[nodiscard]] std::unique_ptr<Label> makeText(const std::string& s, float px, bool bold = false,
                                                const ColorSpec& color = textA(), const std::string& family = {});
  [[nodiscard]] std::string utf8Of(char32_t cp);
  [[nodiscard]] std::unique_ptr<Label> makeIcon(char32_t cp, float px, const ColorSpec& color = textA());
  // Sets text with extra letter spacing in px.
  void setSpacedText(Label& label, const std::string& s, float letterSpacingPx);

  // Item

  class Item : public InputArea {
  public:
    Item();
    // Lay out for `width` px (the parent's content width): set the own size, return the height.
    // Natural-width items (chips, steppers) ignore it; fixed-width ones use fixedWidth().
    virtual float place(Renderer& renderer, float width) = 0;
    // Re-reads bound values after a write or reload and animates to them.
    virtual void sync() {}
    // Shown only while `pred` is true, re-checked on every sync.
    Item* showIf(std::function<bool()> pred);
    [[nodiscard]] bool shown() const noexcept { return m_shown; }
    // While `pred` is false the item is dimmed and ignores the pointer. Re-checked on every sync.
    Item* enabledIf(std::function<bool()> pred);
    Item* setFixedWidth(float w) {
      m_fixedW = w;
      return this;
    }
    [[nodiscard]] float fixedWidth() const noexcept { return m_fixedW; }
    // Syncs this and every Item below it, re-evaluating showIf().
    void syncTree();

  protected:
    // The fixed width if set, else the one offered.
    [[nodiscard]] float widthFor(float offered) const { return m_fixedW > 0.0F ? m_fixedW : offered; }

  private:
    std::function<bool()> m_showIf;
    std::function<bool()> m_enabledIf;
    bool m_shown = true;
    bool m_enabled = true;
    float m_fixedW = 0.0F;
  };

  // Base for items that hold children: Column, Flow, HRow, Group, Fold.
  class Container : public Item {
  public:
    template <typename T, typename... A> T* add(A&&... args) {
      return static_cast<T*>(addItem(std::make_unique<T>(std::forward<A>(args)...)));
    }
    virtual Item* addItem(std::unique_ptr<Item> item);
    [[nodiscard]] std::vector<Item*> items() const;
  };

  // Shown children top to bottom, `spacing` apart, all as wide as the column.
  class Column : public Container {
  public:
    explicit Column(float spacing = 0.0F) : m_spacing(spacing) {}
    float place(Renderer& renderer, float width) override;

  private:
    float m_spacing;
  };

  // Items for a changing list (audio devices, disks, Bluetooth devices). `keys()` is re-read on every sync
  // and the items are rebuilt with `make(key)` when the keys change. They stack like a Column; pass the
  // parent's spacing (14 in a Group). Takes no room while empty. Items read their live state in sync().
  class Repeater : public Column {
  public:
    Repeater(std::function<std::vector<std::string>()> keys,
             std::function<std::unique_ptr<Item>(const std::string& key)> make, float spacing = 14.0F);
    void sync() override;

  private:
    std::function<std::vector<std::string>()> m_keysFn;
    std::function<std::unique_ptr<Item>(const std::string&)> m_make;
    std::vector<std::string> m_keys;
    bool m_built = false;
  };

  // Children at their own width, wrapped into lines `spacing` apart.
  class Flow : public Container {
  public:
    explicit Flow(float spacing = 6.0F) : m_spacing(spacing) {}
    float place(Renderer& renderer, float width) override;

  private:
    float m_spacing;
  };

  // Children side by side at their own width, vertically centred.
  class HRow : public Container {
  public:
    explicit HRow(float spacing = 8.0F) : m_spacing(spacing) {}
    float place(Renderer& renderer, float width) override;

  private:
    float m_spacing;
  };

  // Runs `fn` every `ms` while the page is open (and once right away). Takes no room. `fn` updates
  // whatever the page's bound texts read, then calls refresh().
  class Poll : public Item {
  public:
    Poll(int ms, std::function<void()> fn);
    ~Poll() override;
    float place(Renderer& renderer, float width) override;

  private:
    std::unique_ptr<::Timer> m_timer;
    std::function<void()> m_fn;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
  };

  // Empty spacer of a fixed height.
  class Gap : public Item {
  public:
    explicit Gap(float h) : m_h(h) {}
    float place(Renderer& renderer, float width) override;

  private:
    float m_h;
  };

  // Text

  // One label in the look font. `wrap` word-wraps at the offered width; `elide` cuts off at the right.
  struct TextOpts {
    float px = 12.0F;
    bool bold = false;
    ColorSpec color = textA();
    bool wrap = false;
    bool elide = false;
    float topPadding = 0.0F;
    float letterSpacing = 0.0F;
    std::string family; // Empty means look.font.
  };
  class Text : public Item {
  public:
    explicit Text(std::string text, TextOpts opts = {});
    // Text that is re-read on every sync.
    Text* bindText(std::function<std::string()> fn);
    Text* bindColor(std::function<ColorSpec()> fn);
    void setText(const std::string& s);
    void sync() override;
    float place(Renderer& renderer, float width) override;
    [[nodiscard]] Label* label() const noexcept { return m_label; }

  private:
    TextOpts m_opts;
    std::string m_text;
    Label* m_label = nullptr;
    std::function<std::string()> m_bind;
    std::function<ColorSpec()> m_bindColor;
  };

  // Small dim bold heading over a picker ("Design", "Where they pop up").
  class Heading : public Text {
  public:
    explicit Heading(std::string text, float topPadding = 4.0F);
  };

  // Sections

  // A title, with optional icon tile and hint line, over a soft card holding the rows.
  class Group : public Container {
  public:
    Group(std::string title, std::string hint = {}, char32_t icon = 0);
    Item* addItem(std::unique_ptr<Item> item) override;
    float place(Renderer& renderer, float width) override;
    [[nodiscard]] Column* body() const noexcept { return m_body; }
    // Hint re-read on every sync; "" hides it.
    Group* bindHint(std::function<std::string()> fn);
    // Title re-read on every sync (e.g. "3 updates waiting").
    Group* bindTitle(std::function<std::string()> fn);
    void sync() override;

  private:
    std::function<std::string()> m_hintFn;
    std::function<std::string()> m_titleFn;
    std::string m_title;
    std::string m_hint;
    Box* m_iconTile = nullptr;
    Label* m_icon = nullptr;
    Label* m_titleLabel = nullptr;
    Label* m_hintLabel = nullptr;
    Box* m_card = nullptr;
    Column* m_body = nullptr;
  };

  // A section with a chevron header that folds open and closed.
  class Fold : public Container {
  public:
    Fold(std::string title, std::string hint = {}, bool open = false);
    Item* addItem(std::unique_ptr<Item> item) override;
    float place(Renderer& renderer, float width) override;
    void setOpen(bool open);
    [[nodiscard]] bool open() const noexcept { return m_open; }
    Fold* bindHint(std::function<std::string()> fn);
    void sync() override;

  private:
    bool m_open;
    float m_shownH = 0.0F;  // Animated body height.
    float m_bodyH = 0.0F;
    bool m_animating = false;
    std::function<std::string()> m_hintFn;
    InputArea* m_header = nullptr;
    Box* m_headerBg = nullptr;
    Label* m_chevron = nullptr;
    Label* m_titleLabel = nullptr;
    Label* m_hintLabel = nullptr;
    Node* m_clip = nullptr;
    Column* m_body = nullptr;
  };

  // Label on the left, control on the right, optional dim hint under the label.
  class Row : public Item {
  public:
    Row(std::string label, std::string hint, std::unique_ptr<Item> control);
    Row(std::string label, std::unique_ptr<Item> control) : Row(std::move(label), {}, std::move(control)) {}
    float place(Renderer& renderer, float width) override;
    [[nodiscard]] Item* control() const noexcept { return m_control; }
    // Hint re-read on every sync; "" hides it.
    Row* bindHint(std::function<std::string()> fn);
    void sync() override;

  private:
    std::function<std::string()> m_hintFn;
    Label* m_label = nullptr;
    Label* m_hint = nullptr;
    Item* m_control = nullptr;
  };

  // Controls

  // Toggle switch. A truthy bound value is on; toggling writes true or false.
  class Switch : public Item {
  public:
    // setFixedWidth() stretches the track and the knob travels to its far end.
    explicit Switch(Binding binding);
    float place(Renderer& renderer, float width) override;
    void sync() override;

  private:
    void apply(bool on, bool animate);
    Binding m_binding;
    bool m_on = false;
    bool m_first = true;
    Box* m_bg = nullptr;
    Box* m_knob = nullptr;
    ColorSpec m_bgColor;
    ColorSpec m_knobColor;
  };

  // Pill slider with an icon on the fill, a bold label and the value at the right. Drag, click or scroll.
  struct SliderOpts {
    char32_t icon = 0;
    std::string label;
    std::function<float(const json&)> toUnit;     // Bound value to 0..1.
    std::function<json(float)> fromUnit;          // 0..1 to the value to write.
    std::function<std::string(const json&)> text; // Value text ("16px", "95%").
    float step = 0.05F;                           // Per wheel notch, in 0..1.
    float height = 30.0F;
    // For values outside settings.json (volumes, brightness):
    std::function<char32_t()> iconFn;             // Icon re-read on every sync.
    std::function<std::string()> labelFn;         // Label re-read on every sync.
    std::function<bool()> muted;                  // Greys out the fill.
    std::function<void()> onIconClicked;          // Makes the icon's circle a separate button.
  };
  class Slider : public Item {
  public:
    Slider(Binding binding, SliderOpts opts);
    float place(Renderer& renderer, float width) override;
    void sync() override;

  private:
    void moveTo(float unit);
    void applyFill(float shown);
    void restyleTexts();
    [[nodiscard]] float at(float localX) const;
    Binding m_binding;
    SliderOpts m_opts;
    float m_value = 0.0F;
    float m_shown = -1.0F;
    bool m_dragging = false;
    bool m_muted = false;
    Box* m_track = nullptr;
    Box* m_fill = nullptr;
    Label* m_icon = nullptr;
    InputArea* m_iconArea = nullptr; // Only with onIconClicked.
    Label* m_label = nullptr;
    Label* m_valueText = nullptr;
  };

  // One-of-N choice with a sliding highlight, at a fixed `width`.
  struct Option {
    std::string label;
    json value;
    char32_t icon = 0;
    std::string note; // Second line on style picker cards.
  };
  class Segmented : public Item {
  public:
    Segmented(Binding binding, std::vector<Option> options, float width, float fontPx = 11.0F);
    float place(Renderer& renderer, float width) override;
    void sync() override;

  private:
    struct Cell {
      InputArea* area = nullptr;
      Label* icon = nullptr;
      Label* label = nullptr;
      ColorSpec color;
    };
    [[nodiscard]] int indexOf(const json& v) const;
    void restyle(bool animate);
    Binding m_binding;
    std::vector<Option> m_options;
    float m_fontPx;
    int m_index = 0;
    bool m_first = true;
    float m_highlightX = 3.0F;
    Box* m_bg = nullptr;
    Box* m_highlight = nullptr;
    std::vector<Cell> m_cells;
  };

  // Minus / value / plus for small integers. Shows value / scale (e.g. an OSD timeout in 100 ms steps).
  struct StepperOpts {
    int from = 0;
    int to = 10;
    int step = 1;
    std::string suffix;
    double scale = 1.0;
  };
  class IconButton;
  class Stepper : public Item {
  public:
    Stepper(Binding binding, StepperOpts opts);
    float place(Renderer& renderer, float width) override;
    void sync() override;

  private:
    [[nodiscard]] int current() const;
    Binding m_binding;
    StepperOpts m_opts;
    IconButton* m_minus = nullptr;
    IconButton* m_plus = nullptr;
    Label* m_text = nullptr;
  };

  // Round icon button with a hover tint and a springy press.
  class IconButton : public Item {
  public:
    IconButton(char32_t icon, std::function<void()> onClick, float size = 34.0F, float iconPx = 16.0F);
    IconButton* setFilled(bool filled);
    IconButton* enabledWhen(std::function<bool()> fn); // Disabled: faded and not clickable.
    float place(Renderer& renderer, float width) override;
    void sync() override;

  private:
    void restyle(bool animate);
    float m_size;
    bool m_filled = false;
    bool m_enabled = true;
    std::function<bool()> m_enabledFn;
    std::function<void()> m_onClick;
    Box* m_bg = nullptr;
    Label* m_icon = nullptr;
    ColorSpec m_bgColor;
    ColorSpec m_iconColor;
  };

  // Small pill button with an optional icon and font, highlighted while onWhen() is true.
  class Chip : public Item {
  public:
    Chip(std::string label, char32_t icon = 0, std::string fontFamily = {});
    Chip* onClick(std::function<void()> fn) {
      m_onClick = std::move(fn);
      return this;
    }
    Chip* onWhen(std::function<bool()> fn);
    Chip* labelFrom(std::function<std::string()> fn);
    float place(Renderer& renderer, float width) override;
    void sync() override;

  private:
    void restyle(bool animate);
    std::string m_text;
    std::function<void()> m_onClick;
    std::function<bool()> m_onWhen;
    std::function<std::string()> m_labelFn;
    bool m_on = false;
    Box* m_bg = nullptr;
    Label* m_icon = nullptr;
    Label* m_label = nullptr;
    ColorSpec m_bgColor;
  };
  // Chip that sets `path` to `value` and lights up while the setting holds it.
  [[nodiscard]] std::unique_ptr<Chip> choiceChip(const std::string& label, const std::string& path, json value,
                                                 std::string fontFamily = {});

  // Single-line text field. Shows the bound value except while focused and writes it on Enter (or on
  // every edit with applyOnEdit). `convert` turns the text into the value to write, or returns nullopt
  // to reject it (e.g. not a #hex).
  struct FieldOpts {
    float width = 260.0F;
    std::string placeholder;
    char32_t icon = 0;
    bool applyOnEdit = false;
    std::function<std::optional<json>(const std::string&)> convert;
    std::function<void(const std::string&)> onEdited;   // Every change (search fields).
    std::function<void(const std::string&)> onAccepted; // Enter
  };
  class Field : public Item {
  public:
    Field(std::optional<Binding> binding, FieldOpts opts);
    float place(Renderer& renderer, float width) override;
    void sync() override;
    [[nodiscard]] const std::string& text() const;
    void setText(const std::string& s);
    void setPlaceholder(const std::string& s);
    [[nodiscard]] InputArea* focusArea() const;
    [[nodiscard]] Input* input() const noexcept { return m_input; }

  private:
    void restyle(bool animate);
    std::optional<Binding> m_binding;
    FieldOpts m_opts;
    Box* m_bg = nullptr;
    Label* m_icon = nullptr;
    Label* m_placeholder = nullptr;
    Input* m_input = nullptr;
    ColorSpec m_border;
    bool m_focused = false;
  };
  // Field for a settings path. Writes the trimmed text on Enter; empty text is ignored unless allowEmpty.
  [[nodiscard]] std::unique_ptr<Field> textField(const std::string& path, float width, std::string placeholder = {},
                                                 bool allowEmpty = false);

  // Round colour dot for the accent swatches. Shows a ring while picked and grows on hover.
  class Swatch : public Item {
  public:
    Swatch(std::function<ColorSpec()> color, std::function<bool()> on, std::function<void()> onClick,
           float size = 28.0F, float ringWidth = 3.0F);
    float place(Renderer& renderer, float width) override;
    void sync() override;

  private:
    std::function<ColorSpec()> m_color;
    std::function<bool()> m_on;
    std::function<void()> m_onClick;
    float m_size;
    float m_ring;
    Box* m_dot = nullptr;
  };

  // Colour value for the bar editor: a preview dot, a "token / #hex" field ("a, b" for a gradient) and the
  // palette tokens as small dots. Clicking the picked token again unsets it.
  class ColorPick : public Item {
  public:
    explicit ColorPick(Binding binding);
    float place(Renderer& renderer, float width) override;
    void sync() override;

  private:
    Binding m_binding;
    Box* m_preview = nullptr;
    Field* m_field = nullptr;
    std::vector<std::pair<std::string, Box*>> m_tokens;
    std::vector<InputArea*> m_tokenAreas;
  };

  // Path field with a button that opens the path in the file manager.
  class PathField : public Item {
  public:
    PathField(const std::string& path, float width, std::string placeholder = {});
    float place(Renderer& renderer, float width) override;

  private:
    Field* m_field = nullptr;
    IconButton* m_open = nullptr;
  };

  // Pickers

  // One card per option with a small drawing of a screen in that style. `kind` is one of launcher, panel,
  // notifications, osd, corner, edge, tiles, panelLook, lock or power. For looks that set several keys,
  // pass a hand-made Binding (see "Panel looks" in sp_page_panel.cpp).
  class StylePicker : public Item {
  public:
    StylePicker(std::string kind, std::vector<Option> options, Binding binding, float cardW = 168.0F);
    float place(Renderer& renderer, float width) override;
    void sync() override;

  private:
    struct Card;
    std::string m_kind;
    std::vector<Option> m_options;
    Binding m_binding;
    float m_cardW;
    std::vector<Card*> m_cards;
  };

  // Every installed font, each drawn in itself, with a search field.
  class FontPicker : public Item {
  public:
    explicit FontPicker(Binding binding, std::string sample = "Kusanagi  0123  The quick brown fox");
    ~FontPicker() override;
    float place(Renderer& renderer, float width) override;
    void sync() override;

  private:
    struct Rows;
    void filter();
    void scrollTo(float offset);
    Binding m_binding;
    std::string m_sample;
    std::vector<std::string> m_all;
    std::vector<std::string> m_shownFonts;
    std::string m_current;
    Field* m_search = nullptr;
    Label* m_count = nullptr;
    Box* m_box = nullptr;
    InputArea* m_viewport = nullptr;
    Node* m_content = nullptr;
    Box* m_indicator = nullptr;
    std::unique_ptr<Rows> m_rows;
    float m_offset = 0.0F;
    bool m_loaded = false;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
  };

  // Searches the installed apps and reports the picked desktop entry id. `exclude` lists ids to leave out.
  class AppPicker : public Item {
  public:
    AppPicker(std::function<void(const std::string&)> picked, std::function<std::vector<std::string>()> exclude = {},
              int max = 8);
    float place(Renderer& renderer, float width) override;

  private:
    struct Result;
    void refresh();
    std::function<void(const std::string&)> m_picked;
    std::function<std::vector<std::string>()> m_exclude;
    int m_max;
    Field* m_query = nullptr;
    std::vector<std::string> m_ids;
    std::vector<Result*> m_results;
    bool m_dirty = true;
  };

  // Motion

  // Easing curves on 0..1.
  [[nodiscard]] float outCubic(float t);
  [[nodiscard]] float outQuint(float t);
  [[nodiscard]] float outBack(float t, float overshoot);
  // Colour tween. Cancels `owner`'s running animations first.
  void tweenColor(Node& owner, const ColorSpec& from, const ColorSpec& to, int baseMs,
                  std::function<void(const ColorSpec&)> apply);
  // Scale tween with an overshoot scaled by look.bounce.
  void tweenScale(Node& node, float to, int baseMs, float overshoot = 2.5F);

} // namespace kusanagi::sp
