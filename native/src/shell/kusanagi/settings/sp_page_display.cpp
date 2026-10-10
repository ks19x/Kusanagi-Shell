// Settings > Display: monitors, brightness (backlights and DDC/CI), the window gaps and borders override
// (see wm_layout.h) and night light through gammastep.

#include "compositors/compositor_platform.h"
#include "core/timer_manager.h"
#include "render/core/renderer.h"
#include "shell/control_center/control_center_services.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "shell/kusanagi/wm_layout.h"
#include "system/brightness_service.h"
#include "ui/controls/label.h"
#include "wayland/wayland_connection.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

namespace kusanagi::sp {

  namespace {

    // Formats like JavaScript: 1 -> "1", 1.25 -> "1.25".
    std::string jsNum(double v) {
      char buf[32];
      if (std::fabs(v - std::round(v)) < 1e-9) std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(v)));
      else std::snprintf(buf, sizeof(buf), "%g", v);
      return buf;
    }

    struct State {
      std::vector<wm::Monitor> monitors; // The compositor's list; if empty, use the Wayland outputs.
      bool night = false;                // gammastep is running.
      std::string ddc;                   // ok, off, missing, noi2c or noaccess.
      bool detecting = false;            // A "Look again" is waiting for the list to settle.
      std::vector<BrightnessDisplay> shown;
      Timer retemp;                      // Dragging warmth restarts gammastep 400 ms after the last move.
      Timer looking;
    };

    BrightnessService* brightness() { return services().brightness; }

    std::vector<const BrightnessDisplay*> screens() {
      std::vector<const BrightnessDisplay*> out;
      if (BrightnessService* b = brightness()) {
        for (const auto& d : b->displays()) {
          if (d.controllable) out.push_back(&d);
        }
      }
      return out;
    }
    const BrightnessDisplay* screen(const std::string& id) {
      for (const auto* d : screens()) {
        if (d->id == id) return d;
      }
      return nullptr;
    }
    bool builtIn(const std::string& connector) {
      return connector.starts_with("eDP") || connector.starts_with("LVDS") || connector.starts_with("DSI");
    }
    // "Built-in display" for a laptop panel, the monitor's model for DDC.
    std::string screenName(const BrightnessDisplay& d) {
      if (builtIn(d.id)) return "Built-in display";
      if (CompositorPlatform* p = services().platform) {
        for (const auto& o : p->outputs()) {
          if (o.connectorName == d.id && !o.model.empty()) return o.model;
        }
      }
      return d.label.empty() ? d.id : d.label;
    }

    // "" when fine, else off, missing, noi2c, noaccess, or none when no monitor answered.
    std::string ddcState(const State& s) {
      if (s.ddc.empty() || s.ddc == "ok") {
        if (s.ddc.empty()) return "";
        for (const auto* d : screens()) {
          if (!builtIn(d->id)) return "";
        }
        return "none";
      }
      return s.ddc;
    }

    void checkDdc(const std::weak_ptr<State>& weak) {
      // Same checks as brightness.sh detect, minus the slow ddcutil detect (the service already ran it).
      const std::string script = "[ \"$1\" = 1 ] || { echo off; exit 0; }; command -v ddcutil >/dev/null || { echo missing; exit 0; };"
                                 " ls /dev/i2c-* >/dev/null 2>&1 || { echo noi2c; exit 0; };"
                                 " for b in /dev/i2c-*; do [ -r \"$b\" ] && [ -w \"$b\" ] && { echo ok; exit 0; }; done; echo noaccess";
      run({"sh", "-c", script, "sh", get<bool>("brightness.ddc", true) ? "1" : "0"}, [weak](const std::string& out, int) {
        auto s = weak.lock();
        if (!s) return;
        std::string v = out.substr(0, out.find('\n'));
        if (v != s->ddc) {
          s->ddc = v;
          refresh();
        }
      });
    }

    void setNight(const std::shared_ptr<State>& s, bool on) {
      spawn({"sh", "-c",
             on ? "pkill -x gammastep; sleep 0.2; setsid -f gammastep -O " + std::to_string(get<int>("display.nightTemp", 4500))
                      + " >/dev/null 2>&1"
                : std::string("pkill -x gammastep")});
      s->night = on;
    }

    class MonitorLine : public Item {
    public:
      MonitorLine(const wm::Monitor& m) {
        m_icon = static_cast<Label*>(addChild(makeIcon(0xf0379, 26.0F, accent())));
        m_name = static_cast<Label*>(addChild(makeText(m.name, 13.0F, true)));
        char hz[48] = "";
        if (m.hz != 0.0) std::snprintf(hz, sizeof(hz), "  @ %.2f Hz", m.hz);
        m_detail = static_cast<Label*>(addChild(makeText(std::to_string(m.w) + " × " + std::to_string(m.h) + hz + "   ·   scale "
                                                             + jsNum(m.scale) + "   ·   at " + std::to_string(m.x) + ", " + std::to_string(m.y),
                                                         11.0F, false, dim())));
      }
      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float h = 52.0F;
        m_icon->measure(renderer);
        m_icon->setPosition(0.0F, std::round((h - m_icon->height()) / 2.0F));
        m_name->measure(renderer);
        m_detail->measure(renderer);
        const float colH = m_name->height() + m_detail->height();
        const float x = m_icon->width() + 14.0F;
        const float y = std::round((h - colH) / 2.0F);
        m_name->setPosition(x, y);
        m_detail->setPosition(x, y + m_name->height());
        setSize(width, h);
        return h;
      }

    private:
      Label* m_icon = nullptr;
      Label* m_name = nullptr;
      Label* m_detail = nullptr;
    };

    std::vector<wm::Monitor> monitorsOf(const State& s) {
      if (!s.monitors.empty()) return s.monitors;
      // Fall back to the Wayland outputs' logical sizes and positions.
      std::vector<wm::Monitor> out;
      if (CompositorPlatform* p = services().platform) {
        for (const auto& o : p->outputs()) {
          out.push_back(wm::Monitor{o.connectorName, o.logicalWidth, o.logicalHeight, 0.0, o.configuredScale(), o.logicalX, o.logicalY});
        }
      }
      return out;
    }

    // Slider for a gap or border size in whole pixels.
    std::unique_ptr<Slider> pxSlider(const char* key, char32_t icon, const char* label, int max) {
      const std::string path = std::string("windows.") + key;
      return std::make_unique<Slider>(sp::bind(path, [](const json&) { wm::settingsChanged(); }),
                                      SliderOpts{
                                          .icon = icon,
                                          .label = label,
                                          .toUnit = [max](const json& v) { return v.is_number() ? v.get<float>() / static_cast<float>(max) : 0.0F; },
                                          .fromUnit = [max](float u) -> json { return static_cast<int>(std::lround(u * static_cast<float>(max))); },
                                          .text = [](const json& v) { return std::to_string(v.is_number() ? v.get<int>() : 0) + "px"; },
                                          .step = 1.0F / static_cast<float>(max),
                                      });
    }

  } // namespace

  void buildDisplay(Column& page) {
    auto state = std::make_shared<State>();
    std::weak_ptr<State> weak = state;

    if (const auto cmd = wm::monitorsCommand(); !cmd.empty()) {
      run(cmd, [weak](const std::string& out, int) {
        auto s = weak.lock();
        if (!s) return;
        s->monitors = wm::parseMonitors(out);
        refresh();
      });
    }
    run({"pgrep", "-x", "gammastep"}, [weak](const std::string&, int code) {
      auto s = weak.lock();
      if (!s) return;
      s->night = code == 0;
      refresh();
    });
    checkDdc(weak);
    if (BrightnessService* b = brightness()) b->requestDdcRefresh();
    // The service changes on its own (levels, monitors found), so poll it.
    page.add<Poll>(500, [weak]() {
      auto s = weak.lock();
      if (!s) return;
      std::vector<BrightnessDisplay> now;
      for (const auto* d : screens()) now.push_back(*d);
      if (now == s->shown) return;
      const bool listChanged = now.size() != s->shown.size();
      s->shown = std::move(now);
      if (listChanged && s->detecting) s->detecting = false;
      refresh();
    });

    {
      auto* g = page.add<Group>("Monitors");
      g->add<Repeater>(
          [state]() {
            // One key per monitor and what it shows, so a changed mode rebuilds the line.
            std::vector<std::string> keys;
            for (const auto& m : monitorsOf(*state)) {
              keys.push_back(m.name + "|" + std::to_string(m.w) + "x" + std::to_string(m.h) + "|" + jsNum(m.hz) + "|" + jsNum(m.scale) + "|"
                             + std::to_string(m.x) + "," + std::to_string(m.y));
            }
            return keys;
          },
          [state](const std::string& key) -> std::unique_ptr<Item> {
            for (const auto& m : monitorsOf(*state)) {
              if (key.starts_with(m.name + "|")) return std::make_unique<MonitorLine>(m);
            }
            return nullptr;
          }
      );
      g->add<Chip>("Edit " + wm::name() + " config", 0xf107b)
          ->onClick([]() { spawn({"xdg-open", wm::configFile()}); })
          ->showIf([]() { return !wm::configFile().empty(); });
    }

    {
      auto* g = page.add<Group>("Brightness", "The screens' own backlight — a laptop panel, and monitors over their cable (DDC/CI).");
      g->add<Repeater>(
          []() {
            std::vector<std::string> keys;
            for (const auto* d : screens()) keys.push_back(d->id);
            return keys;
          },
          [](const std::string& id) -> std::unique_ptr<Item> {
            const auto level = [id]() {
              const BrightnessDisplay* d = screen(id);
              return d != nullptr ? d->brightness : 0.0F;
            };
            return std::make_unique<Slider>(
                Binding{
                    .get = [level]() -> json { return level(); },
                    .set = [id](const json& v) {
                      // Don't let a laptop panel go fully black; that's hard to undo.
                      float f = std::clamp(v.get<float>(), builtIn(id) ? 0.01F : 0.0F, 1.0F);
                      if (BrightnessService* b = brightness()) b->setBrightness(id, f);
                    },
                },
                SliderOpts{
                    .toUnit = [](const json& v) { return v.is_number() ? v.get<float>() : 0.0F; },
                    .fromUnit = [](float u) -> json { return u; },
                    .text = [](const json& v) {
                      return std::to_string(static_cast<int>(std::lround((v.is_number() ? v.get<double>() : 0.0) * 100.0))) + "%";
                    },
                    .iconFn = [level]() -> char32_t {
                      const float l = level();
                      return l < 0.34F ? 0xf00dd : l < 0.67F ? 0xf00de : 0xf00df;
                    },
                    .labelFn = [id]() -> std::string {
                      const BrightnessDisplay* d = screen(id);
                      return d == nullptr ? std::string() : screenName(*d) + "  ·  " + d->id;
                    },
                }
            );
          }
      );
      const auto why = [state]() -> std::string {
        if (state->detecting && screens().empty()) return "Looking for screens…";
        const std::string st = ddcState(*state);
        if (st == "missing") return "Monitors: ddcutil isn't installed — it's what talks to them over the cable.";
        if (st == "noi2c") return "Monitors: the i2c-dev kernel module isn't loaded, so ddcutil can't reach them.";
        if (st == "noaccess") return "Monitors: no permission for /dev/i2c-* yet — after the setup, log out and back in.";
        if (st == "none") return "No monitor answered over DDC/CI — switch DDC/CI on in the monitor's own menu, then look again.";
        return "";
      };
      g->add<Text>("", TextOpts{.color = dim(), .wrap = true})->bindText(why)->showIf([why]() { return !why().empty(); });
      g->add<Row>("Monitors over DDC/CI", "External monitors via ddcutil. A change takes a moment to reach them.",
                  std::make_unique<Switch>(sp::bind("brightness.ddc", [weak](const json&) {
                    // Toggling DDC detects monitors again.
                    if (auto s = weak.lock()) {
                      checkDdc(weak);
                      s->detecting = true;
                      s->looking.start(std::chrono::milliseconds(4000), [weak]() {
                        if (auto st = weak.lock()) {
                          st->detecting = false;
                          refresh();
                        }
                      });
                    }
                    if (BrightnessService* b = brightness()) b->requestDdcRescan();
                  })));
      g->add<Row>("Step", "Per brightness key press or bar scroll.",
                  std::make_unique<Stepper>(sp::bind("brightness.step"), StepperOpts{.from = 1, .to = 25, .suffix = "%"}));
      auto* row = g->add<HRow>(8.0F);
      row->add<Chip>("Set up monitor brightness", 0xf0493)
          ->onWhen([]() { return true; })
          ->onClick([]() { spawn({"kusanagi", "brightness", "setup"}); })
          ->showIf([state]() {
            const std::string st = ddcState(*state);
            return get<bool>("brightness.ddc", true) && (st == "missing" || st == "noi2c" || st == "noaccess");
          });
      row->add<Chip>("Look again", 0xf0450)
          ->labelFrom([state]() { return std::string(state->detecting ? "Looking…" : "Look again"); })
          ->onClick([weak]() {
            auto s = weak.lock();
            if (!s) return;
            s->detecting = true;
            checkDdc(weak);
            if (BrightnessService* b = brightness()) b->requestDdcRescan();
            // ddcutil detect takes a few seconds and the service has no "done" signal.
            s->looking.start(std::chrono::milliseconds(4000), [weak]() {
              if (auto st = weak.lock()) {
                st->detecting = false;
                refresh();
              }
            });
            refresh();
          });
    }

    if (wm::canSetLayout()) {
      auto* g = page.add<Group>("Windows", "Gaps and borders, applied to " + wm::name() + " as you drag.");
      g->add<Row>("Set from Kusanagi",
                  std::make_unique<Switch>(Binding{
                      .get = []() -> json { return get<bool>("windows.override", false); },
                      .set = [](const json& v) {
                        if (truthy(v)) {
                          // Start the sliders from what the compositor uses now.
                          const wm::Layout l = wm::layout();
                          write({{"windows.gapsIn", l.gapsIn}, {"windows.gapsOut", l.gapsOut}, {"windows.border", l.border},
                                 {"windows.override", true}});
                        } else {
                          set("windows.override", false);
                        }
                        wm::settingsChanged();
                      },
                  }))
          ->bindHint([]() {
            return get<bool>("windows.override", false) ? "Overrides your " + wm::name() + " config."
                                                         : "Off: your " + wm::name() + " config decides.";
          });
      auto* col = g->add<Column>(8.0F);
      col->enabledIf([]() { return get<bool>("windows.override", false); });
      col->addItem(pxSlider("gapsIn", 0xf084e, "Between windows", 40));
      col->addItem(pxSlider("gapsOut", 0xf0293, "Screen edges", 60));
      col->addItem(pxSlider("border", 0xf01fd, "Border", 10));
    }

    {
      auto* g = page.add<Group>("Night light", "Warmer colours for the evening (gammastep).");
      g->add<Row>("On", std::make_unique<Switch>(Binding{
                            .get = [state]() -> json { return state->night; },
                            .set = [state](const json& v) { setNight(state, truthy(v)); },
                        }));
      g->add<Slider>(sp::bind("display.nightTemp",
                          [weak](const json&) {
                            auto s = weak.lock();
                            if (!s) return;
                            s->retemp.start(std::chrono::milliseconds(400), [weak]() {
                              if (auto st = weak.lock(); st && st->night) setNight(st, true);
                            });
                          }),
                     SliderOpts{
                         .icon = 0xf0594,
                         .label = "Warmth",
                         .toUnit = [](const json& v) { return (6500.0F - (v.is_number() ? v.get<float>() : 4500.0F)) / 4000.0F; },
                         .fromUnit = [](float u) -> json { return static_cast<int>(std::lround((6500.0F - u * 4000.0F) / 100.0F)) * 100; },
                         .text = [](const json& v) { return std::to_string(v.is_number() ? v.get<int>() : 4500) + "K"; },
                     });
    }
  }

} // namespace kusanagi::sp
