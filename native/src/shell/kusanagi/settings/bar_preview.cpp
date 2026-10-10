// Bar preview for the settings app. Widgets get the same specs config/kusanagi_import.cpp builds for the real
// bar, and the layout follows the live bar so the preview looks like the real thing.

#include "shell/kusanagi/settings/bar_preview.h"

#include "compositors/compositor_platform.h"
#include "core/timer_manager.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/animation/animation_manager.h"
#include "render/core/render_styles.h"
#include "render/core/renderer.h"
#include "shell/bar/widget.h"
#include "shell/bar/widget_factory.h"
#include "shell/bar/widgets/kusanagi_box.h"
#include "shell/bar/widgets/kusanagi_module_widget.h"
#include "shell/bar/widgets/kusanagi_workspaces_widget.h"
#include "shell/bar/widgets/tray_widget.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"
#include "wayland/wayland_connection.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>
#include <linux/input-event-codes.h>

namespace kusanagi::sp {

  namespace {

    constexpr auto kPointer = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    constexpr float kDragThreshold = 10.0F;

    double num(const json& j, const char* key, double fallback) {
      const auto it = j.find(key);
      return it != j.end() && it->is_number() ? it->get<double>() : fallback;
    }
    std::string str(const json& j, const char* key, const std::string& fallback = {}) {
      const auto it = j.find(key);
      return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
    }
    json pairOf(const json& v, double a, double b) {
      if (v.is_array() && v.size() >= 2) return json::array({v[0], v[1]});
      if (v.is_array() && v.size() == 1) return json::array({v[0], v[0]});
      if (v.is_number()) return json::array({v, v});
      return json::array({a, b});
    }
    float at(const json& pair, std::size_t i) {
      return pair.is_array() && pair.size() > i && pair[i].is_number() ? pair[i].get<float>() : 0.0F;
    }
    // Accepts n, [edge, inner, sides], [edge, sides] or { edge, inner, sides }.
    std::array<float, 3> triple(const json& v) {
      auto n = [](const json& x) { return x.is_number() ? x.get<float>() : 0.0F; };
      if (v.is_number()) return {n(v), n(v), n(v)};
      if (v.is_array()) {
        if (v.size() >= 3) return {n(v[0]), n(v[1]), n(v[2])};
        if (v.size() == 2) return {n(v[0]), n(v[0]), n(v[1])};
        if (v.size() == 1) return {n(v[0]), n(v[0]), n(v[0])};
      }
      if (v.is_object()) return {static_cast<float>(num(v, "edge", 0)), static_cast<float>(num(v, "inner", 0)),
                                 static_cast<float>(num(v, "sides", 0))};
      return {0.0F, 0.0F, 0.0F};
    }
    json merged(json base, const json& over) {
      if (!base.is_object()) base = json::object();
      if (over.is_object())
        for (auto it = over.begin(); it != over.end(); ++it) base[it.key()] = it.value();
      return base;
    }
    std::string stripTags(const std::string& s) {
      std::string out;
      bool tag = false;
      for (const char c : s) {
        if (c == '<') tag = true;
        else if (c == '>') tag = false;
        else if (!tag) out += c;
      }
      return out;
    }

  } // namespace

  struct BarPreview::Impl {
    BarPreview& self;
    std::function<json()> barFn;
    std::function<float(float)> heightFn;
    BarPreviewOpts opts;
    std::function<void(const std::string&, int, int)> picked;
    std::function<void(const std::string&, const std::string&, bool)> moved;
    std::function<std::string()> selKeyFn;

    // A change to either of these rebuilds the widgets.
    json raw;               // The spec as given.
    std::string look;       // Palette and font the widgets were made with.
    bool dirty = true;
    float builtW = -1.0F;

    // The bar spec with defaults filled in.
    json b = json::object();
    bool vertical = false;
    std::string edge = "top";
    float size = 28.0F;
    std::array<float, 3> margin{};
    float pad = 0.0F;
    float spacing = 0.0F;

    // Everything is drawn on a virtual screen that is scaled into the item.
    Box* desktop = nullptr;
    Node* screen = nullptr;
    Node* barNode = nullptr;
    std::unique_ptr<KusanagiBox> barBox;
    Node* barBoxNode = nullptr;
    float k = 1.0F;
    float screenX = 0.0F, screenY = 0.0F;

