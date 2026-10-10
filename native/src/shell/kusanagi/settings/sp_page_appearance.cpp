// Settings > Appearance: palette, accent, font, shape and depth, and motion.

#include "core/timer_manager.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/animation/animation_manager.h"
#include "render/core/color.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace kusanagi::sp {

  namespace {

    constexpr auto kPointer = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;

    // Colours each palette card is drawn in.
    struct Pal {
      const char* id;
      const char* name;
      const char* bg;
      const char* text;
      const char* accent;
      const char* accent2;
      const char* ok;
      const char* danger;
    };
    constexpr std::array<Pal, 10> kPalettes{{
        {"wallpaper", "Wallpaper", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr},
        {"catppuccin-mocha", "Mocha", "#1e1e2e", "#cdd6f4", "#cba6f7", "#f5c2e7", "#a6e3a1", "#f38ba8"},
        {"catppuccin-latte", "Latte", "#eff1f5", "#4c4f69", "#8839ef", "#ea76cb", "#40a02b", "#d20f39"},
        {"gruvbox", "Gruvbox", "#1d2021", "#ebdbb2", "#fabd2f", "#fe8019", "#b8bb26", "#fb4934"},
        {"nord", "Nord", "#2e3440", "#eceff4", "#88c0d0", "#81a1c1", "#a3be8c", "#bf616a"},
        {"rose-pine", "Rosé Pine", "#191724", "#e0def4", "#ebbcba", "#c4a7e7", "#9ccfd8", "#eb6f92"},
        {"tokyo-night", "Tokyo Night", "#1a1b26", "#c0caf5", "#7aa2f7", "#bb9af7", "#9ece6a", "#f7768e"},
        {"everforest", "Everforest", "#272e33", "#d3c6aa", "#a7c080", "#83c092", "#a7c080", "#e67e80"},
        {"kanagawa", "Kanagawa", "#1f1f28", "#dcd7ba", "#7e9cd8", "#957fb8", "#98bb6c", "#e82424"},
        {"mono", "Mono", "#0e0e0e", "#e6e6e6", "#e6e6e6", "#b0b0b0", "#8fd18f", "#ff5f5f"},
    }};

    ColorSpec hex(const char* h, const ColorSpec& fallback) {
      Color c;
      if (h != nullptr && tryParseHexColor(h, c)) return fixedColorSpec(c);
      return fallback;
    }

    std::string lower(std::string s) {
      for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return s;
    }

    std::string fixed1(double v) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.1f", v);
      return buf;
    }

    // Small card drawn in the palette's own colours.
    class PaletteCard : public Item {
    public:
      explicit PaletteCard(const Pal& pal) : m_pal(pal) {
        m_bg = static_cast<Box*>(addChild(ui::box({})));
        const std::array<std::pair<const char*, ColorSpec>, 4> dots{{
            {pal.accent, accent()}, {pal.accent2, accent2()}, {pal.ok, ok()}, {pal.danger, danger()}}};
        for (const auto& [h, role] : dots) {
          Box* d = static_cast<Box*>(addChild(ui::box({})));
          d->setFill(hex(h, role));
          d->setRadius(6.0F);
          d->setSize(12.0F, 12.0F);
          m_dots.push_back(d);
        }
        m_name = static_cast<Label*>(addChild(makeText(pal.name, 11.0F, false, hex(pal.text, textA()))));
        m_name->setMaxLines(1);
        setCursorShape(kPointer);
        setOnEnter([this](const PointerData&) { tweenScale(*this, 1.04F, 160, 1.70158F); });
        setOnLeave([this]() { tweenScale(*this, 1.0F, 160, 1.70158F); });
        setOnClick([this](const PointerData&) { set("look.palette", m_pal.id); });
        sync();
      }

      void sync() override {
        const bool on = get<std::string>("look.palette", "wallpaper") == m_pal.id;
        m_bg->setFill(hex(m_pal.bg, bgPanel()));
        m_bg->setBorder(on ? accent() : textA(0.12F), on ? 2.0F : 1.0F);
        if (on != m_on) {
          m_on = on;
          m_name->setFontWeight(on ? FontWeight::Bold : FontWeight::Normal);
          requestLayout();
        }
      }

      float place(Renderer& renderer, float /*width*/) override {
        setSize(112.0F, 58.0F);
        m_bg->setSize(112.0F, 58.0F);
        m_bg->setRadius(10.0F);
        for (std::size_t i = 0; i < m_dots.size(); ++i) m_dots[i]->setPosition(10.0F + static_cast<float>(i) * 16.0F, 10.0F);
        m_name->measure(renderer);
        m_name->setPosition(10.0F, 58.0F - 8.0F - m_name->height());
        return 58.0F;
      }

    private:
      const Pal& m_pal;
      Box* m_bg = nullptr;
      std::vector<Box*> m_dots;
      Label* m_name = nullptr;
      bool m_on = false;
    };

    struct Motion {
      const char* id;
      const char* name;
      const char* note;
      double speed;
      double bounce;
    };
    constexpr std::array<Motion, 7> kMotions{{
        {"instant", "Instant", "no animation at all", 0.0, 0.0},
        {"snappy", "Snappy", "quick, barely a spring", 0.6, 0.3},
        {"smooth", "Smooth", "the default", 1.0, 1.0},
        {"bouncy", "Bouncy", "springs that overshoot", 1.0, 1.8},
        {"playful", "Playful", "slower and very springy", 1.25, 2.0},
        {"gentle", "Gentle", "calm, soft landings", 1.4, 0.4},
        {"cinematic", "Cinematic", "long glides, no bounce", 1.8, 0.0},
    }};

    std::string currentMotion() {
      const double s = get<double>("look.animSpeed", 1.0);
      const double b = get<double>("look.bounce", 1.0);
      for (const auto& m : kMotions) {
        if (std::fabs(m.speed - s) < 0.01 && std::fabs(m.bounce - b) < 0.01) return m.id;
      }
      return "custom";
    }

    // Motion preset card. While hovered, a dot hops across with the preset's own timing.
    class MotionCard : public Item {
    public:
      explicit MotionCard(const Motion& m) : m_m(m) {
        m_bg = static_cast<Box*>(addChild(ui::box({})));
        m_track = static_cast<Box*>(addChild(ui::box({})));
        m_track->setFill(textA(0.05F));
        m_dot = static_cast<Box*>(m_track->addChild(ui::box({})));
        m_dot->setFill(accent());
        m_dot->setRadius(9.0F);
        m_dot->setSize(18.0F, 18.0F);
        m_dot->setPosition(4.0F, 4.0F);
        m_name = static_cast<Label*>(addChild(makeText(m.name, 12.0F, true)));
        m_note = static_cast<Label*>(addChild(makeText(m.note, 9.0F, false, dim())));
        m_note->setMaxLines(0);
        setCursorShape(kPointer);
        setOnEnter([this](const PointerData&) {
          restyle();
          const int interval = std::max(500, static_cast<int>(420.0 * m_m.speed + 380.0));
          hop();
          m_timer.startRepeating(std::chrono::milliseconds(interval), [this]() { hop(); });
        });
        setOnLeave([this]() {
          restyle();
          m_timer.stop();
        });
        setOnClick([this](const PointerData&) {
          write({{"look.animSpeed", m_m.speed}, {"look.bounce", m_m.bounce}});
        });
        sync();
      }

      void hop() {
        m_atEnd = !m_atEnd;
        const float to = m_atEnd ? m_track->width() - 18.0F - 4.0F : 4.0F;
        AnimationManager* anims = animationManager();
        if (m_m.speed <= 0.0 || anims == nullptr) {
          m_dot->setPosition(to, 4.0F);
          return;
        }
        anims->cancelForOwner(m_dot);
        const float from = m_dot->x();
        const auto overshoot = static_cast<float>(1.6 * m_m.bounce);
        // The preset's own duration, not scaled by the current speed setting.
        anims->animateTimer(
            0.0F, 1.0F, static_cast<float>(420.0 * m_m.speed), Easing::Linear,
            [this, from, to, overshoot](float t) { m_dot->setPosition(from + (to - from) * outBack(t, overshoot), 4.0F); },
            {}, m_dot
        );
        requestLayout(); // Started from a timer, so wake the window's frame loop.
      }

      void restyle() {
        m_bg->setFill(textA(hovered() ? 0.08F : 0.04F));
        m_bg->setBorder(m_on ? accent() : textA(0.07F), m_on ? 2.0F : 1.0F);
        m_name->setColor(m_on ? accent() : textA(1.0F));
      }

      void sync() override {
        m_on = currentMotion() == m_m.id;
        restyle();
      }

      float place(Renderer& renderer, float /*width*/) override {
        setSize(128.0F, 104.0F);
        m_bg->setSize(128.0F, 104.0F);
        m_bg->setRadius(12.0F);
        m_track->setPosition(12.0F, 14.0F);
        m_track->setSize(128.0F - 24.0F, 26.0F);
        m_track->setRadius(13.0F);
        if (!m_atEnd) m_dot->setPosition(4.0F, 4.0F);
        m_name->measure(renderer);
        m_name->setPosition(12.0F, 52.0F);
        m_note->setMaxWidth(128.0F - 24.0F);
        m_note->measure(renderer);
        m_note->setPosition(12.0F, 52.0F + m_name->height());
        return 104.0F;
      }

    private:
      const Motion& m_m;
      Box* m_bg = nullptr;
      Box* m_track = nullptr;
      Box* m_dot = nullptr;
      Label* m_name = nullptr;
      Label* m_note = nullptr;
      Timer m_timer;
      bool m_atEnd = false;
      bool m_on = false;
    };

  } // namespace

  void buildAppearance(Column& page) {
    {
      auto* g = page.add<Group>("Palette", "Colours for the shell. Wallpaper = picked from your wallpaper (and re-picked whenever it changes).");
      auto* flow = g->add<Flow>(8.0F);
      for (const auto& p : kPalettes) flow->add<PaletteCard>(p);
    }

    {
      auto* g = page.add<Group>("Accent colour", "Comes from the palette — or pin one over it.");
      auto* flow = g->add<Flow>(10.0F);
      flow->add<Chip>("From palette", 0xf03d8)
          ->onWhen([]() { return get<std::string>("look.accent", "").empty(); })
          ->onClick([]() { set("look.accent", ""); });
      static const std::array<const char*, 8> swatches{"#f5a3b5", "#f5b38a", "#e9d27c", "#8fd6b0",
                                                       "#8cc8f0", "#b9a6f2", "#e8505b", "#e8e8e8"};
      for (const char* c : swatches) {
        const std::string col = c;
        flow->add<Swatch>([col]() { return hex(col.c_str(), accent()); },
                          [col]() { return lower(get<std::string>("look.accent", "")) == col; },
                          [col]() { set("look.accent", col); });
      }
      g->add<Row>("Custom colour", "hex, e.g. #ff7a90",
                  std::make_unique<Field>(bind("look.accent"),
                                          FieldOpts{
                                              .width = 160.0F,
                                              .placeholder = "#rrggbb",
                                              .convert = [](const std::string& t) -> std::optional<json> {
                                                std::string v = t;
                                                v.erase(0, v.find_first_not_of(" \t"));
                                                v.erase(v.find_last_not_of(" \t") + 1);
                                                if (v.size() != 7 || v[0] != '#') return std::nullopt;
                                                for (std::size_t i = 1; i < 7; ++i) {
                                                  if (!std::isxdigit(static_cast<unsigned char>(v[i]))) return std::nullopt;
                                                }
                                                return json(v);
                                              },
                                          }));
    }

    {
      auto* g = page.add<Group>("Font", "For all of Kusanagi's text. Icons always come from the Nerd Font, so any font works.", 0xf0284);
      g->add<Text>("Nerd Fonts", TextOpts{.px = 11.0F, .bold = true, .color = dim()});
      auto* flow = g->add<Flow>(6.0F);
      static const std::array<const char*, 14> nerd{"JetBrainsMono", "Iosevka",    "CaskaydiaMono", "FiraCode", "ZedMono",
                                                    "Lilex",         "VictorMono", "SpaceMono",     "Mononoki", "BlexMono",
                                                    "UbuntuMono",    "0xProto",    "Ubuntu",        "UbuntuSans"};
      for (const char* n : nerd) {
        const std::string family = std::string(n) + " Nerd Font";
        flow->add<Chip>(n, 0, family)
            ->onWhen([family]() { return get<std::string>("look.font", "") == family; })
            ->onClick([family]() { set("look.font", family); })
            ->showIf([family]() {
              const auto* fonts = installedFonts();
              return fonts != nullptr && std::ranges::find(*fonts, family) != fonts->end();
            });
      }
      g->add<Text>("Every font", TextOpts{.px = 11.0F, .bold = true, .color = dim(), .topPadding = 6.0F});
      g->add<FontPicker>(bind("look.font"));
    }

    {
      auto* g = page.add<Group>("Shape & depth");
      g->add<Slider>(bind("look.radius"), SliderOpts{
                                              .icon = 0xf0830,
                                              .label = "Corner radius",
                                              .toUnit = [](const json& v) { return (v.get<float>() - 4.0F) / 24.0F; },
                                              .fromUnit = [](float u) { return json(std::lround(4.0F + u * 24.0F)); },
                                              .text = [](const json& v) { return std::to_string(std::lround(v.get<double>())) + "px"; },
                                              .step = 1.0F / 24.0F,
                                          });
      g->add<Slider>(bind("panel.opacity"), SliderOpts{
                                                .icon = 0xf050e,
                                                .label = "Surface opacity",
                                                .toUnit = [](const json& v) { return (v.get<float>() - 0.5F) / 0.5F; },
                                                .fromUnit = [](float u) { return json(std::round((0.5 + u * 0.5) * 100.0) / 100.0); },
                                                .text = [](const json& v) { return std::to_string(std::lround(v.get<double>() * 100.0)) + "%"; },
                                            });
      g->add<Row>("Outlines", "a hairline around panels, cards and popups", std::make_unique<Switch>(bind("look.borders")));
      g->add<Row>("Outlines in the accent colour", std::make_unique<Switch>(bind("look.borderAccent")));
      g->add<Slider>(bind("look.backdrop"), SliderOpts{
                                                .icon = 0xf050e,
                                                .label = "Backdrop",
                                                .toUnit = [](const json& v) { return v.get<float>() / 0.5F; },
                                                .fromUnit = [](float u) { return json(std::round(u * 0.5 * 100.0) / 100.0); },
                                                .text = [](const json& v) { return std::to_string(std::lround(v.get<double>() * 100.0)) + "%"; },
                                            });
      g->add<Text>("Backdrop = how much the screen dims behind the panel, launcher and pickers.",
                   TextOpts{.px = 10.0F, .color = dim(), .wrap = true});
      g->add<Row>("Shadows", "under the panel, launcher, popups and OSD", std::make_unique<Switch>(bind("look.shadows")));
    }

    {
      auto* g = page.add<Group>("Motion", "How everything moves. Hover a card to see it; fine-tune with the sliders below.", 0xf0e09);
      auto* flow = g->add<Flow>(8.0F);
      for (const auto& m : kMotions) flow->add<MotionCard>(m);
      g->add<Slider>(bind("look.animSpeed"), SliderOpts{
                                                 .icon = 0xf04c5,
                                                 .label = "Speed",
                                                 .toUnit = [](const json& v) { return v.get<float>() / 2.0F; },
                                                 .fromUnit = [](float u) { return json(std::round(u * 2.0 * 20.0) / 20.0); },
                                                 .text = [](const json& v) -> std::string {
                                                   const double s = v.get<double>();
                                                   if (s == 0.0) return "off";
                                                   return s <= 1.0 ? "×" + fixed1(1.0 / s) + " faster" : "×" + fixed1(s) + " slower";
                                                 },
                                                 .step = 0.05F,
                                             });
      g->add<Slider>(bind("look.bounce"), SliderOpts{
                                              .icon = 0xf0e09,
                                              .label = "Bounciness",
                                              .toUnit = [](const json& v) { return v.get<float>() / 2.0F; },
                                              .fromUnit = [](float u) { return json(std::round(u * 2.0 * 10.0) / 10.0); },
                                              .text = [](const json& v) -> std::string {
                                                const double b = v.get<double>();
                                                return b == 0.0 ? "none" : fixed1(b) + "×";
                                              },
                                              .step = 0.05F,
                                          });
      g->add<Row>("Instant open", "keep the launcher, panel, pickers and clipboard ready so they open on the very first frame",
                  std::make_unique<Switch>(bind("look.preload")));
    }

    page.add<Chip>("Reset appearance", 0xf0709)->onClick([]() {
      reset("look");
      set("panel.opacity", 0.95);
    });
  }

} // namespace kusanagi::sp
