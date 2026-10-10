// Settings > Panel: looks, the Home tab and its section order, layout, and which tiles show.

#include "cursor-shape-v1-client-protocol.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace kusanagi::sp {

  namespace {

    const std::array<std::pair<const char*, const char*>, 17> kTileNames{{
        {"nightlight", "Night light"}, {"dnd", "Do not disturb"}, {"mic", "Microphone"},  {"gamemode", "Game mode"},
        {"screenshot", "Screenshot"},  {"record", "Record"},      {"colorpicker", "Colour picker"},
        {"wallpaper", "Wallpaper"},    {"clipboard", "Clipboard"}, {"lock", "Lock"},      {"settings", "Settings"},
        {"launcher", "Apps"},          {"caffeine", "Caffeine"},  {"replay", "Replay buffer"}, {"clip", "Save clip"},
        {"updates", "Updates"},        {"bluetooth", "Bluetooth"},
    }};

    struct Look {
      const char* value;
      const char* label;
      const char* note;
      json set;
    };
    const std::vector<Look>& looks() {
      static const std::vector<Look> l{
          {"default", "Default", "cards, big clock, everything",
           json{{"tileStyle", "cards"}, {"tileColumns", 4}, {"sliderStyle", "thick"}, {"header", "big"}, {"tabs", true},
                {"order", {"tiles", "sliders", "media", "weather", "stats"}}}},
          {"compact", "Compact", "pills, slim sliders first",
           json{{"tileStyle", "pills"}, {"tileColumns", 2}, {"sliderStyle", "slim"}, {"header", "compact"}, {"tabs", true},
                {"order", {"sliders", "tiles", "media", "stats"}}}},
          {"icons", "Icon grid", "round buttons, no header",
           json{{"tileStyle", "icons"}, {"tileColumns", 6}, {"sliderStyle", "slim"}, {"header", "hidden"}, {"tabs", false},
                {"order", {"tiles", "sliders", "media"}}}},
          {"dashboard", "Dashboard", "music and weather on top",
           json{{"tileStyle", "cards"}, {"tileColumns", 3}, {"sliderStyle", "thick"}, {"header", "big"}, {"tabs", true},
                {"order", {"media", "weather", "tiles", "sliders", "stats"}}}},
      };
      return l;
    }

    std::string currentLook() {
      for (const auto& l : looks()) {
        const json& s = l.set;
        if (value("panel.tileStyle") == s["tileStyle"] && value("panel.tileColumns") == s["tileColumns"]
            && value("panel.sliderStyle") == s["sliderStyle"] && value("panel.header") == s["header"]
            && value("panel.order") == s["order"]) {
          return l.value;
        }
      }
      return "custom";
    }

    json order() {
      json o = value("panel.order");
      return o.is_array() ? o : json::array();
    }

    int indexIn(const json& arr, const std::string& v) {
      for (std::size_t i = 0; i < arr.size(); ++i) {
        if (arr[i] == v) return static_cast<int>(i);
      }
      return -1;
    }

    // One section: on/off switch, name, and up / down buttons.
    class SectionRow : public Item {
    public:
      SectionRow(std::string id, std::string name) : m_id(std::move(id)) {
        m_bg = static_cast<Box*>(addChild(ui::box({})));
        m_switch = static_cast<Switch*>(addChild(std::make_unique<Switch>(Binding{
            .get = [this]() -> json { return indexIn(order(), m_id) >= 0; },
            .set = [this](const json& v) {
              json o = order();
              const int at = indexIn(o, m_id);
              if (truthy(v) && at < 0) o.push_back(m_id);
              if (!truthy(v) && at >= 0) o.erase(o.begin() + at);
              set("panel.order", o);
            },
        })));
        m_label = static_cast<Label*>(addChild(makeText(name, 12.0F)));
        m_up = static_cast<IconButton*>(addChild(std::make_unique<IconButton>(0xf005d, [this]() { move(-1); })));
        m_down = static_cast<IconButton*>(addChild(std::make_unique<IconButton>(0xf0045, [this]() { move(1); })));
        m_up->enabledWhen([this]() { return indexIn(order(), m_id) > 0; });
        m_down->enabledWhen([this]() {
          const json o = order();
          const int at = indexIn(o, m_id);
          return at >= 0 && at < static_cast<int>(o.size()) - 1;
        });
        sync();
      }

      void move(int by) {
        json o = order();
        const int at = indexIn(o, m_id);
        const int to = at + by;
        if (at < 0 || to < 0 || to >= static_cast<int>(o.size())) return;
        std::swap(o[static_cast<std::size_t>(at)], o[static_cast<std::size_t>(to)]);
        set("panel.order", o);
      }

      [[nodiscard]] const std::string& id() const noexcept { return m_id; }

      void sync() override {
        const bool on = indexIn(order(), m_id) >= 0;
        m_bg->setFill(textA(on ? 0.05F : 0.02F));
        m_label->setColor(on ? textA(1.0F) : dim());
        m_up->setVisible(on);
        m_down->setVisible(on);
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float h = 42.0F;
        setSize(width, h);
        m_bg->setSize(width, h);
        m_bg->setRadius(10.0F);
        m_switch->place(renderer, width);
        m_switch->setPosition(10.0F, std::round((h - 22.0F) / 2.0F));
        m_label->measure(renderer);
        m_label->setPosition(66.0F, std::round((h - m_label->height()) / 2.0F));
        m_up->place(renderer, width);
        m_down->place(renderer, width);
        m_down->setPosition(width - 8.0F - 34.0F, std::round((h - 34.0F) / 2.0F));
        m_up->setPosition(width - 8.0F - 68.0F, std::round((h - 34.0F) / 2.0F));
        return h;
      }

    private:
      std::string m_id;
      Box* m_bg = nullptr;
      Switch* m_switch = nullptr;
      Label* m_label = nullptr;
      IconButton* m_up = nullptr;
      IconButton* m_down = nullptr;
    };

    // Sections in panel.order, with the ones that are off at the end.
    class SectionList : public Column {
    public:
      SectionList() : Column(14.0F) {
        const std::array<std::pair<const char*, const char*>, 5> names{
            {{"tiles", "Quick tiles"}, {"sliders", "Volume + microphone"}, {"media", "Now playing"}, {"weather", "Weather"},
             {"stats", "Quick stats"}}};
        for (const auto& [id, name] : names) m_rows.push_back(add<SectionRow>(id, name));
        sort();
      }

      void sort() {
        const json o = order();
        std::vector<SectionRow*> rows = m_rows;
        std::ranges::stable_sort(rows, [&o](SectionRow* a, SectionRow* b) {
          const int ia = indexIn(o, a->id());
          const int ib = indexIn(o, b->id());
          return (ia < 0 ? 99 : ia) < (ib < 0 ? 99 : ib);
        });
        // Re-insert in order, since Column lays out children in order.
        std::vector<std::unique_ptr<Node>> taken;
        for (SectionRow* r : rows) taken.push_back(removeChild(r));
        for (auto& n : taken) addChild(std::move(n));
      }

      void sync() override {
        sort();
        requestLayout();
      }

    private:
      std::vector<SectionRow*> m_rows;
    };

  } // namespace

  void buildPanel(Column& page) {
    {
      auto* g = page.add<Group>("Panel looks", "The Home tab in one click — then change any part of it below.", 0xf056e);
      std::vector<Option> opts;
      for (const auto& l : looks()) opts.push_back(Option{l.label, l.value, 0, l.note});
      g->add<StylePicker>("panelLook", std::move(opts),
                          Binding{
                              .get = []() -> json { return currentLook(); },
                              .set = [](const json& v) {
                                for (const auto& l : looks()) {
                                  if (v != l.value) continue;
                                  std::vector<std::pair<std::string, json>> changes;
                                  for (const auto& [k, val] : l.set.items()) changes.emplace_back("panel." + k, val);
                                  write(std::move(changes));
                                }
                              },
                          },
                          128.0F);
    }

    {
      auto* g = page.add<Group>("Home tab");
      g->add<Heading>("Tiles", 0.0F);
      g->add<StylePicker>("tiles", std::vector<Option>{{"Cards", "cards"}, {"Pills", "pills"}, {"Icons", "icons"}},
                          bind("panel.tileStyle"), 128.0F);
      g->add<Row>("Tiles per row", std::make_unique<Stepper>(bind("panel.tileColumns"), StepperOpts{.from = 1, .to = 8}));
      g->add<Row>("Sliders", std::make_unique<Segmented>(bind("panel.sliderStyle"),
                                                         std::vector<Option>{{"Thick", "thick"}, {"Slim", "slim"}}, 220.0F));
      g->add<Row>("Header", "clock, date, you and the buttons at the top",
                  std::make_unique<Segmented>(bind("panel.header"),
                                              std::vector<Option>{{"Big", "big"}, {"Compact", "compact"}, {"Hidden", "hidden"}},
                                              280.0F));
      g->add<Row>("Tab row", "off: the panel only shows Home (tabs stay on Super+N, IPC…)",
                  std::make_unique<Switch>(bind("panel.tabs")));
      g->add<Heading>("Sections, top to bottom", 6.0F);
      g->add<SectionList>();
    }

    {
      auto* g = page.add<Group>("Layout");
      g->add<Row>("Width", std::make_unique<Stepper>(bind("panel.width"),
                                                     StepperOpts{.from = 480, .to = 720, .step = 20, .suffix = "px"}));
      g->add<Row>("Opens on", std::make_unique<Segmented>(bind("panel.defaultTab"),
                                                          std::vector<Option>{{"Home", 0}, {"Sound", 4}, {"Net", 5}, {"System", 1},
                                                                              {"Inbox", 2}, {"Quick", 3}},
                                                          400.0F, 10.0F));
      g->add<Heading>("Design");
      g->add<StylePicker>("panel",
                          std::vector<Option>{{"Grow from clock", "island", 0, "pours out of the bar's clock"},
                                              {"Drop down", "drop", 0, "full width, slides down"},
                                              {"Float", "fade", 0, "fades in, centred"},
                                              {"Side sheet", "sheet", 0, "full height at the right"}},
                          bind("panel.morph"));
      g->add<Row>("Weather location", "Empty = guessed from your connection",
                  textField("weather.location", 220.0F, "e.g. Berlin", true));
      g->add<Row>("Units", std::make_unique<Segmented>(bind("weather.units"),
                                                       std::vector<Option>{{"°C · km/h", "metric"}, {"°F · mph", "imperial"}},
                                                       220.0F));
    }

    {
      auto* g = page.add<Group>("Tiles", "Tap to add or remove — they appear in the order you add them (4 per row).");
      auto* flow = g->add<Flow>(6.0F);
      for (const auto& [id, name] : kTileNames) {
        const std::string tile = id;
        const std::string label = name;
        const auto pos = [tile]() {
          const json t = value("panel.tiles");
          return t.is_array() ? indexIn(t, tile) : -1;
        };
        flow->add<Chip>(label)
            ->labelFrom([pos, label]() {
              const int p = pos();
              return (p >= 0 ? std::to_string(p + 1) + "  " : std::string()) + label;
            })
            ->onWhen([pos]() { return pos() >= 0; })
            ->onClick([tile, pos]() {
              json t = value("panel.tiles");
              if (!t.is_array()) t = json::array();
              const int p = pos();
              if (p >= 0) {
                t.erase(t.begin() + p);
              } else {
                t.push_back(tile);
              }
              set("panel.tiles", t);
            });
      }
    }

    page.add<Chip>("Reset the control panel", 0xf0709)->onClick([]() { reset("panel"); });
  }

} // namespace kusanagi::sp