    // Something pickable. Its area's offset on the virtual screen converts pointer positions into item
    // coordinates.
    struct Target {
      std::string key;
      std::string sec;
      int gi = -1;
      int mi = -1;
      std::function<std::string()> label;
      float sx = 0.0F, sy = 0.0F; // Origin of the input area on the virtual screen.
      bool group = false;
      // Rectangle on the virtual screen, used for drop targets.
      float rx = 0.0F, ry = 0.0F, rw = 0.0F, rh = 0.0F;
      bool shown = false;
    };
    struct Mod {
      std::unique_ptr<Widget> widget;
      Node* outer = nullptr;
      InputArea* overlay = nullptr; // Workspaces, tray and taskbar take their own input, so picking sits above.
      Box* outline = nullptr;
      json spec;
      std::unique_ptr<Target> target;
    };
    struct Grp {
      json norm;
      bool bare = true;
      Node* shell = nullptr;
      InputArea* area = nullptr; // Covers the whole island, including the space between modules.
      std::unique_ptr<KusanagiBox> box;
      Node* boxNode = nullptr;
      Node* content = nullptr;
      Node* overlays = nullptr;
      Box* outline = nullptr;
      std::vector<Mod> mods;
      std::unique_ptr<Target> target;
      float along = 0.0F;
    };
    struct Sec {
      std::string name;
      Node* node = nullptr;
      std::vector<Grp> groups;
      float len = 0.0F;
    };
    std::array<Sec, 3> secs;

    Target* pressed = nullptr;
    float pressX = 0.0F, pressY = 0.0F; // Item coordinates.
    bool dragging = false;
    bool suppressClick = false;
    std::string dragKey;
    float dragAtX = 0.0F, dragAtY = 0.0F;
    struct Drop {
      std::string key;
      bool after = true;
      float x = 0, y = 0, w = 0, h = 0;
    };
    std::optional<Drop> drop;
    Box* indicator = nullptr;
    Box* ghost = nullptr;
    Label* ghostLabel = nullptr;
    float indX = 0.0F, indY = 0.0F;
    bool indPlaced = false;

    std::unique_ptr<::Timer> tick;
    bool needWidgets = true; // A widget asked to update (hover, new data) or the timer ticked.

    Impl(BarPreview& s) : self(s) {}

    ~Impl() { clear(); }

    void clear() {
      pressed = nullptr;
      dragging = false;
      // Widgets go first, while the nodes they point at still exist.
      for (auto& sec : secs) {
        for (auto& g : sec.groups)
          for (auto& m : g.mods) m.widget.reset();
        sec.groups.clear();
        sec.node = nullptr;
      }
      if (screen != nullptr) self.removeChild(screen);
      screen = nullptr;
      desktop = nullptr;
      barNode = nullptr;
      barBoxNode = nullptr;
      barBox.reset();
    }

    [[nodiscard]] std::string lookKey() const {
      const Color a = resolved(accent()), a2 = resolved(accent2()), bg = resolved(bgPanel()), t = resolved(textA());
      char buf[160];
      std::snprintf(buf, sizeof(buf), "%.3f%.3f%.3f|%.3f%.3f%.3f|%.3f%.3f%.3f|%.3f%.3f%.3f|", a.r, a.g, a.b, a2.r, a2.g, a2.b,
                    bg.r, bg.g, bg.b, t.r, t.g, t.b);
      return buf + kusanagi::font();
    }

    void normalise() {
      b = merged(json{{"position", "top"}, {"screen", ""}, {"size", 28}, {"length", 0}, {"align", "center"},
                      {"padding", 0}, {"spacing", 0}, {"bg", "transparent"}, {"border", ""}, {"borderWidth", 0},
                      {"radius", 0}, {"line", nullptr}, {"fg", "text"}},
                 raw);
      edge = str(b, "position", "top");
      vertical = edge == "left" || edge == "right";
      size = static_cast<float>(num(b, "size", 28));
      margin = triple(raw.value("margin", json()));
      pad = static_cast<float>(num(b, "padding", 0));
      spacing = static_cast<float>(num(b, "spacing", 0));
    }

    [[nodiscard]] wl_output* output() const {
      const WidgetFactory* f = WidgetFactory::current();
      if (f == nullptr) return nullptr;
      auto& w = f->platform().wayland();
      if (wl_output* o = w.lastPointerOutput(); o != nullptr) return o;
      return w.outputs().empty() ? nullptr : w.outputs().front().output;
    }

    std::unique_ptr<Widget> makeWidget(const std::string& type, const json& spec) {
      const WidgetFactory* f = WidgetFactory::current();
      if (f == nullptr) return nullptr;
      std::unique_ptr<Widget> w;
      if (type == "workspaces") {
        w = std::make_unique<KusanagiWorkspacesWidget>(f->platform(), output(), spec.dump());
      } else if (type == "tray" || type == "taskbar") {
        w = f->create(type, output(), 1.0F, edge, "kusanagi-preview", spacing, false);
      } else {
        w = std::make_unique<KusanagiModuleWidget>(f->kusanagiServices(), spec.dump());
      }
      return w;
    }

