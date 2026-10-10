// Settings > Presets: apply a whole look in one click, save your own, and share looks as files or text.
// An imported look is shown first, with any commands it would run, and only applied from there.
// The presets themselves live in shell/kusanagi/presets.h.

#include "core/deferred_call.h"
#include "core/process/process.h"
#include "core/timer_manager.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/core/render_styles.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/presets.h"
#include "shell/kusanagi/settings/bar_preview.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>

namespace kusanagi::sp {

  namespace {

    constexpr auto kPointer = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;

    std::string idOf(const json& p) {
      if (!p.is_object()) return {};
      const json id = p.value("id", json());
      if (truthy(id) && id.is_string()) return id.get<std::string>();
      const json n = p.value("name", json());
      return n.is_string() ? n.get<std::string>() : std::string();
    }

    std::string str(const json& o, const char* key, std::string fallback = {}) {
      return o.is_object() && o.contains(key) && o[key].is_string() ? o[key].get<std::string>() : fallback;
    }

    // Number at `key`, or `fallback` if missing (or zero, with zeroIsUnset).
    double num(const json& o, const char* key, double fallback, bool zeroIsUnset = false) {
      if (!o.is_object() || !o.contains(key) || !o[key].is_number()) return fallback;
      const double v = o[key].get<double>();
      return zeroIsUnset && v == 0.0 ? fallback : v;
    }

