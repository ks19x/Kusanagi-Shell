// Settings > Bar: the Layout tab (classic options or the layout editor from sp_page_bar_editor.cpp),
// layout templates from bar_templates.h, and Dock & taskbar.

#include "compositors/compositor_detect.h"
#include "core/files/resource_paths.h"
#include "core/timer_manager.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/animation/animation_manager.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/bar_templates.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/settings/bar_preview.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_page_bar.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "system/desktop_entry.h"
#include "system/icon_resolver.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <memory>

namespace kusanagi::sp {

  namespace {

    constexpr auto kPointer = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;

    // Formats a number the way JavaScript would, for building strings like "bg/0.5".
    std::string jsNum(const json& v) {
      if (!v.is_number()) return v.is_string() ? v.get<std::string>() : std::string("0");
      const double d = v.get<double>();
      if (d == std::floor(d) && std::abs(d) < 1e15) return std::to_string(static_cast<long long>(d));
      return std::format("{}", d);
    }

    bool customLayout() {
      const json b = value("bars");
      return b.is_array() && !b.empty();
    }

    // Page state. A fresh page starts on the Layout tab with nothing stashed.
    struct BarPage {
      std::string tab = "layout";
      json stash; // Custom layout kept when switching to classic, so switching back loses nothing.
    };

    // Dock helpers

    json pinned() {
      const json p = value("dock.pinned");
      return p.is_array() ? p : json::array();
    }
    bool isPinned(const std::string& id) {
      for (const auto& x : pinned())
        if (x == id) return true;
      return false;
    }
    void pin(const std::string& id) {
      if (id.empty() || isPinned(id)) return;
      json p = pinned();
      p.push_back(id);
      set("dock.pinned", p);
    }
    void unpin(const std::string& id) {
      json out = json::array();
      for (const auto& x : pinned())
        if (x != id) out.push_back(x);
      set("dock.pinned", out);
    }
    void movePin(const std::string& id, int delta) {
      json l = pinned();
      int i = -1;
      for (std::size_t n = 0; n < l.size(); ++n)
        if (l[n] == id) i = static_cast<int>(n);
      const int j = i + delta;
      if (i < 0 || j < 0 || j >= static_cast<int>(l.size())) return;
      const json x = l[static_cast<std::size_t>(i)];
      l.erase(l.begin() + i);
      l.insert(l.begin() + j, x);
      set("dock.pinned", l);
    }
    std::string lower(std::string s) {
      std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return s;
    }
    // Exact desktop entry id first, then looser matches on id, WM class and name.
    const DesktopEntry* entryOf(const std::string& id) {
      if (id.empty()) return nullptr;
      const auto& all = desktopEntries();
      for (const auto& e : all)
        if (e.id == id) return &e;
      const std::string l = lower(id);
      for (const auto& e : all)
        if (e.idLower == l || e.startupWmClassLower == l) return &e;
      const std::string last = l.substr(l.rfind('.') == std::string::npos ? 0 : l.rfind('.') + 1);
      for (const auto& e : all)
        if (e.idLower == last || e.nameLower == l) return &e;
      return nullptr;
    }
    std::string appName(const std::string& id) {
      if (id == "org.quickshell") return "Kusanagi";
      const DesktopEntry* e = entryOf(id);
      return e != nullptr ? e->name : id;
    }
    // An icon that actually exists: the entry's, the id in a few spellings, or a generic one. "" if none.
    std::string appIcon(const std::string& id) {
      if (id.empty()) return {};
      if (id == "org.quickshell") return paths::assetPath("kusanagi-logo.svg").string();
      static IconResolver resolver;
      const DesktopEntry* e = entryOf(id);
      const std::string last = id.substr(id.rfind('.') == std::string::npos ? 0 : id.rfind('.') + 1);
      for (const std::string& n : {e != nullptr ? e->icon : std::string(), id, lower(id), last, lower(last),
                                   std::string("application-x-executable")}) {
        if (n.empty()) continue;
        if (n[0] == '/') return n;
        if (const std::string& p = resolver.resolve(n, 72); !p.empty()) return p;
      }
      return {};
    }
    // The launcher's most launched apps, from launcher-usage.json.
    std::vector<std::string> topUsed(std::size_t n) {
      std::vector<std::pair<std::string, double>> counts;
      std::ifstream f(expandHome("~/.config/kusanagi/launcher-usage.json"));
      const json j = f ? json::parse(f, nullptr, false) : json();
      if (j.is_object() && j.contains("counts") && j["counts"].is_object())
        for (auto it = j["counts"].begin(); it != j["counts"].end(); ++it)
          if (it.value().is_number()) counts.emplace_back(it.key(), it.value().get<double>());
      std::ranges::stable_sort(counts, [](const auto& a, const auto& b) { return a.second > b.second; });
      std::vector<std::string> out;
      for (const auto& [id, c] : counts) {
        if (out.size() >= n) break;
        if (entryOf(id) != nullptr) out.push_back(id);
      }
      return out;
    }
    // True if any bar has a taskbar module.
    bool usesTaskbar() {
      const json bars = value("bars");
      if (!bars.is_array()) return false;
      auto isTaskbar = [](const json& m) { return m == "taskbar" || (m.is_object() && m.value("type", json()) == "taskbar"); };
      for (const auto& b : bars) {
        if (!b.is_object()) continue;
        for (const char* s : {"start", "center", "end"}) {
          const json l = b.value(s, json::array());
          if (!l.is_array()) continue;
          for (const auto& e : l) {
            if (isTaskbar(e)) return true;
            if (e.is_object() && e.contains("modules") && e["modules"].is_array())
              for (const auto& m : e["modules"])
                if (isTaskbar(m)) return true;
          }
        }
      }
      return false;
    }