    void wire(InputArea* area, Target* t) {
      area->setCursorShape(kPointer);
      area->setAcceptedButtons(InputArea::buttonMask({BTN_LEFT, BTN_RIGHT, BTN_MIDDLE}));
      area->setOnPress([this, t](const InputArea::PointerData& d) {
        if (d.pressed) {
          pressed = t;
          dragging = false;
          suppressClick = false;
          toItem(t, d.localX, d.localY, pressX, pressY);
          return;
        }
        if (dragging) {
          suppressClick = true;
          dragEnd();
        }
        pressed = nullptr;
      });
      area->setOnMotion([this, t](const InputArea::PointerData& d) {
        if (pressed != t) return;
        float x = 0.0F, y = 0.0F;
        toItem(t, d.localX, d.localY, x, y);
        if (!dragging) {
          if (std::hypot(x - pressX, y - pressY) < kDragThreshold) return;
          dragging = true;
          dragBegin(*t);
        }
        dragMove(x, y);
      });
      area->setOnCancel([this]() {
        if (dragging) {
          dragKey.clear();
          drop.reset();
          dragging = false;
          sp::requestLayout();
        }
        pressed = nullptr;
      });
      area->setOnClick([this, t](const InputArea::PointerData&) {
        if (suppressClick) {
          suppressClick = false;
          return;
        }
        if (picked) {
          // The pick can trigger a rebuild that destroys the target, so copy its fields first.
          const std::string sec = t->sec;
          const int gi = t->gi, mi = t->mi;
          picked(sec, gi, mi);
        }
      });
    }

    void widgetsChanged() {
      needWidgets = true;
      sp::requestLayout();
    }

    void toItem(const Target* t, float lx, float ly, float& x, float& y) const {
      x = screenX + (t->sx + lx) * k;
      y = screenY + (t->sy + ly) * k;
    }