    std::string hexOf(const Color& c) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", static_cast<int>(std::lround(std::clamp(c.r, 0.0F, 1.0F) * 255.0F)),
                    static_cast<int>(std::lround(std::clamp(c.g, 0.0F, 1.0F) * 255.0F)),
                    static_cast<int>(std::lround(std::clamp(c.b, 0.0F, 1.0F) * 255.0F)));
      return buf;
    }

    // The bar a layout preset's card shows: its first bar, or its dock.
    json cardBar(const json& bars) {
      if (bars.size() > 1 && bars[1].is_object() && bars[1].value("length", json()) == "auto") return bars[1];
      return bars[0];
    }

    // Small drawing of a classic preset's bar (style, height, roundness, workspaces, layout).
    class PresetPreview : public Item {
    public:
      explicit PresetPreview(json preset) : m_preset(std::move(preset)) {
        m_desk = static_cast<Box*>(addChild(ui::box({})));
        m_clip = addChild(ui::node({}));
        m_clip->setClipChildren(true);
        m_strip = m_clip->addChild(ui::node({}));
        m_barBg = static_cast<Box*>(m_strip->addChild(ui::box({})));
        m_wsIsle = static_cast<Box*>(m_strip->addChild(ui::box({})));
        for (int i = 0; i < 5; ++i) {
          Ws w;
          w.shape = static_cast<Box*>(m_wsIsle->addChild(ui::box({})));
          w.text = static_cast<Label*>(m_wsIsle->addChild(makeText("", 7.0F)));
          w.text->setMaxLines(1);
          m_ws.push_back(w);
        }
        m_title = static_cast<Label*>(m_strip->addChild(makeText("foot — ~/kusanagi", 7.0F, false, textA(0.8F))));
        m_title->setMaxLines(1);
        m_clockIsle = static_cast<Box*>(m_strip->addChild(ui::box({})));
        m_clock = static_cast<Label*>(m_clockIsle->addChild(makeText("20:31", 7.0F, true)));
        m_statIsle = static_cast<Box*>(m_strip->addChild(ui::box({})));
        m_stat = static_cast<Label*>(m_statIsle->addChild(makeText("", 7.0F)));
        m_stat->setMaxLines(1);
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float H = 62.0F;
        setSize(width, H);
        const json b = m_preset.value("bar", json::object());
        const json w = m_preset.value("workspaces", json::object());
        const float k = 0.62F;
        const float h = std::round(static_cast<float>(num(b, "height", 28.0, true)) * k);
        const bool atBottom = str(b, "position") == "bottom";
        const bool centered = str(b, "layout") == "centered";
        const float r = static_cast<float>(num(b, "radius", 10.0)) * k;
        const std::string style = str(b, "style");
        const ColorSpec barBg = bgPanel(std::max(0.35F, static_cast<float>(num(b, "opacity", 0.5))));
        const bool outline = b.is_object() && b.contains("outline") && truthy(b["outline"]);

        // Backdrop
        RoundedRectStyle desk;
        desk.fillMode = FillMode::LinearGradient;
        desk.gradientDirection = GradientDirection::Vertical;
        Color c0 = resolved(accent(0.22F));
        Color c1 = resolved(accent2(0.08F));
        desk.gradientStops = {GradientStop{0.0F, c0}, GradientStop{1.0F, c1}, GradientStop{1.0F, c1}, GradientStop{1.0F, c1}};
        desk.fill = c0;
        m_desk->setStyle(desk);
        m_desk->setSize(width, H);
        m_clip->setSize(width, H);

        m_strip->setPosition(0.0F, atBottom ? H - h : 0.0F);
        m_strip->setSize(width, h);

        // Solid or floating background
        const bool fl = style == "floating";
        m_barBg->setVisible(style == "solid" || fl);
        m_barBg->setPosition(fl ? 4.0F : 0.0F, fl ? 2.0F : 0.0F);
        m_barBg->setSize(width - (fl ? 8.0F : 0.0F), h - (fl ? 4.0F : 0.0F));
        m_barBg->setRadius(fl ? r : 0.0F);
        m_barBg->setFill(barBg);

        const auto isle = [&](Box* box) {
          box->setFill(style == "islands" ? barBg : textA(0.0F));
          box->setBorder(textA(0.15F), outline ? 1.0F : 0.0F);
          box->setRadius(r);
        };

        // Workspaces
        const std::string st = str(w, "style").empty() ? "pills" : str(w, "style");
        const bool dwl = st == "dwl";
        const bool dots = st == "pills" || st == "dots";
        const bool flush = dwl && !centered;
        const float isleH = flush ? h : h - 5.0F;
        float rowW = 0.0F;
        std::array<float, 5> ws{};
        for (int i = 0; i < 5; ++i) {
          const bool act = i == 1;
          ws[static_cast<std::size_t>(i)] = dwl ? h
                                           : st == "pills" && act ? 15.0F
                                           : (st == "numbers" || st == "roman" || st == "kanji" || st == "custom") ? 9.0F
                                                                                                                       : 6.0F;
          rowW += ws[static_cast<std::size_t>(i)] + (i > 0 ? (dwl ? 0.0F : 3.0F) : 0.0F);
        }
        isle(m_wsIsle);
        const float wsW = rowW + (flush ? 0.0F : 10.0F);
        m_wsIsle->setRadius(flush ? 0.0F : r);
        m_wsIsle->setSize(wsW, isleH);
        m_wsIsle->setPosition(flush ? 0.0F : centered ? (width - wsW) / 2.0F : 4.0F, flush ? 0.0F : 2.5F);
        static const std::array<const char*, 5> roman{"I", "II", "III", "IV", "V"};
        static const std::array<const char*, 5> kanji{"一", "二", "三", "四", "五"};
        float x = (wsW - rowW) / 2.0F;
        for (int i = 0; i < 5; ++i) {
          Ws& it = m_ws[static_cast<std::size_t>(i)];
          const bool act = i == 1;
          const bool occ = i == 0 || i == 2;
          const float iw = ws[static_cast<std::size_t>(i)];
          const float ih = dwl ? isleH : 9.0F;
          const float iy = (isleH - ih) / 2.0F;
          if (dots) {
            const float sh = st == "pills" && act ? 8.0F : 6.0F;
            it.shape->setVisible(true);
            it.shape->setSize(iw, sh);
            it.shape->setPosition(x, iy + (ih - sh) / 2.0F);
            it.shape->setRadius(sh / 2.0F);
            it.shape->setFill(act ? accent() : occ ? textA(0.85F) : textA(0.0F));
            it.shape->setBorder(textA(0.35F), act || occ ? 0.0F : 1.0F);
          } else if (dwl) {
            it.shape->setVisible(true);
            it.shape->setSize(iw, ih);
            it.shape->setPosition(x, iy);
            it.shape->setRadius(0.0F);
            it.shape->setFill(act ? accent() : textA(0.0F));
            it.shape->setBorder(textA(0.0F), 0.0F);
          } else {
            it.shape->setVisible(false);
          }
          it.text->setVisible(!dots);
          if (!dots) {
            it.text->setText(st == "roman" ? roman[static_cast<std::size_t>(i)]
                             : st == "kanji" ? kanji[static_cast<std::size_t>(i)]
                                             : std::to_string(i + 1));
            it.text->setColor(dwl && act ? bgPanel() : act ? accent() : textA(occ ? 0.85F : 0.4F));
            it.text->measure(renderer);
            it.text->setPosition(x + (iw - it.text->width()) / 2.0F, iy + (ih - it.text->height()) / 2.0F);
          }
          x += iw + (dwl ? 0.0F : 3.0F);
        }

        // Window title (dwl and text-only looks)
        const json mods = b.value("modules", json::object());
        const bool title = mods.is_object() && mods.contains("title") && truthy(mods["title"]) && !centered;
        m_title->setVisible(title);
        if (title) {
          m_title->measure(renderer);
          m_title->setPosition(m_wsIsle->x() + wsW + 6.0F, (h - m_title->height()) / 2.0F);
        }

        // Clock
        const bool bold = !(b.is_object() && b.contains("clockBold") && b["clockBold"] == false);
        m_clock->setFontWeight(bold ? FontWeight::Bold : FontWeight::Normal);
        m_clock->measure(renderer);
        isle(m_clockIsle);
        const float cw = m_clock->width() + 12.0F;
        m_clockIsle->setSize(cw, h - 5.0F);
        m_clockIsle->setPosition(centered ? 4.0F : (width - cw) / 2.0F, 2.5F);
        m_clock->setPosition((cw - m_clock->width()) / 2.0F, (h - 5.0F - m_clock->height()) / 2.0F);

        // Status. Single spaces, since the real bar collapses double spaces too.
        const bool accentLabels = b.is_object() && b.contains("accentLabels") && truthy(b["accentLabels"]);
        if (accentLabels) {
          const std::string a = hexOf(resolved(accent()));
          m_stat->setUseMarkup(true);
          m_stat->setText("<span foreground=\"" + a + "\">CPU</span> 4% <span foreground=\"" + a + "\">RAM</span> 23%");
        } else {
          m_stat->setUseMarkup(false);
          m_stat->setText("CPU 4% RAM 23%");
        }
        m_stat->measure(renderer);
        isle(m_statIsle);
        const float sw = m_stat->width() + 12.0F;
        m_statIsle->setSize(sw, h - 5.0F);
        m_statIsle->setPosition(width - sw - 4.0F, 2.5F);
        m_stat->setPosition((sw - m_stat->width()) / 2.0F, (h - 5.0F - m_stat->height()) / 2.0F);
        return H;
      }

    private:
      struct Ws {
        Box* shape = nullptr;
        Label* text = nullptr;
      };
      json m_preset;
      Box* m_desk = nullptr;
      Node* m_clip = nullptr;
      Node* m_strip = nullptr;
      Box* m_barBg = nullptr;
      Box* m_wsIsle = nullptr;
      std::vector<Ws> m_ws;
      Label* m_title = nullptr;
      Box* m_clockIsle = nullptr;
      Label* m_clock = nullptr;
      Box* m_statIsle = nullptr;
      Label* m_stat = nullptr;
    };

    // Preview of a layout preset's bar: a snapshot for a card, or a live one in the import box.
    std::unique_ptr<Item> layoutPreview(const json& bar, float height, bool card) {
      if (!card) return std::make_unique<BarPreview>([bar]() { return bar; }, [height](float) { return height; });
      const std::string pos = str(bar, "position");
      const bool vertical = pos == "left" || pos == "right";
      // Same scale as the drawn previews (a narrower virtual screen). Side bars show their top part.
      return std::make_unique<BarPreview>([bar]() { return bar; }, [height](float) { return height; },
                                          BarPreviewOpts{
                                              .snapshot = true,
                                              .screenW = [](float w) { return std::round(w / 0.6F); },
                                              .fixedScale = vertical ? 0.6F : 0.0F,
                                          });
    }


    struct Share {
      std::optional<presets::ReadResult> pending; // Read but not applied yet.
      int pendingGen = 0;
      std::string error;
      std::vector<std::string> found; // .kusanagi files in ~/kusanagi-looks and ~/Downloads.
    };

    // Kept outside the page so it survives reopening it.
    std::string& lastExport() {
      static std::string s;
      return s;
    }

    // Flush pending writes first, since presets read and write settings.json on disk.
    void applyPreset(const json& p) {
      flush();
      presets::apply(p);
      refresh();
    }


    class PresetCard : public Item {
    public:
      PresetCard(json preset, bool own) : m_preset(std::move(preset)), m_own(own) {
        m_bg = static_cast<Box*>(addChild(ui::box({})));
        m_bars = presets::barsOf(m_preset);
        if (m_bars.is_array() && !m_bars.empty()) {
          m_preview = static_cast<Item*>(addChild(layoutPreview(cardBar(m_bars), 62.0F, true)));
        } else {
          m_preview = static_cast<Item*>(addChild(std::make_unique<PresetPreview>(m_preset)));
        }
        m_preview->setHitTestVisible(false); // Just a picture; the card handles the pointer.
        m_name = static_cast<Label*>(addChild(makeText(str(m_preset, "name"), 13.0F, true)));
        m_name->setMaxLines(1);
        m_note = static_cast<Label*>(addChild(makeText(str(m_preset, "note"), 10.0F, false, dim())));
        m_note->setMaxLines(1);
        m_note->setEllipsize(TextEllipsize::End);
        if (m_own) {
          const std::string name = str(m_preset, "name");
          m_remove = static_cast<IconButton*>(addChild(std::make_unique<IconButton>(
              0xf0a7a,
              [name]() {
                // Applying rebuilds the list and this card, so don't do it inside the click handler.
                DeferredCall::callLater([name]() {
                  presets::remove(name);
                  refresh();
                });
              },
              26.0F, 13.0F)));
        }
        setCursorShape(kPointer);
        setOnEnter([this](const PointerData&) { restyle(true); });
        setOnLeave([this]() { restyle(true); });
        setOnPress([this](const PointerData& d) { tweenScale(*this, d.pressed ? 0.98F : 1.0F, 160, 2.0F); });
        setOnCancel([this]() { tweenScale(*this, 1.0F, 160, 2.0F); });
        setOnClick([this](const PointerData&) {
          const json p = m_preset;
          DeferredCall::callLater([p]() { applyPreset(p); });
        });
        sync();
      }

      void sync() override {
        const bool on = presets::lastApplied() == idOf(m_preset);
        if (on != m_on || m_first) {
          m_on = on;
          m_bg->setBorder(on ? accent() : textA(0.08F), on ? 2.0F : 1.0F);
        }
        if (m_first) restyle(false);
        m_first = false;
      }

      void restyle(bool animate) {
        const ColorSpec to = textA(hovered() ? 0.07F : 0.045F);
        if (animate) {
          tweenColor(*m_bg, m_color, to, 140, [this](const ColorSpec& c) { m_bg->setFill(c); });
        } else {
          m_bg->setFill(to);
        }
        m_color = to;
      }

      float place(Renderer& renderer, float width) override {
        // Two cards per row.
        const float w = std::floor((width - 12.0F) / 2.0F);
        const float h = 132.0F;
        setSize(w, h);
        setTransformOrigin(w / 2.0F, h / 2.0F);
        m_bg->setSize(w, h);
        m_bg->setRadius(std::max(8.0F, kusanagi::radius() - 4.0F));
        (void)m_preview->place(renderer, w - 20.0F);
        m_preview->setPosition(10.0F, 10.0F);
        m_name->measure(renderer);
        m_name->setPosition(12.0F, 80.0F);
        m_note->setMaxWidth(std::max(1.0F, w - 24.0F - (m_own ? 30.0F : 0.0F)));
        m_note->measure(renderer);
        m_note->setPosition(12.0F, 80.0F + m_name->height() + 2.0F);
        if (m_remove != nullptr) {
          (void)m_remove->place(renderer, 26.0F);
          m_remove->setPosition(w - 8.0F - 26.0F, h - 8.0F - 26.0F);
        }
        return h;
      }

    private:
      json m_preset;
      json m_bars;
      bool m_own;
      bool m_on = false;
      bool m_first = true;
      Box* m_bg = nullptr;
      Item* m_preview = nullptr;
      Label* m_name = nullptr;
      Label* m_note = nullptr;
      IconButton* m_remove = nullptr;
      ColorSpec m_color;
    };

    // "Your looks", rebuilt when the list in presets.json changes.
    class SavedFlow : public Flow {
    public:
      SavedFlow() : Flow(12.0F) {
        showIf([]() { return !presets::saved().empty(); });
        sync();
      }
      void sync() override {
        const json list = presets::saved();
        const std::string key = list.dump();
        if (key == m_key) return;
        m_key = key;
        for (Item* it : items()) (void)removeChild(it);
        for (const auto& p : list) {
          if (p.is_object()) add<PresetCard>(p, true);
        }
        requestLayout();
      }

    private:
      std::string m_key = "\x01";
    };

    // Looks for .kusanagi files in ~/kusanagi-looks and ~/Downloads, newest first.
    void scan(const std::shared_ptr<Share>& share) {
      std::weak_ptr<Share> weak = share;
      (void)process::runAsync(
          std::vector<std::string>{"sh", "-c", "ls -1t \"$HOME\"/kusanagi-looks/*.kusanagi \"$HOME\"/Downloads/*.kusanagi 2>/dev/null; true"},
          process::RunCallbacks{
              .onExit = [weak](process::RunResult r) {
                std::vector<std::string> files;
                std::istringstream in(r.out);
                for (std::string l; std::getline(in, l);) {
                  if (l.find_first_not_of(" \t\r") != std::string::npos) files.push_back(l);
                }
                DeferredCall::callLater([weak, files = std::move(files)]() {
                  auto s = weak.lock();
                  if (!s) return;
                  s->found = files;
                  refresh();
                });
              },
          });
    }

    void open(const std::shared_ptr<Share>& share, presets::ReadResult r) {
      if (!r.error.empty()) {
        share->error = r.error;
        share->pending.reset();
      } else {
        share->error.clear();
        share->pending = std::move(r);
      }
      ++share->pendingGen;
      refresh();
    }

    // Found files as chips, then "Look again".
    class FoundFlow : public Flow {
    public:
      explicit FoundFlow(std::shared_ptr<Share> share) : Flow(6.0F), m_share(std::move(share)) { sync(); }
      void sync() override {
        if (m_built && m_share->found == m_files) return;
        m_built = true;
        m_files = m_share->found;
        for (Item* it : items()) (void)removeChild(it);
        for (const auto& f : m_files) {
          std::string label = f.substr(f.rfind('/') + 1);
          if (label.ends_with(".kusanagi")) label.resize(label.size() - 9);
          std::weak_ptr<Share> weak = m_share;
          add<Chip>(label, 0xf0214)->onClick([weak, f]() {
            if (auto s = weak.lock()) open(s, presets::readLookFile(f));
          });
        }
        std::weak_ptr<Share> weak = m_share;
        add<Chip>("Look again", 0xf0450)->onClick([weak]() {
          if (auto s = weak.lock()) scan(s);
        });
        requestLayout();
      }

    private:
      std::shared_ptr<Share> m_share;
      std::vector<std::string> m_files;
      bool m_built = false;
    };

    // Shows an imported look before anything changes.
    class PendingBox : public Item {
    public:
      explicit PendingBox(std::shared_ptr<Share> share) : m_share(std::move(share)) {
        m_bg = static_cast<Box*>(addChild(ui::box({})));
        m_bg->setRadius(12.0F);
        m_bg->setFill(accent(0.08F));
        m_bg->setBorder(accent(0.4F), 1.0F);
        showIf([s = m_share]() { return s->pending.has_value(); });
        sync();
      }

      void sync() override {
        if (m_gen == m_share->pendingGen) return;
        m_gen = m_share->pendingGen;
        if (m_col != nullptr) (void)removeChild(m_col);
        m_col = nullptr;
        if (!m_share->pending) return;
        const presets::ReadResult& p = *m_share->pending;
        m_col = static_cast<Column*>(addChild(std::make_unique<Column>(10.0F)));
        m_col->add<Text>(str(p.look, "name"), TextOpts{.px = 14.0F, .bold = true});
        m_col->add<Text>(str(p.look, "note"), TextOpts{.px = 11.0F, .color = dim(), .wrap = true});
        const json bars = p.look.value("bars", json::array());
        if (bars.is_array() && !bars.empty()) {
          m_col->addItem(layoutPreview(bars[0], 56.0F, false));
        } else {
          m_col->add<Gap>(0.0F); // Keeps the column's spacing the same as with a preview.
        }
        const bool cmds = !p.commands.empty();
        if (cmds) {
          auto* c = m_col->add<Column>(4.0F);
          c->add<Text>("⚠ This look runs commands on your machine (bar modules / clicks). Only apply it if you trust where it came from:",
                       TextOpts{.px = 11.0F, .bold = true, .color = danger(), .wrap = true});
          for (const auto& cmd : p.commands) {
            c->add<Text>("  $ " + cmd, TextOpts{.px = 11.0F, .elide = true, .family = "JetBrainsMono Nerd Font"})->setFixedWidth(0.0F);
          }
        }
        auto* flow = m_col->add<Flow>(6.0F);
        std::weak_ptr<Share> weak = m_share;
        // Each action closes and rebuilds this box, so defer it out of the click handler.
        const auto later = [weak](std::function<void(Share&)> fn) {
          return [weak, fn]() {
            DeferredCall::callLater([weak, fn]() {
              auto s = weak.lock();
              if (!s || !s->pending) return;
              fn(*s);
              s->pending.reset();
              ++s->pendingGen;
              refresh();
            });
          };
        };
        flow->add<Chip>(cmds ? "Apply with its commands" : "Apply", 0xf012c)
            ->onWhen([]() { return true; })
            ->onClick(later([](Share& s) {
              presets::keep(s.pending->look);
              applyPreset(s.pending->look);
            }));
        if (cmds) {
          flow->add<Chip>("Apply without its commands", 0xf0a7a)->onClick(later([](Share& s) {
            const json l = presets::withoutCommands(s.pending->look);
            presets::keep(l);
            applyPreset(l);
          }));
        }
        flow->add<Chip>(cmds ? "Just save it (commands included)" : "Just save it to my looks", 0xf0415)
            ->onClick(later([](Share& s) { presets::keep(s.pending->look); }));
        flow->add<Chip>("Cancel", 0xf0156)->onClick(later([](Share&) {}));
        requestLayout();
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float ch = m_col != nullptr ? m_col->place(renderer, std::max(1.0F, width - 28.0F)) : 0.0F;
        if (m_col != nullptr) m_col->setPosition(14.0F, 14.0F);
        const float h = ch + 28.0F;
        m_bg->setSize(width, h);
        setSize(width, h);
        return h;
      }

    private:
      std::shared_ptr<Share> m_share;
      int m_gen = -1;
      Box* m_bg = nullptr;
      Column* m_col = nullptr;
    };

  } // namespace

  void buildPresets(Column& page) {
    {
      auto* g = page.add<Group>("Presets", "Tap one to apply it. They change the look — bar, workspaces, motion, surfaces, popups — and "
                                           "leave your wallpaper, lock and the rest alone. Fine-tune anything afterwards.");
      auto* flow = g->add<Flow>(12.0F);
      for (const auto& p : presets::builtin()) flow->add<PresetCard>(p, false);
    }

    {
      auto* g = page.add<Group>("Your looks");
      g->bindHint([]() -> std::string {
        return presets::saved().empty() ? "Save how things look right now, and come back to it any time." : "";
      });
      g->add<SavedFlow>();
      auto* row = g->add<HRow>(8.0F);
      auto field = std::make_shared<Field*>(nullptr);
      const auto save = [field](const std::string& t) {
        DeferredCall::callLater([field, t]() {
          flush(); // The snapshot reads settings.json from disk.
          presets::save(t);
          if (*field != nullptr) (*field)->setText("");
          refresh();
        });
      };
      *field = row->add<Field>(std::nullopt, FieldOpts{.width = 220.0F, .placeholder = "Name this look", .onAccepted = save});
      row->add<Chip>("Save current look", 0xf0415)->onClick([field, save]() { save((*field)->text()); });
    }

    // Share
    auto share = std::make_shared<Share>();
    scan(share);
    {
      auto* g = page.add<Group>("Share a look",
                                "A look is one small file: bars, styles, colours, motion, font, panel, lock and power menu — not your "
                                "wallpaper or pinned apps.",
                                0xf0497);
      g->add<Text>("Export what you have now", TextOpts{.px = 11.0F, .bold = true, .color = dim()});
      auto* row = g->add<HRow>(8.0F);
      auto* exportName = row->add<Field>(std::nullopt, FieldOpts{.width = 220.0F, .placeholder = "Name it"});
      auto rescan = std::make_shared<Timer>();
      row->add<Chip>("Save file", 0xf0193)->onClick([exportName, share, rescan]() {
        flush();
        const std::string path = presets::exportLook(exportName->text(), false);
        if (!path.empty()) lastExport() = path;
        std::weak_ptr<Share> weak = share;
        rescan->start(std::chrono::milliseconds(600), [weak]() {
          if (auto s = weak.lock()) scan(s);
        });
        refresh();
      });
      row->add<Chip>("Copy as text", 0xf018f)->onClick([exportName]() {
        flush();
        lastExport() = presets::exportLook(exportName->text(), true);
        refresh();
      });
      auto* done = g->add<HRow>(8.0F);
      done->showIf([]() { return !lastExport().empty(); });
      done->add<Text>("", TextOpts{.px = 11.0F, .color = ok()})->bindText([]() -> std::string {
        if (lastExport() == "clipboard") return "Copied — paste it anywhere (Discord, a gist…).";
        std::string p = lastExport();
        const char* home = std::getenv("HOME");
        if (home != nullptr && *home != '\0') {
          if (const auto at = p.find(home); at != std::string::npos) p.replace(at, std::string(home).size(), "~");
        }
        return "Saved " + p;
      });
      done->add<Chip>("Open folder", 0xf024b)
          ->onClick([]() { spawn({"xdg-open", presets::looksDir()}); })
          ->showIf([]() { return lastExport() != "clipboard"; });

      g->add<Text>("Import one", TextOpts{.px = 11.0F, .bold = true, .color = dim(), .topPadding = 8.0F});
      g->add<FoundFlow>(share);
      auto paste = std::make_shared<Field*>(nullptr);
      *paste = g->add<Field>(std::nullopt, FieldOpts{
                                               .width = 0.0F,
                                               .placeholder = "…or paste a look's text here and press Enter",
                                               .onAccepted = [share, paste](const std::string& t) {
                                                 open(share, presets::readLook(t));
                                                 DeferredCall::callLater([paste]() {
                                                   if (*paste != nullptr) (*paste)->setText("");
                                                 });
                                               },
                                           });
      g->add<Text>("", TextOpts{.px = 11.0F, .color = danger()})
          ->bindText([share]() { return share->error; })
          ->showIf([share]() { return !share->error.empty(); });
      g->add<PendingBox>(share);
    }
  }

} // namespace kusanagi::sp
