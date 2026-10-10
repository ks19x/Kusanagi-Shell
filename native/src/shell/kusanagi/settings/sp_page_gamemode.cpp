// Settings > Game mode: current state, auto-on for fullscreen, what it turns off, and how it's announced.

#include "compositors/compositor_detect.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <cmath>
#include <memory>

namespace kusanagi::sp {

  namespace {

    struct State {
      bool active = false;
      bool manual = false;
    };

    // `gamemode status` prints "on (by hand)", "on (fullscreen)" or "off".
    void poll(State& s) {
      const std::string out = ipc("gamemode status");
      s.active = out.starts_with("on");
      s.manual = out.find("by hand") != std::string::npos;
    }

    class Status : public Item {
    public:
      explicit Status(std::shared_ptr<State> state) : m_state(std::move(state)) {
        m_circle = static_cast<Box*>(addChild(ui::box({})));
        m_icon = static_cast<Label*>(addChild(makeIcon(0xf0297, 22.0F)));
        m_title = static_cast<Label*>(addChild(makeText("", 14.0F, true)));
        m_sub = static_cast<Label*>(addChild(makeText("", 11.0F, false, dim())));
        sync();
      }

      void sync() override {
        const bool on = m_state->active;
        if (on != m_on || m_first) {
          const ColorSpec to = on ? accent() : textA(0.08F);
          if (m_first) {
            m_circle->setFill(to);
          } else {
            tweenColor(*m_circle, m_color, to, 200, [this](const ColorSpec& c) { m_circle->setFill(c); });
          }
          m_color = to;
          m_icon->setColor(on ? bgPanel() : dim());
          m_on = on;
          m_first = false;
        }
        bool changed = m_title->setText(on ? (m_state->manual ? "On — switched on by hand" : "On — a game is fullscreen") : "Off");
        changed = m_sub->setText(get<bool>("gamemode.auto", true) ? "auto: on" : "auto: off") || changed;
        if (changed) requestLayout();
      }

      float place(Renderer& renderer, float /*width*/) override {
        m_circle->setSize(46.0F, 46.0F);
        m_circle->setRadius(23.0F);
        m_icon->measure(renderer);
        m_title->measure(renderer);
        m_sub->measure(renderer);
        const float colH = m_title->height() + m_sub->height();
        const float h = std::max(46.0F, colH);
        m_circle->setPosition(0.0F, std::round((h - 46.0F) / 2.0F));
        m_icon->setPosition(std::round((46.0F - m_icon->width()) / 2.0F), m_circle->y() + std::round((46.0F - m_icon->height()) / 2.0F));
        const float cy = std::round((h - colH) / 2.0F);
        m_title->setPosition(46.0F + 14.0F, cy);
        m_sub->setPosition(46.0F + 14.0F, cy + m_title->height());
        setSize(46.0F + 14.0F + std::max(m_title->width(), m_sub->width()), h);
        return h;
      }

    private:
      std::shared_ptr<State> m_state;
      Box* m_circle = nullptr;
      Label* m_icon = nullptr;
      Label* m_title = nullptr;
      Label* m_sub = nullptr;
      ColorSpec m_color;
      bool m_on = false;
      bool m_first = true;
    };

  } // namespace

  void buildGameMode(Column& page) {
    auto state = std::make_shared<State>();
    poll(*state);
    page.add<Poll>(1000, [state]() {
      const State before = *state;
      poll(*state);
      if (before.active != state->active || before.manual != state->manual) refresh();
    });

    {
      auto* g = page.add<Group>("Game mode", "Super+G toggles it by hand. Auto turns it on when a window goes fullscreen and off when you leave it.");
      g->add<Status>(state);
      g->add<Row>("Turn on automatically for fullscreen",
                  std::make_unique<Switch>(Binding{
                      .get = []() -> json { return get<bool>("gamemode.auto", true); },
                      // The shell saves it and reacts, turning on right away if something is fullscreen.
                      .set = [state](const json& v) {
                        (void)ipc(truthy(v) ? "gamemode auto-on" : "gamemode auto-off");
                        poll(*state);
                        refresh();
                      },
                  }));
      g->add<Chip>("Turn on now", 0xf0297)
          ->labelFrom([state]() { return std::string(state->active ? "Turn off now" : "Turn on now"); })
          ->onClick([state]() {
            (void)ipc("gamemode toggle");
            poll(*state);
            refresh();
          });
    }

    {
      auto* g = page.add<Group>("While it's on");
      // Only Mango and Hyprland can switch their effects at runtime.
      g->add<Row>("Compositor effects off", "blur, shadows and animations — the biggest GPU win",
                  std::make_unique<Switch>(bind("gamemode.effects")))
          ->showIf([]() { return compositors::isMango() || compositors::isHyprland(); });
      g->add<Row>("Performance CPU governor", "feral gamemode for as long as it's on", std::make_unique<Switch>(bind("gamemode.feral")));
      g->add<Row>("Quiet shell", "bar stats stop polling, song marquee and wallpaper slideshow pause",
                  std::make_unique<Switch>(bind("gamemode.quiet")));
      g->add<Row>("Hold notifications", "they still land in the Inbox; critical ones still pop up",
                  std::make_unique<Switch>(bind("gamemode.dnd")));
    }

    {
      auto* g = page.add<Group>("Switching");
      g->add<Row>("Grace before switching off", "alt-tab out and back within this and nothing changes",
                  std::make_unique<Segmented>(bind("gamemode.grace"),
                                              std::vector<Option>{{"None", 0}, {"0.8 s", 800}, {"2 s", 2000}, {"5 s", 5000}}, 300.0F));
      g->add<Row>("Show the on/off pill",
                  std::make_unique<Segmented>(bind("gamemode.announce"),
                                              std::vector<Option>{{"By hand", "manual"}, {"Always", "always"}, {"Never", "never"}},
                                              300.0F));
    }
  }

} // namespace kusanagi::sp