    void build(Renderer& renderer) {
      (void)renderer;
      clear();
      normalise();
      look = lookKey();

      auto screenNode = ui::node({});
      screen = screenNode.get();
      self.insertChildAt(0, std::move(screenNode));
      desktop = static_cast<Box*>(screen->addChild(ui::box({})));
      desktop->setVisible(opts.desktop);
      barNode = screen->addChild(ui::node({}));
      barBox = std::make_unique<KusanagiBox>();
      barBoxNode = barNode->addChild(barBox->create());

      // Same per-module values config/kusanagi_import.cpp hands the real bar's widgets.
      const json s = kusanagi::settings();
      const double barFontSize = s.value("bar", json::object()).value("fontSize", 11.0);
      const std::string clock = s.value("bar", json::object()).value("clock", std::string("HH:mm"));
      const json groupDefaults = raw.value("group", json::object());
      const json moduleDefaults = raw.value("module", json::object());
      const json groupBase = {{"bg", "transparent"}, {"border", ""}, {"borderWidth", 0}, {"radius", 0},
                              {"padding", json::array({0, 0})}, {"gap", json::array({0, 0})}, {"inset", json::array({0, 0})},
                              {"spacing", 0}, {"capStart", "none"}, {"capEnd", "none"}, {"opacity", 1}, {"line", nullptr}};
      const double fontSize = b["fontSize"].is_number() && b["fontSize"].get<double>() > 0 ? b["fontSize"].get<double>() : barFontSize;
      const std::string font = str(b, "font").empty() ? kusanagi::font() : str(b, "font");

      const std::array<const char*, 3> names{"start", "center", "end"};
      for (std::size_t si = 0; si < 3; ++si) {
        Sec& sec = secs[si];
        sec.name = names[si];
        sec.node = barNode->addChild(ui::node({}));
        const json entries = raw.value(sec.name, json::array());
        if (!entries.is_array()) continue;
        int gi = 0;
        for (const auto& rawEntry : entries) {
          const int myGi = gi++;
          const json entry = rawEntry.is_string() ? json{{"type", rawEntry}} : rawEntry;
          Grp g;
          if (!entry.is_object()) {
            sec.groups.push_back(std::move(g));
            continue;
          }
          const bool isGroup = str(entry, "type") == "group";
          g.bare = !isGroup;
          json norm = groupBase;
          if (isGroup) {
            for (const json* src : {&groupDefaults, &entry})
              for (auto it = src->begin(); it != src->end(); ++it)
                if (it.key() != "modules" && it.key() != "module") norm[it.key()] = it.value();
          }
          for (const char* key : {"padding", "gap", "inset"}) norm[key] = pairOf(norm[key], 0, 0);
          if (!isGroup) norm["inset"] = pairOf(entry.contains("groupInset") ? entry["groupInset"] : groupDefaults.value("inset", json()), 0, 0);
          g.norm = norm;

          g.shell = sec.node->addChild(ui::node({}));
          g.shell->setOpacity(std::clamp(static_cast<float>(num(norm, "opacity", 1)), 0.0F, 1.0F));
          if (opts.pickable && isGroup) {
            auto area = ui::inputArea({});
            g.area = static_cast<InputArea*>(g.shell->addChild(std::move(area)));
            g.target = std::make_unique<Target>();
            g.target->key = sec.name + ":" + std::to_string(myGi) + ":-1";
            g.target->sec = sec.name;
            g.target->gi = myGi;
            g.target->mi = -1;
            g.target->group = true;
            g.target->label = []() { return std::string("group"); };
            wire(g.area, g.target.get());
          }
          g.box = std::make_unique<KusanagiBox>();
          g.boxNode = g.shell->addChild(g.box->create());
          g.boxNode->setHitTestVisible(false);
          g.content = g.shell->addChild(ui::node({}));
          g.overlays = g.shell->addChild(ui::node({}));

          json moduleDefs = moduleDefaults;
          if (isGroup && entry.contains("module") && entry["module"].is_object()) moduleDefs = merged(moduleDefs, entry["module"]);
          const json modules = isGroup ? entry.value("modules", json::array()) : json::array({entry});
          int mi = 0;
          for (const auto& rawModule : modules) {
            const int myMi = mi++;
            const json mod = rawModule.is_string() ? json{{"type", rawModule}} : rawModule;
            if (!mod.is_object()) continue;
            json spec = merged(moduleDefs, mod);
            if (spec.contains("formatAlt")) spec["when"]["alt"]["format"] = spec["formatAlt"];
            if (spec.value("show", true) == false) continue;
            const std::string type = str(spec, "type", "text");
            spec["_groupInset"] = norm["inset"];
            spec["_edge"] = edge;
            spec["_barFontSize"] = fontSize;
            if (type != "workspaces") {
              spec["_barFg"] = str(b, "fg", "text");
              spec["_barFont"] = str(b, "font");
              spec["_clock"] = clock;
              spec["padding"] = pairOf(spec.value("padding", json()), 10, 10);
            }
            // The preview never opens the media card.
            if (type == "media") spec["popup"] = false;
            Mod m;
            m.spec = spec;
            m.widget = makeWidget(type, spec);
            if (m.widget == nullptr) continue;
            m.widget->setLabelFontFamily(font);
            m.widget->setLabelFontWeight(FontWeight::Normal);
            m.widget->setUpdateCallback([this]() { widgetsChanged(); });
            m.widget->setRedrawCallback([this]() { widgetsChanged(); });
            m.widget->setFrameTickRequestCallback([this]() { widgetsChanged(); });
            m.widget->create();
            m.outer = g.content->addChild(m.widget->releaseRoot());
            if (opts.pickable) {
              m.target = std::make_unique<Target>();
              m.target->key = sec.name + ":" + std::to_string(myGi) + ":" + std::to_string(isGroup ? myMi : -1);
              m.target->sec = sec.name;
              m.target->gi = myGi;
              m.target->mi = isGroup ? myMi : -1;
              Widget* w = m.widget.get();
              m.target->label = [w, type]() {
                // Drag label: the module's text without markup, or its type.
                std::string text;
                if (auto* root = w->root(); root != nullptr) {
                  for (const auto& c : root->children())
                    if (auto* l = dynamic_cast<Label*>(c.get()); l != nullptr && !l->text().empty()) {
                      text = stripTags(std::string(l->text()));
                      break;
                    }
                }
                return text.empty() ? type : text;
              };
              auto* own = dynamic_cast<KusanagiModuleWidget*>(m.widget.get()) != nullptr ? dynamic_cast<InputArea*>(w->root()) : nullptr;
              if (own == nullptr) {
                m.overlay = static_cast<InputArea*>(g.overlays->addChild(ui::inputArea({})));
                own = m.overlay;
              }
              wire(own, m.target.get());
              m.outline = static_cast<Box*>(g.overlays->addChild(ui::box({})));
              m.outline->setFill(clearColorSpec());
              m.outline->setBorder(accent(), 2.0F);
              m.outline->setRadius(4.0F);
              m.outline->setHitTestVisible(false);
            }
            g.mods.push_back(std::move(m));
          }
          if (opts.pickable && isGroup) {
            g.outline = static_cast<Box*>(g.overlays->addChild(ui::box({})));
            g.outline->setFill(clearColorSpec());
            g.outline->setBorder(accent(), 2.0F);
            g.outline->setRadius(6.0F);
            g.outline->setHitTestVisible(false);
          }
          sec.groups.push_back(std::move(g));
        }
      }

      if (opts.pickable && indicator == nullptr) {
        indicator = static_cast<Box*>(self.addChild(ui::box({})));
        indicator->setFill(accent());
        indicator->setRadius(1.5F);
        indicator->setHitTestVisible(false);
        ghost = static_cast<Box*>(self.addChild(ui::box({})));
        ghost->setFill(accent());
        ghost->setRadius(11.0F);
        ghost->setHitTestVisible(false);
        ghostLabel = static_cast<Label*>(ghost->addChild(makeText("", 11.0F, true, bgPanel())));
        ghostLabel->setMaxLines(1);
      }
    }

