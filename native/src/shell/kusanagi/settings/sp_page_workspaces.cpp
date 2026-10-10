// Settings > Workspaces: indicator style with a live preview, custom icons, how many are always shown,
// the glow and the active colour.

#include "render/core/renderer.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace kusanagi::sp {

  namespace {

    // Preview of the workspace indicator: 2 is active, 1 and 3 have windows.
    class WsPreview : public Item {
    public:
      WsPreview() {
        m_frame = static_cast<Box*>(addChild(ui::box({})));
        m_pill = static_cast<Box*>(addChild(ui::box({})));
        m_slots = addChild(ui::node({}));
      }

      void sync() override {
        m_dirty = true;
        requestLayout();
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float h = 64.0F;
        setSize(width, h);
        m_frame->setSize(width, h);
        m_frame->setRadius(std::max(6.0F, kusanagi::radius() - 8.0F));
        m_frame->setFill(bgPanel(0.6F));

        if (!m_dirty && width == m_builtW) return h;
        m_dirty = false;
        m_builtW = width;
        while (!m_slots->children().empty()) m_slots->removeChild(m_slots->children().back().get());
        const std::string style = get<std::string>("workspaces.style", "pills");
        const bool glow = get<bool>("workspaces.glow", true);
        const std::string ac = get<std::string>("workspaces.activeColor", "accent");
        const ColorSpec active = ac == "accent2" ? accent2() : ac == "text" ? textA(1.0F) : accent();
        const int shown = std::clamp(get<int>("workspaces.shown", 5), 1, 10);
        const bool dwl = style == "dwl";
        const bool textStyle = dwl || style == "numbers" || style == "roman" || style == "kanji" || style == "custom";
        static const std::array<const char*, 10> roman{"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
        static const std::array<const char*, 10> kanji{"一", "二", "三", "四", "五", "六", "七", "八", "九", "十"};
        std::vector<std::string> icons;
        {
          std::istringstream in(get<std::string>("workspaces.icons", ""));
          std::string w;
          while (in >> w) icons.push_back(w);
        }
        const float slotH = 22.0F;
        const float fontPx = dwl ? get<float>("bar.fontSize", 11.0F) : 12.0F;

        float x = 0.0F;
        for (int i = 0; i < shown; ++i) {
          const int n = i + 1;
          const bool isActive = i == 1;
          const bool empty = !(i == 0 || i == 2);
          Node* slot = m_slots->addChild(ui::node({}));
          float len = 0.0F;
          if (!textStyle) {
            const bool pill = isActive && style == "pills";
            const float dw = pill ? 26.0F : 10.0F;
            const float dh = pill ? 14.0F : 10.0F;
            len = dw + 8.0F;
            if (isActive && glow) {
              // Glow under the active dot.
              auto* halo = static_cast<Box*>(slot->addChild(ui::box({})));
              halo->setPosition(4.0F - 6.0F, (slotH - dh) / 2.0F - 6.0F);
              halo->setSize(dw + 12.0F, dh + 12.0F);
              halo->setRadius(6.0F + 6.0F);
              halo->setSoftness(10.0F);
              Color c = resolved(active);
              c.a *= 0.55F;
              halo->setFill(fixedColorSpec(c));
            }
            auto* dot = static_cast<Box*>(slot->addChild(ui::box({})));
            dot->setPosition(4.0F, std::round((slotH - dh) / 2.0F));
            dot->setSize(dw, dh);
            dot->setRadius(std::min(6.0F, dh / 2.0F));
            dot->setFill(isActive ? active : empty ? clearColorSpec() : textA(0.85F));
            dot->setBorder(textA(0.35F), isActive || !empty ? 0.0F : 1.0F);
            if (pill) {
              auto* num = static_cast<Label*>(dot->addChild(makeText(std::to_string(n), 10.0F, true, bgPanel())));
              num->measure(renderer);
              num->setPosition(std::round((dw - num->width()) / 2.0F), std::round((dh - num->height()) / 2.0F));
            }
          } else {
            std::string g = std::to_string(n);
            if (style == "roman") g = roman[static_cast<std::size_t>(i)];
            if (style == "kanji") g = kanji[static_cast<std::size_t>(i)];
            if (style == "custom" && static_cast<std::size_t>(i) < icons.size()) g = icons[static_cast<std::size_t>(i)];
            const ColorSpec col = dwl && isActive ? bgPanel() : isActive ? active : empty ? textA(0.35F) : textA(0.85F);
            auto label = makeText(g, fontPx, isActive && !dwl, col);
            label->measure(renderer);
            const float lw = label->width();
            const float lh = label->height();
            len = dwl ? std::max(slotH, lw + 14.0F) : lw + 4.0F + 8.0F;
            if (dwl) {
              auto* block = static_cast<Box*>(slot->addChild(ui::box({})));
              block->setSize(len, slotH);
              block->setFill(isActive ? active : clearColorSpec());
              if (!empty) {
                auto* mark = static_cast<Box*>(slot->addChild(ui::box({})));
                mark->setPosition(3.0F, 3.0F);
                mark->setSize(4.0F, 4.0F);
                Color mc = resolved(isActive ? bgPanel() : textA(1.0F));
                if (!isActive) mc.a *= 0.8F;
                mark->setFill(fixedColorSpec(mc));
              }
            }
            auto* l = static_cast<Label*>(slot->addChild(std::move(label)));
            const float ly = std::round((slotH - lh) / 2.0F) - (dwl ? 0.0F : 1.0F);
            const float lx = dwl ? std::round((len - lw) / 2.0F) : 6.0F;
            l->setPosition(lx, ly);
            if (!dwl && isActive) {
              auto* under = static_cast<Box*>(slot->addChild(ui::box({})));
              const float uw = std::max(8.0F, lw);
              under->setPosition(std::round(lx + (lw - uw) / 2.0F), ly + lh + 1.0F);
              under->setSize(uw, 2.0F);
              under->setRadius(1.0F);
              under->setFill(active);
            }
          }
          slot->setPosition(x, 0.0F);
          slot->setSize(len, slotH);
          x += len;
        }
        // Pill behind the indicator, rounded like the bar.
        const float pw = x + 16.0F;
        m_pill->setSize(pw, slotH);
        m_pill->setRadius(std::min(slotH / 2.0F, get<float>("bar.radius", 10.0F)));
        m_pill->setFill(textA(0.06F));
        const float px = std::round((width - pw) / 2.0F);
        const float py = std::round((h - slotH) / 2.0F);
        m_pill->setPosition(px, py);
        m_slots->setPosition(px + 8.0F, py);
        return h;
      }

    private:
      Box* m_frame = nullptr;
      Box* m_pill = nullptr;
      Node* m_slots = nullptr;
      bool m_dirty = true;
      float m_builtW = -1.0F;
    };

  } // namespace

  void buildWorkspaces(Column& page) {
    page.add<Group>("Preview")->add<WsPreview>();

    {
      auto* g = page.add<Group>("Style");
      auto* flow = g->add<Flow>(6.0F);
      const std::array<std::pair<const char*, const char*>, 7> styles{{{"pills", "Pills"},
                                                                       {"dots", "Dots"},
                                                                       {"numbers", "1 2 3"},
                                                                       {"roman", "I II III"},
                                                                       {"kanji", "一 二 三"},
                                                                       {"dwl", "dwl blocks"},
                                                                       {"custom", "Custom icons"}}};
      for (const auto& [value, label] : styles) flow->addItem(choiceChip(label, "workspaces.style", value));
      g->add<Row>("Icons", "one per workspace, separated by spaces — any text or Nerd Font glyph",
                  std::make_unique<Field>(bind("workspaces.icons"),
                                          FieldOpts{.width = 300.0F, .placeholder = "♠ ♣ ♥ ♦ ★", .applyOnEdit = true}))
          ->showIf([]() { return get<std::string>("workspaces.style", "pills") == "custom"; });
    }

    {
      auto* g = page.add<Group>("Behaviour");
      g->add<Row>("Always shown", "more appear while they have windows",
                  std::make_unique<Stepper>(bind("workspaces.shown"), StepperOpts{.from = 1, .to = 9}));
      g->add<Row>("Glow on the active one", std::make_unique<Switch>(bind("workspaces.glow")));
      g->add<Row>("Active colour",
                  std::make_unique<Segmented>(bind("workspaces.activeColor"),
                                              std::vector<Option>{{"Accent", "accent"}, {"Second accent", "accent2"}, {"Text", "text"}},
                                              300.0F));
    }

    page.add<Chip>("Reset workspaces", 0xf0709)->onClick([]() { reset("workspaces"); });
  }

} // namespace kusanagi::sp