    // One pinned app: icon (or a letter tile), name, and move / unpin buttons on hover.
    class PinTile : public Item {
    public:
      PinTile(std::string id, bool first, bool last) : m_id(std::move(id)) {
        m_bg = static_cast<Box*>(addChild(ui::box({})));
        m_bg->setRadius(14.0F);
        m_bg->setFill(textA(0.05F));
        m_bgColor = textA(0.05F);
        m_iconPath = appIcon(m_id);
        m_icon = static_cast<Image*>(addChild(ui::image({.fit = ImageFit::Contain})));
        m_letterTile = static_cast<Box*>(addChild(ui::box({})));
        m_letterTile->setRadius(9.0F);
        m_letterTile->setFill(accent(0.3F));
        const std::string name = appName(m_id);
        m_letter = static_cast<Label*>(m_letterTile->addChild(
            makeText(name.empty() ? std::string() : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])))), 18.0F, true)));
        m_name = static_cast<Label*>(addChild(makeText(name, 10.0F)));
        m_name->setMaxLines(1);
        m_name->setEllipsize(TextEllipsize::End);
        m_buttons = addChild(ui::node({}));
        m_buttons->setOpacity(0.0F);
        const std::string id2 = m_id;
        if (!first) m_left = static_cast<IconButton*>(m_buttons->addChild(std::make_unique<IconButton>(0xf004d, [id2]() { movePin(id2, -1); }, 22.0F, 13.0F)));
        m_unpin = static_cast<IconButton*>(m_buttons->addChild(std::make_unique<IconButton>(0xf0156, [id2]() { unpin(id2); }, 22.0F, 13.0F)));
        if (!last) m_right = static_cast<IconButton*>(m_buttons->addChild(std::make_unique<IconButton>(0xf0054, [id2]() { movePin(id2, 1); }, 22.0F, 13.0F)));
        setOnEnter([this](const PointerData&) { hover(true); });
      }

      float place(Renderer& renderer, float /*width*/) override {
        setSize(92.0F, 92.0F);
        m_bg->setSize(92.0F, 92.0F);
        if (!m_loaded && !m_iconPath.empty()) {
          m_loaded = true;
          m_hasIcon = m_icon->setSourceFile(renderer, m_iconPath, 72);
        }
        m_icon->setVisible(m_hasIcon);
        m_letterTile->setVisible(!m_hasIcon);
        m_icon->setSize(36.0F, 36.0F);
        m_icon->setPosition(28.0F, 12.0F);
        m_letterTile->setSize(36.0F, 36.0F);
        m_letterTile->setPosition(28.0F, 12.0F);
        m_letter->measure(renderer);
        m_letter->setPosition(std::round((36.0F - m_letter->width()) / 2.0F), std::round((36.0F - m_letter->height()) / 2.0F));
        m_name->setMaxWidth(82.0F);
        m_name->measure(renderer);
        m_name->setPosition(std::round((92.0F - m_name->width()) / 2.0F), 54.0F);
        float x = 0.0F;
        for (IconButton* b : {m_left, m_unpin, m_right}) {
          if (b == nullptr) continue;
          b->place(renderer, 22.0F);
          b->setPosition(x, 0.0F);
          x += 22.0F;
        }
        m_buttons->setSize(x, 22.0F);
        m_buttons->setPosition(std::round((92.0F - x) / 2.0F), 92.0F - 2.0F - 22.0F);
        return 92.0F;
      }

    private:
      void hover(bool on) {
        const bool in = on || hovered() || (m_left != nullptr && m_left->hovered()) || m_unpin->hovered()
                        || (m_right != nullptr && m_right->hovered());
        if (in == m_in) return;
        m_in = in;
        // Moving onto one of the buttons counts as leaving the tile, so while it looks hovered keep
        // checking whether the pointer is still over the tile or a button.
        if (in) {
          m_hoverCheck.startRepeating(std::chrono::milliseconds(100), [this]() { hover(false); });
        } else {
          m_hoverCheck.stop();
        }
        const ColorSpec to = textA(in ? 0.09F : 0.05F);
        tweenColor(*m_bg, m_bgColor, to, 120, [this](const ColorSpec& c) { m_bg->setFill(c); });
        m_bgColor = to;
        const float from = m_buttons->opacity();
        if (AnimationManager* a = animationManager(); a != nullptr) {
          a->cancelForOwner(m_buttons);
          a->animate(0.0F, 1.0F, 120.0F, Easing::Linear,
                     [this, from, in](float t) { m_buttons->setOpacity(from + ((in ? 1.0F : 0.0F) - from) * t); }, {}, m_buttons);
        } else {
          m_buttons->setOpacity(in ? 1.0F : 0.0F);
        }
      }

      Timer m_hoverCheck;
      std::string m_id;
      std::string m_iconPath;
      bool m_loaded = false;
      bool m_hasIcon = false;
      bool m_in = false;
      Box* m_bg = nullptr;
      ColorSpec m_bgColor;
      Image* m_icon = nullptr;
      Box* m_letterTile = nullptr;
      Label* m_letter = nullptr;
      Label* m_name = nullptr;
      Node* m_buttons = nullptr;
      IconButton* m_left = nullptr;
      IconButton* m_unpin = nullptr;
      IconButton* m_right = nullptr;
    };

    // Pinned apps in dock order, rebuilt when the list changes, or a line saying there are none.
    class PinnedTiles : public Flow {
    public:
      PinnedTiles() : Flow(8.0F) { sync(); }
      void sync() override {
        const json p = pinned();
        if (p == m_last && !items().empty()) return;
        m_last = p;
        while (!children().empty()) removeChild(children().back().get());
        for (std::size_t i = 0; i < p.size(); ++i)
          if (p[i].is_string()) add<PinTile>(p[i].get<std::string>(), i == 0, i + 1 == p.size());
        if (p.empty()) add<EmptyLine>();
        requestLayout();
      }

    private:
      // Vertically centred in a 34 px line.
      class EmptyLine : public Text {
      public:
        EmptyLine() : Text("Nothing pinned yet — search below, or pin your most used apps in one go.", TextOpts{.px = 11.0F, .color = dim()}) {}
        float place(Renderer& renderer, float width) override {
          Text::place(renderer, width);
          label()->setPosition(0.0F, std::round((34.0F - label()->height()) / 2.0F));
          setSize(label()->width(), 34.0F);
          return 34.0F;
        }
      };
      json m_last = nullptr;
    };

    // Template cards

    class TemplateCard : public Item {
    public:
      TemplateCard(const bar_templates::Template& t, std::function<void()> onPick) : m_onPick(std::move(onPick)) {
        const json& b0 = t.bars.empty() ? json::object() : t.bars[0];
        m_vert = b0.value("position", std::string("top")) == "left" || b0.value("position", std::string("top")) == "right";
        m_bg = static_cast<Box*>(addChild(ui::box({})));
        m_bg->setRadius(12.0F);
        m_bg->setBorder(textA(0.06F), 1.0F);
        m_bars = static_cast<Column*>(addChild(std::make_unique<Column>(2.0F)));
        const auto n = static_cast<float>(std::max<std::size_t>(1, t.bars.size()));
        const float cardH = m_vert ? 120.0F : 82.0F;
        for (const auto& bar : t.bars) {
          const float h = m_vert ? 84.0F : (cardH - 36.0F) / n - 2.0F;
          m_bars->add<BarPreview>([bar]() { return bar; }, [h](float) { return h; }, BarPreviewOpts{.snapshot = true});
        }
        m_name = static_cast<Label*>(addChild(makeText(t.name, 12.0F, true)));
        m_note = static_cast<Label*>(addChild(makeText(t.note, 10.0F, false, dim())));
        setCursorShape(kPointer);
        setOnEnter([this](const PointerData&) { m_bg->setFill(textA(0.08F)); });
        setOnLeave([this]() { m_bg->setFill(textA(0.04F)); });
        setOnClick([this](const PointerData&) {
          if (m_onPick) m_onPick();
        });
        m_bg->setFill(textA(0.04F));
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float h = m_vert ? 120.0F : 82.0F;
        setSize(width, h);
        m_bg->setSize(width, h);
        m_bars->place(renderer, width - 16.0F);
        m_bars->setPosition(8.0F, 8.0F);
        // The note is centred on the name's line.
        m_name->measure(renderer);
        m_note->measure(renderer);
        const float rowH = std::max(m_name->height(), m_note->height());
        m_name->setPosition(12.0F, h - 24.0F);
        m_note->setPosition(12.0F + m_name->width() + 10.0F, h - 24.0F + std::round((rowH - m_note->height()) / 2.0F));
        return h;
      }

    private:
      std::function<void()> m_onPick;
      bool m_vert = false;
      Box* m_bg = nullptr;
      Column* m_bars = nullptr;
      Label* m_name = nullptr;
      Label* m_note = nullptr;
    };

  } // namespace

  json legacyBars() {
    const json b = value("bar");
    const json mods = b.value("modules", json::object());
    auto opt = [&b](const char* key) { return value(std::string("bar.") + key); };
    const std::string style = opt("style").get<std::string>();
    const bool islands = style == "islands", floating = style == "floating", solid = style == "solid";
    const bool centered = opt("layout") == "centered", bottom = opt("position") == "bottom";
    const bool acc = truthy(opt("accentLabels"));
    auto lbl = [acc](const std::string& t) { return acc ? "<font color=\"accent\">" + t + "</font>" : t; };
    auto mod = [](const char* key) { return truthy(value(std::string("bar.modules.") + key)); };
    (void)mods;
    const bool flush = value("workspaces.style") == "dwl" && !centered;
    auto scrollFor = [](const json& a) {
      if (a == "volume") return std::pair<std::string, std::string>{"volume:up", "volume:down"};
      if (a == "workspaces") return std::pair<std::string, std::string>{"workspace:prev", "workspace:next"};
      return std::pair<std::string, std::string>{"none", "none"};
    };
    const auto stats = scrollFor(opt("scrollStats"));
    const json opacity = opt("opacity"), radius = opt("radius"), height = opt("height");
    const std::string bgTok = "bg/" + jsNum(opacity);
    const json island = {{"type", "group"},
                         {"bg", islands ? bgTok : "transparent"},
                         {"border", truthy(opt("outline")) && islands ? "text/0.12" : ""},
                         {"borderWidth", truthy(opt("outline")) && islands ? 1 : 0},
                         {"radius", radius}};
    const int hg = truthy(opt("hoverGrow")) ? 4 : 0;
    auto withStats = [&stats](json m) {
      m["scrollUp"] = stats.first;
      m["scrollDown"] = stats.second;
      return m;
    };

    json wsGroup = island;
    wsGroup["gap"] = flush || centered ? json::array({0, 0}) : json::array({6, 0});
    if (flush) wsGroup["inset"] = json::array({0, 0}); // Otherwise inset stays unset.
    wsGroup["radius"] = flush ? json(0) : radius;
    wsGroup["scrollUp"] = "workspace:prev";
    wsGroup["scrollDown"] = "workspace:next";
    wsGroup["modules"] = json::array({json{{"type", "workspaces"}, {"padding", flush ? json::array({0, 0}) : json::array({8, 8})}, {"gap", json::array({0, 0})}},
                                      json{{"type", "title"}, {"show", mod("title") && !centered}, {"padding", json::array({12, 10})}, {"maxLength", opt("titleWidth")}}});
    json clockGroup = island;
    clockGroup["gap"] = centered ? json::array({6, 0}) : json::array({0, 0});
    const auto clockScroll = scrollFor(opt("scrollClock"));
    clockGroup["modules"] = json::array({json{{"type", "clock"}, {"timeFormat", opt("clock")}, {"bold", opt("clockBold")}, {"hoverGrow", hg},
                                              {"scrollUp", clockScroll.first}, {"scrollDown", clockScroll.second}}});
    json right = json::array();
    if (mod("media"))
      right.push_back(withStats({{"type", "media"}, {"width", opt("mediaWidth")}, {"marquee", opt("marquee")}, {"popup", opt("mediaPopup")}, {"hoverGrow", hg}}));
    right.push_back({{"type", "caffeine"}, {"fg", acc ? "accent" : ""}, {"fontSize", "+2"}, {"padding", json::array({10, 7})}});
    if (mod("cpu")) right.push_back(withStats({{"type", "cpu"}, {"format", lbl("CPU") + " {usage}%"}, {"hoverGrow", hg}}));
    if (mod("ram")) right.push_back(withStats({{"type", "ram"}, {"format", lbl("RAM") + " {percent}%"}, {"hoverGrow", hg}}));
    if (mod("gpu")) right.push_back(withStats({{"type", "gpu"}, {"format", lbl("GPU") + " {usage}%"}, {"hoverGrow", hg}}));
    if (mod("temp")) right.push_back(withStats({{"type", "temp"}, {"hoverGrow", hg}}));
    if (mod("volume"))
      right.push_back({{"type", "volume"}, {"fontSize", "+2"}, {"padding", json::array({10, 0})}, {"gap", json::array({2, 0})}, {"hoverGrow", hg}});
    if (mod("network"))
      right.push_back(withStats({{"type", "network"}, {"fg", acc ? "accent" : ""}, {"fontSize", "+2"}, {"padding", json::array({10, 7})}, {"hoverGrow", hg}}));
    if (mod("tray")) right.push_back({{"type", "tray"}, {"padding", json::array({10, 6})}, {"gap", json::array({0, 0})}, {"iconSize", opt("trayIconSize")}});
    if (mod("power"))
      right.push_back(withStats({{"type", "power"}, {"fg", acc ? "accent" : ""}, {"fontSize", "+4"}, {"padding", json::array({10, 6})}, {"gap", json::array({2, 10})}}));
    json rightGroup = island;
    rightGroup["gap"] = json::array({0, 6});
    rightGroup["modules"] = right;

    const double h = height.is_number() ? height.get<double>() : 28.0;
    json bar = {{"position", bottom ? "bottom" : "top"},
                {"size", floating ? json(h - 4) : height},
                {"margin", floating ? json::array({3, 1, 6}) : json::array({0, 0, 0})},
                {"bg", solid || floating ? bgTok : "transparent"},
                {"radius", floating ? radius : json(0)},
                // Mango's outer gap is one value for top and bottom. Reserve 5 px less so windows end up
                // at the same 3 px.
                {"exclusive", compositors::isMango() && !bottom ? json(h - 5) : json(true)},
                {"group", {{"inset", floating ? json::array({1, 1}) : json::array({4, 2})}}}};
    bar["start"] = json::array({centered ? clockGroup : wsGroup});
    bar["center"] = json::array({centered ? wsGroup : clockGroup});
    bar["end"] = json::array({rightGroup});
    return deepNormalized(json::array({bar}));
  }

  void buildBarClassic(Column& col) {
    {
      auto* g = col.add<Group>("Style");
      g->add<Segmented>(bind("bar.style"),
                        std::vector<Option>{{"Islands", "islands"}, {"Solid", "solid"}, {"Floating", "floating"}, {"Clear", "clear"}}, 0.0F);
      g->add<Row>("Position", std::make_unique<Segmented>(bind("bar.position"), std::vector<Option>{{"Top", "top"}, {"Bottom", "bottom"}}, 220.0F));
      g->add<Row>("Layout", "where the workspaces and the clock sit",
                  std::make_unique<Segmented>(bind("bar.layout"),
                                              std::vector<Option>{{"Workspaces · Clock", "classic"}, {"Clock · Workspaces", "centered"}}, 340.0F, 10.0F));
      g->add<Row>("Height", std::make_unique<Stepper>(bind("bar.height"), StepperOpts{.from = 24, .to = 40, .step = 2, .suffix = "px"}));
      g->add<Slider>(bind("bar.opacity"), SliderOpts{
                                              .icon = 0xf050e,
                                              .label = "Background",
                                              .toUnit = [](const json& v) { return v.is_number() ? v.get<float>() : 0.0F; },
                                              .fromUnit = [](float u) { return json(std::round(u * 100.0) / 100.0); },
                                              .text = [](const json& v) { return std::to_string(std::lround((v.is_number() ? v.get<double>() : 0.0) * 100.0)) + "%"; },
                                          });
      g->add<Slider>(bind("bar.radius"), SliderOpts{
                                             .icon = 0xf0830,
                                             .label = "Roundness",
                                             .toUnit = [](const json& v) { return (v.is_number() ? v.get<float>() : 0.0F) / 12.0F; },
                                             .fromUnit = [](float u) { return json(std::lround(u * 12.0F)); },
                                             .text = [](const json& v) { return jsNum(v) + "px"; },
                                             .step = 1.0F / 12.0F,
                                         });
      g->add<Row>("Outline the islands", std::make_unique<Switch>(bind("bar.outline")));
      g->add<Row>("Accent labels", "CPU / RAM / GPU and the icons in your accent colour", std::make_unique<Switch>(bind("bar.accentLabels")));
      g->add<Row>("Text size", "icons scale along", std::make_unique<Stepper>(bind("bar.fontSize"), StepperOpts{.from = 9, .to = 14, .suffix = "px"}));
      g->add<Row>("Tray icon size", std::make_unique<Stepper>(bind("bar.trayIconSize"), StepperOpts{.from = 10, .to = 22, .suffix = "px"}));
      g->add<Row>("Grow on hover", std::make_unique<Switch>(bind("bar.hoverGrow")));
    }
    {
      auto* g = col.add<Group>("Clock");
      g->add<Segmented>(bind("bar.clock"),
                        std::vector<Option>{{"20:31", "HH:mm"}, {"20:31:07", "HH:mm:ss"}, {"8:31 PM", "h:mm AP"}, {"Fri 20:31", "ddd HH:mm"},
                                            {"2 Oct 20:31", "d MMM HH:mm"}},
                        0.0F);
      g->add<Row>("Bold", std::make_unique<Switch>(bind("bar.clockBold")));
      g->add<Row>("Custom format", "Qt date format — HH mm ss · h AP · ddd dddd · d MMM yyyy", textField("bar.clock", 200.0F));
    }
    {
      auto* g = col.add<Group>("Modules", "What sits on the right side of the bar.");
      auto* flow = g->add<Flow>(6.0F);
      struct M {
        const char* key;
        const char* label;
        char32_t icon;
      };
      for (const M& m : {M{"title", "Window title", 0xf05b1}, M{"media", "Media", 0xf075a}, M{"cpu", "CPU", 0xf0ee0}, M{"ram", "RAM", 0xf035b},
                         M{"gpu", "GPU", 0xf08ae}, M{"temp", "Temperature", 0xf050f}, M{"volume", "Volume", 0xf057e},
                         M{"network", "Network", 0xf0200}, M{"tray", "Tray", 0xf003b}, M{"power", "Power", 0xf0425}}) {
        const std::string path = std::string("bar.modules.") + m.key;
        flow->add<Chip>(m.label, m.icon)->onWhen([path]() { return truthy(value(path)); })->onClick([path]() { set(path, !truthy(value(path))); });
      }
    }
    {
      auto* g = col.add<Group>("Scrolling");
      const std::vector<Option> what{{"Volume", "volume"}, {"Workspaces", "workspaces"}, {"Nothing", "none"}};
      g->add<Row>("On the clock", std::make_unique<Segmented>(bind("bar.scrollClock"), what, 300.0F));
      g->add<Row>("On the right side", "media, stats, network, power", std::make_unique<Segmented>(bind("bar.scrollStats"), what, 300.0F));
      g->add<Row>("Volume step", std::make_unique<Stepper>(bind("bar.volumeStep"), StepperOpts{.from = 1, .to = 20, .suffix = "%"}));
    }
    {
      auto* g = col.add<Group>("Media");
      g->add<Row>("Controls on hover", "cover, seek bar, prev / play / next", std::make_unique<Switch>(bind("bar.mediaPopup")));
      g->add<Row>("Scroll long titles", std::make_unique<Switch>(bind("bar.marquee")));
      g->add<Row>("Window title width", std::make_unique<Stepper>(bind("bar.titleWidth"), StepperOpts{.from = 20, .to = 120, .step = 10, .suffix = " ch"}));
      g->add<Row>("Title width", std::make_unique<Stepper>(bind("bar.mediaWidth"), StepperOpts{.from = 10, .to = 50, .step = 5, .suffix = " ch"}));
    }
    col.add<Chip>("Reset the bar", 0xf0709)->onClick([]() { reset("bar"); });
  }

  void buildBar(Column& page) {
    auto st = std::make_shared<BarPage>();
    auto tabIs = [st](const char* t) { return [st, t]() { return st->tab == t; }; };

    page.add<Segmented>(Binding{.get = [st]() -> json { return st->tab; },
                                .set =
                                    [st](const json& v) {
                                      st->tab = v.get<std::string>();
                                      refresh();
                                    }},
                        std::vector<Option>{{"Layout", "layout"}, {"Templates", "templates"}, {"Dock & taskbar", "dock"}}, 0.0F);

    // Layout tab: classic options or the custom layout editor.
    {
      auto* col = page.add<Column>(22.0F);
      col->showIf(tabIs("layout"));
      auto* g = col->add<Group>("Mode", " ");
      g->bindHint([]() {
        return customLayout() ? std::string("Your own layout: every bar, group and module is editable below (or in settings.json → \"bars\").")
                              : std::string("The classic Kusanagi bar, set up with the options below. Pick a template or go custom to build any bar.");
      });
      g->add<Segmented>(Binding{.get = []() -> json { return customLayout() ? "custom" : "classic"; },
                                .set =
                                    [st](const json& v) {
                                      if (v == "custom") {
                                        if (customLayout()) return;
                                        // Start from what is on screen now: the classic bar as a layout.
                                        set("bars", st->stash.is_array() && !st->stash.empty() ? st->stash : legacyBars());
                                      } else {
                                        if (!customLayout()) return;
                                        st->stash = value("bars");
                                        set("bars", json::array());
                                      }
                                    }},
                        std::vector<Option>{{"Classic options", "classic"}, {"Custom layout", "custom"}}, 0.0F);
      auto* classic = col->add<Column>(22.0F);
      classic->showIf([]() { return !customLayout(); });
      buildBarClassic(*classic);
      auto* editor = col->add<Column>(22.0F);
      editor->showIf([]() { return customLayout(); });
      buildBarEditor(*editor);
    }

    {
      auto* col = page.add<Column>(22.0F);
      col->showIf(tabIs("templates"));
      auto* g = col->add<Group>("Templates", "Start from one of these; then change anything. Also: kusanagi msg bar template <name>.");
      auto* grid = g->add<Column>(8.0F);
      for (const auto& t : bar_templates::list()) {
        const std::string id = t.id;
        grid->add<TemplateCard>(t, [st, id]() {
          st->stash = nullptr;
          set("bars", deepNormalized(bar_templates::bars(id)));
          st->tab = "layout";
          refresh();
        });
      }
    }

    {
      auto* col = page.add<Column>(22.0F);
      col->showIf(tabIs("dock"));
      auto* g = col->add<Group>("Dock & taskbar", " ");
      g->bindHint([]() {
        return usesTaskbar() ? std::string("Apps pinned here stay in the dock even when closed. Right-click any dock icon to pin or unpin it.")
                             : std::string("Pinned apps for the Taskbar module — the Dock and Taskbar templates use it. Right-click a dock icon to pin it.");
      });
      g->add<PinnedTiles>();
      g->add<AppPicker>([](const std::string& id) { pin(id); },
                        []() {
                          std::vector<std::string> out;
                          for (const auto& x : pinned())
                            if (x.is_string()) out.push_back(x.get<std::string>());
                          return out;
                        });
      auto* chips = g->add<Flow>(6.0F);
      chips->add<Chip>("Pin my 6 most used apps", 0xf0403)->onClick([]() {
        json p = pinned();
        for (const auto& id : topUsed(6))
          if (std::ranges::find(p, json(id)) == p.end()) p.push_back(id);
        set("dock.pinned", p);
      });
      chips->add<Chip>("Unpin all", 0xf0a7a)->onClick([]() { set("dock.pinned", json::array()); })->showIf([]() { return !pinned().empty(); });
      g->add<Row>("Running apps", "how an open app is marked",
                  std::make_unique<Segmented>(bind("dock.indicator"), std::vector<Option>{{"Dots", "dot"}, {"Line", "line"}, {"None", "none"}}, 240.0F));
      g->add<Row>("One icon per app", "off: one per window, like a classic taskbar", std::make_unique<Switch>(bind("dock.grouped")));
      g->add<Slider>(bind("dock.magnify"), SliderOpts{
                                               .icon = 0xf0349,
                                               .label = "Grow on hover",
                                               .toUnit = [](const json& v) { return ((v.is_number() ? v.get<float>() : 1.0F) - 1.0F) / 0.8F; },
                                               .fromUnit = [](float u) { return json(std::round((1.0 + u * 0.8) * 20.0) / 20.0); },
                                               .text =
                                                   [](const json& v) {
                                                     const double m = v.is_number() ? v.get<double>() : 1.0;
                                                     return m <= 1.001 ? std::string("off") : std::format("×{:.2f}", m);
                                                   },
                                           });
    }
  }

} // namespace kusanagi::sp