    void layout(Renderer& renderer, float width, float height) {
      AnimationManager* anims = self.animationManager();
      const float fullAlong = vertical ? opts.screenH : (opts.screenW ? opts.screenW(width) : std::max(800.0F, std::round(width / 0.62F)));
      // Widgets size themselves from the bar's thickness; the length only tells them the orientation.
      const float areaW = vertical ? size : fullAlong;
      const float areaH = vertical ? fullAlong : size;
      const bool widgets = std::exchange(needWidgets, false);
      for (auto& sec : secs)
        for (auto& g : sec.groups)
          for (auto& m : g.mods) {
            if (!widgets) break;
            if (anims != nullptr) m.widget->setAnimationManager(anims);
            if (auto* tray = dynamic_cast<TrayWidget*>(m.widget.get())) tray->setCapsuleCross(std::max(1.0F, std::round(size * 0.76F)));
            m.widget->update(renderer);
            if (m.widget->root() != nullptr) m.widget->layout(renderer, areaW, areaH);
          }

      const std::string sel = selKeyFn ? selKeyFn() : std::string();
      // Along the bar each group is: gap, cap, padding, modules, padding, cap, gap.
      for (auto& sec : secs) {
        float secLen = 0.0F;
        int shownGroups = 0;
        for (auto& g : sec.groups) {
          if (g.shell == nullptr) continue;
          const json& n = g.norm;
          const float i0 = at(n["inset"], 0), i1 = at(n["inset"], 1);
          const float gap0 = at(n["gap"], 0), gap1 = at(n["gap"], 1);
          const float pad0 = at(n["padding"], 0), pad1 = at(n["padding"], 1);
          const float gsp = static_cast<float>(num(n, "spacing", 0));
          const float innerCross = size - i0 - i1;
          const float capS = KusanagiBox::capSize(str(n, "capStart", "none"), innerCross);
          const float capE = KusanagiBox::capSize(str(n, "capEnd", "none"), innerCross);
          const float lead = gap0 + capS + pad0;
          float mods = 0.0F;
          int shownMods = 0;
          for (auto& m : g.mods) {
            const bool on = m.outer != nullptr && m.outer->visible() && m.widget->root() != nullptr && m.widget->root()->visible();
            if (m.overlay != nullptr) m.overlay->setVisible(on);
            if (!on) {
              if (m.outline != nullptr) m.outline->setVisible(false);
              if (m.target) m.target->shown = false;
              continue;
            }
            const float len = vertical ? m.outer->height() : m.outer->width();
            if (shownMods > 0) mods += gsp;
            m.outer->setPosition(vertical ? 0.0F : mods, vertical ? mods : 0.0F);
            m.outer->setOpacity(dragging && dragKey == (m.target ? m.target->key : "") ? 0.35F : 1.0F);
            // The module's box: its gap along the bar, its group's inset plus its own across it.
            const json mg = pairOf(m.spec.value("gap", json()), 2, 2);
            const json mins = pairOf(m.spec.value("inset", json()), 0, 0);
            const float e = i0 + at(mins, 0), in = i1 + at(mins, 1);
            const bool far = edge == "bottom" || edge == "right";
            const float c0 = far ? in : e;
            const float boxAlong = len - at(mg, 0) - at(mg, 1);
            const float bx = vertical ? c0 : lead + mods + at(mg, 0);
            const float by = vertical ? lead + mods + at(mg, 0) : c0;
            const float bw = vertical ? size - e - in : boxAlong;
            const float bh = vertical ? boxAlong : size - e - in;
            if (m.overlay != nullptr) {
              m.overlay->setPosition(bx, by);
              m.overlay->setSize(std::max(0.0F, bw), std::max(0.0F, bh));
            }
            if (m.target) {
              m.target->shown = true;
              m.target->rx = bx;
              m.target->ry = by;
              m.target->rw = bw;
              m.target->rh = bh;
              // Handlers sit on the overlay (the box) or on the widget's own root (the whole module).
              m.target->sx = m.overlay != nullptr ? bx : (vertical ? 0.0F : lead + mods);
              m.target->sy = m.overlay != nullptr ? by : (vertical ? lead + mods : 0.0F);
            }
            if (m.outline != nullptr) {
              m.outline->setVisible(m.target && sel == m.target->key);
              m.outline->setPosition(bx - 1.0F, by - 1.0F);
              m.outline->setSize(bw + 2.0F, bh + 2.0F);
            }
            mods += len;
            ++shownMods;
          }
          // Matches the classic look: an island grows one spacing past its last module.
          if (shownMods > 0) mods += gsp;
          const bool any = mods > 0.0F;
          g.along = any ? gap0 + capS + pad0 + mods + pad1 + capE + gap1 : 0.0F;
          g.shell->setVisible(any);
          g.shell->setSize(vertical ? size : g.along, vertical ? g.along : size);
          g.content->setPosition(vertical ? 0.0F : lead, vertical ? lead : 0.0F);
          const float c0 = edge == "bottom" || edge == "right" ? i1 : i0;
          const float boxAlong = std::max(0.0F, g.along - gap0 - gap1);
          const float bx = vertical ? c0 : gap0, by = vertical ? gap0 : c0;
          const float bw = vertical ? innerCross : boxAlong, bh = vertical ? boxAlong : innerCross;
          const bool drawn = !g.bare && KusanagiBox::draws(n);
          g.boxNode->setVisible(drawn && any);
          if (drawn && any) g.box->apply(n, false, vertical, bx, by, bw, bh, 1.0F);
          if (g.area != nullptr) {
            g.area->setPosition(bx, by);
            g.area->setSize(bw, bh);
            g.area->setVisible(any);
          }
          if (g.target) {
            g.target->shown = any;
            g.target->sx = bx;
            g.target->sy = by;
            g.target->rx = bx;
            g.target->ry = by;
            g.target->rw = bw;
            g.target->rh = bh;
          }
          if (g.outline != nullptr) {
            g.outline->setVisible(any && g.target && sel == g.target->key);
            g.outline->setPosition(bx - 2.0F, by - 2.0F);
            g.outline->setSize(bw + 4.0F, bh + 4.0F);
          }
          if (!any) continue;
          if (shownGroups > 0) secLen += spacing;
          g.shell->setPosition(vertical ? 0.0F : secLen, vertical ? secLen : 0.0F);
          secLen += g.along;
          ++shownGroups;
        }
        if (shownGroups > 0) secLen += spacing; // Same trailing spacing for sections.
        sec.len = secLen;
      }

      // Natural length of the sections, used for an "auto" length bar.
      float natural = 0.0F;
      int counted = 0;
      for (auto& sec : secs)
        if (sec.len > 0.0F) {
          natural += sec.len;
          ++counted;
        }
      natural += 2.0F * pad + static_cast<float>(std::max(0, counted - 1)) * std::max(spacing, 8.0F);

      const float thick = size + margin[0] + margin[1];
      const float viewW = vertical ? std::max(thick + 60.0F, opts.screenH * width / std::max(1.0F, height)) : fullAlong;
      const float viewH = vertical ? opts.screenH : thick + 16.0F;
      k = opts.fixedScale > 0.0F ? opts.fixedScale : std::min(width / viewW, height / viewH);
      screenX = opts.fixedScale > 0.0F ? 0.0F : std::round((width - viewW * k) / 2.0F);
      screenY = opts.fixedScale > 0.0F ? 0.0F : std::round((height - viewH * k) / 2.0F);
      // Scaling happens about the centre (hit testing ignores a transform origin), so offset the node so
      // its top-left lands on (screenX, screenY).
      screen->setSize(viewW, viewH);
      screen->setScale(k);
      screen->setPosition(screenX - viewW * (1.0F - k) / 2.0F, screenY - viewH * (1.0F - k) / 2.0F);

      // Backdrop: darkened accent at the top fading to darkened accent2 at the bottom.
      {
        RoundedRectStyle st;
        st.fillMode = FillMode::LinearGradient;
        st.gradientDirection = GradientDirection::Vertical;
        const Color c0 = darker(resolved(accent()), 2.6F), c1 = darker(resolved(accent2()), 3.4F);
        st.gradientStops = {GradientStop{0.0F, c0}, GradientStop{1.0F, c1}, GradientStop{1.0F, c1}, GradientStop{1.0F, c1}};
        st.fill = c0;
        desktop->setStyle(st);
        desktop->setSize(viewW, viewH);
      }

      const json& length = b["length"];
      const bool autoLen = length.is_string() && length.get<std::string>() == "auto";
      const float lv = length.is_number() ? length.get<float>() : 0.0F;
      const std::string align = str(b, "align", "center");
      const float len = autoLen ? std::max(natural, size) : lv <= 0.0F ? fullAlong - 2.0F * margin[2] : lv <= 1.0F ? std::round(fullAlong * lv) : lv;
      const float atAlong = (!autoLen && lv <= 0.0F) || align == "start" ? margin[2]
                            : align == "end"                            ? fullAlong - margin[2] - len
                                                                        : std::round((fullAlong - len) / 2.0F);
      const float bw = vertical ? size : len, bh = vertical ? len : size;
      const float bx = vertical ? (edge == "left" ? margin[0] : viewW - margin[0] - bw) : atAlong;
      const float by = vertical ? atAlong : (edge == "top" ? margin[0] : viewH - margin[0] - bh);
      barNode->setPosition(bx, by);
      barNode->setSize(bw, bh);
      {
        json bl = json::object();
        for (const char* key : {"bg", "border", "borderWidth", "radius", "line"})
          if (b.contains(key)) bl[key] = b[key];
        barBoxNode->setVisible(KusanagiBox::draws(bl));
        if (KusanagiBox::draws(bl)) barBox->apply(bl, false, vertical, 0.0F, 0.0F, bw, bh, 1.0F);
      }
      for (auto& sec : secs) {
        const float room = vertical ? bh : bw;
        const float sa = sec.name == "start" ? pad : sec.name == "center" ? std::round((room - sec.len) / 2.0F) : room - pad - sec.len;
        sec.node->setPosition(vertical ? 0.0F : sa, vertical ? sa : 0.0F);
        sec.node->setSize(vertical ? size : sec.len, vertical ? sec.len : size);
        // Move targets from group-local to virtual screen coordinates.
        for (auto& g : sec.groups) {
          if (g.shell == nullptr) continue;
          const float ox = bx + sec.node->x() + g.shell->x(), oy = by + sec.node->y() + g.shell->y();
          for (Target* t : targetsOf(g)) {
            t->sx += ox;
            t->sy += oy;
            t->rx += ox;
            t->ry += oy;
          }
        }
      }
      layoutDrag(renderer);
    }

    static std::vector<Target*> targetsOf(Grp& g) {
      // Modules come before their island so the module wins a tie on distance.
      std::vector<Target*> out;
      for (auto& m : g.mods)
        if (m.target) out.push_back(m.target.get());
      if (g.target) out.push_back(g.target.get());
      return out;
    }

    // Drag and drop

    void dragBegin(const Target& t) {
      dragKey = t.key;
      if (ghostLabel != nullptr) ghostLabel->setText(t.label ? t.label() : std::string("move"));
      indPlaced = false;
      sp::requestLayout();
    }

    void dragMove(float x, float y) {
      dragAtX = x;
      dragAtY = y;
      drop = hit(x, y);
      sp::requestLayout();
    }

    void dragEnd() {
      const std::string from = dragKey;
      const auto to = drop;
      dragKey.clear();
      drop.reset();
      dragging = false;
      sp::requestLayout();
      if (!from.empty() && to && to->key != from && moved) moved(from, to->key, to->after);
    }

    std::optional<Drop> hit(float px, float py) {
      const std::string& dk = dragKey;
      auto parts = [](const std::string& key) {
        std::array<std::string, 3> p;
        std::size_t i = 0, start = 0;
        for (; i < 3; ++i) {
          const auto c = key.find(':', start);
          p[i] = key.substr(start, c == std::string::npos ? std::string::npos : c - start);
          if (c == std::string::npos) break;
          start = c + 1;
        }
        return p;
      };
      const auto dkp = parts(dk);
      const bool dragGroup = dkp[2] == "-1";
      std::optional<Drop> best;
      float bestD = std::numeric_limits<float>::infinity();
      for (auto& sec : secs)
        for (auto& g : sec.groups)
          for (Target* t : targetsOf(g)) {
            if (!t->shown || t->key == dk) continue;
            const auto kp = parts(t->key);
            // Not into itself, and a group never goes into another group.
            if (kp[0] == dkp[0] && kp[1] == dkp[1] && dkp[2] == "-1") continue;
            if (dragGroup && kp[2] != "-1") continue;
            const float rx = screenX + t->rx * k, ry = screenY + t->ry * k, rw = t->rw * k, rh = t->rh * k;
            const float along = vertical ? py : px, lo = vertical ? ry : rx, l = vertical ? rh : rw;
            const float d = along < lo ? lo - along : along > lo + l ? along - lo - l : 0.0F;
            if (d < bestD) {
              bestD = d;
              best = Drop{t->key, along > lo + l / 2.0F, rx, ry, rw, rh};
            }
          }
      // Far from everything: append to the section under the pointer.
      if (!best || bestD > 60.0F) {
        const float w = self.width(), h = self.height();
        const float f = vertical ? py / h : px / w;
        const std::string s = f < 0.34F ? "start" : f > 0.66F ? "end" : "center";
        const float pos = s == "start" ? 0.02F : s == "center" ? 0.5F : 0.98F;
        return Drop{"section:" + s, true, vertical ? 0.0F : w * pos, vertical ? h * pos : 0.0F, 0.0F, vertical ? 0.0F : h};
      }
      return best;
    }

    void layoutDrag(Renderer& renderer) {
      if (indicator == nullptr) return;
      const bool on = !dragKey.empty();
      indicator->setVisible(on && drop.has_value());
      ghost->setVisible(on);
      if (!on) return;
      if (drop) {
        const Drop& t = *drop;
        const float w = vertical ? std::max(t.w, 20.0F) : 3.0F;
        const float h = vertical ? 3.0F : std::max(t.h, 14.0F);
        const float tx = vertical ? t.x : (t.after ? t.x + t.w : t.x) - 1.5F;
        const float ty = vertical ? (t.after ? t.y + t.h : t.y) - 1.5F : t.y;
        indicator->setSize(w, h);
        AnimationManager* anims = self.animationManager();
        if (!indPlaced || anims == nullptr) {
          indX = tx;
          indY = ty;
          indPlaced = true;
        } else if (std::abs(tx - indTargetX) > 0.1F || std::abs(ty - indTargetY) > 0.1F) {
          anims->cancelForOwner(indicator);
          const float fx = indX, fy = indY;
          anims->animate(
              0.0F, 1.0F, 90.0F, Easing::EaseOutCubic,
              [this, fx, fy, tx, ty](float p) {
                indX = fx + (tx - fx) * p;
                indY = fy + (ty - fy) * p;
                if (indicator != nullptr) indicator->setPosition(indX, indY);
              },
              {}, indicator
          );
        }
        indTargetX = tx;
        indTargetY = ty;
        indicator->setPosition(indX, indY);
      }
      ghostLabel->measure(renderer);
      const float gw = ghostLabel->width() + 16.0F, gh = 22.0F;
      ghost->setSize(gw, gh);
      ghost->setPosition(dragAtX + 10.0F, dragAtY - gh - 4.0F);
      ghostLabel->setPosition(std::round((gw - ghostLabel->width()) / 2.0F), std::round((gh - ghostLabel->height()) / 2.0F));
    }
    float indTargetX = -1e9F, indTargetY = -1e9F;
  };

  BarPreview::BarPreview(std::function<json()> bar, std::function<float(float)> height, BarPreviewOpts opts)
      : m(std::make_unique<Impl>(*this)) {
    m->barFn = std::move(bar);
    m->heightFn = std::move(height);
    m->opts = std::move(opts);
    m->raw = m->barFn ? m->barFn() : json::object();
    if (!m->raw.is_object()) m->raw = json::object();
    setClipChildren(true);
    if (!m->opts.snapshot) {
      // Live data (clock, stats, media): relayout every second, which updates every widget.
      m->tick = std::make_unique<::Timer>();
      m->tick->startRepeating(std::chrono::milliseconds(1000), [impl = m.get()]() { impl->widgetsChanged(); });
    }
  }

  BarPreview::~BarPreview() = default;

  BarPreview* BarPreview::onPicked(std::function<void(const std::string&, int, int)> fn) {
    m->picked = std::move(fn);
    return this;
  }
  BarPreview* BarPreview::onMoved(std::function<void(const std::string&, const std::string&, bool)> fn) {
    m->moved = std::move(fn);
    return this;
  }
  BarPreview* BarPreview::selKey(std::function<std::string()> fn) {
    m->selKeyFn = std::move(fn);
    return this;
  }

  void BarPreview::sync() {
    json next = m->barFn ? m->barFn() : json::object();
    if (!next.is_object()) next = json::object();
    // A rebuild mid-drag would lose the pressed module, so wait for the drop.
    if ((next != m->raw || m->lookKey() != m->look) && m->pressed == nullptr) {
      m->raw = std::move(next);
      m->dirty = true;
    }
    requestLayout();
  }

  float BarPreview::place(Renderer& renderer, float width) {
    width = widthFor(width);
    const float h = m->heightFn ? m->heightFn(width) : 60.0F;
    setSize(width, h);
    if (m->dirty || m->screen == nullptr || width != m->builtW) {
      // Snapshot cards are rebuilt only for a new spec, look or width.
      const bool rebuild = m->dirty || m->screen == nullptr || m->opts.snapshot;
      m->dirty = false;
      m->builtW = width;
      if (rebuild) m->build(renderer);
      m->needWidgets = true;
    } else if (m->opts.snapshot) {
      return h;
    }
    m->layout(renderer, width, h);
    return h;
  }

} // namespace kusanagi::sp
