// Settings > Lock & idle: lock screen engine and test, idle actions, lock and power menu designs, and
// the polkit password prompt.

#include "core/deferred_call.h"
#include "core/process/process.h"
#include "shell/kusanagi/kusanagi_ipc.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"

#include <cmath>
#include <cstdlib>
#include <memory>

namespace kusanagi::sp {

  namespace {

    std::string trimmed(std::string s) {
      s.erase(0, s.find_first_not_of(" \t\r\n"));
      s.erase(s.find_last_not_of(" \t\r\n") + 1);
      return s;
    }

    std::unique_ptr<Segmented> seg(const std::string& path, std::vector<Option> options, float width) {
      return std::make_unique<Segmented>(bind(path), std::move(options), width);
    }

  } // namespace

  void buildLock(Column& page) {
    {
      auto* g = page.add<Group>("Lock screen", "Super+L. Try the Kusanagi one with Test first — it unlocks itself after 30 s (or press Esc).");
      g->add<Segmented>(bind("lock.engine"), std::vector<Option>{{"hyprlock", "hyprlock"}, {"Kusanagi", "kusanagi"}}, 0.0F);
      auto* row = g->add<HRow>(8.0F);
      row->add<Chip>("Test the Kusanagi lock", 0xf0fc6)->onClick([]() { (void)ipc("lock test"); });
      row->add<Chip>("Lock now", 0xf033e)->onClick([]() { (void)ipc("lock lock"); });
      g->add<Text>("If Kusanagi ever crashes while locked, the screen stays locked (that's the point). Switch to a TTY "
                   "(Ctrl+Alt+F3), log in and run  WAYLAND_DISPLAY=wayland-0 swaylock  — it takes over and lets you unlock.",
                   TextOpts{.px = 10.0F, .color = dim(), .wrap = true});
    }

    {
      auto* g = page.add<Group>("When you're away",
                                "Kusanagi locks, turns the screens off and can suspend when you stop using the computer — no "
                                "hypridle or swayidle needed. Videos, Caffeine and game mode keep it awake.",
                                0xf04b2);
      // Idle status also changes with media, caffeine and game mode, not only with settings.
      page.add<Poll>(2000, []() { refresh(); });
      g->add<Row>("Handle idle", std::make_unique<Switch>(bind("idle.enabled")))->bindHint([]() {
        return "Idle: " + trimmed(ipc("idle status"));
      });
      g->add<Row>("Lock after",
                  seg("idle.lock", {{"Never", 0}, {"3 min", 3}, {"5 min", 5}, {"10 min", 10}, {"20 min", 20}}, 340.0F));
      g->add<Row>("Screens off after",
                  seg("idle.screenOff", {{"Never", 0}, {"5 min", 5}, {"10 min", 10}, {"15 min", 15}, {"30 min", 30}}, 340.0F));
      g->add<Row>("Suspend after", seg("idle.suspend", {{"Never", 0}, {"30 min", 30}, {"1 h", 60}, {"2 h", 120}}, 340.0F));
      g->add<Row>("Stay awake while media plays", "music or a video in any player", std::make_unique<Switch>(bind("idle.media")));
      g->add<Row>("Heads-up before locking", "a small pill 10 s before; move the mouse to stay",
                  std::make_unique<Switch>(bind("idle.notify")));

      // Another idle daemon running would lock twice.
      auto clash = std::make_shared<std::string>();
      auto* warn = g->add<Text>("", TextOpts{.px = 11.0F, .color = danger(), .wrap = true});
      warn->bindText([clash]() {
        std::string names;
        std::size_t at = 0;
        while (at < clash->size()) {
          const auto nl = clash->find('\n', at);
          const std::string n = clash->substr(at, nl == std::string::npos ? std::string::npos : nl - at);
          if (!n.empty()) names += (names.empty() ? "" : ", ") + n;
          if (nl == std::string::npos) break;
          at = nl + 1;
        }
        return names + " is running too and will also lock / blank the screen — take it out of your compositor's autostart.";
      });
      warn->showIf([clash]() { return !clash->empty() && get<bool>("idle.enabled", false); });
      std::weak_ptr<std::string> weak = clash;
      (void)process::runAsync(
          std::vector<std::string>{"sh", "-c", "for p in hypridle swayidle xidlehook; do pgrep -x $p >/dev/null && echo $p; done"},
          process::RunCallbacks{
              .onExit = [weak](process::RunResult r) {
                DeferredCall::callLater([weak, out = trimmed(r.out)]() {
                  auto c = weak.lock();
                  if (!c) return;
                  *c = out;
                  refresh();
                });
              },
          });
    }

    {
      auto* g = page.add<Group>("Design", "The Kusanagi lock screen. Try one with Test — it unlocks itself after 30 s.", 0xf033e);
      g->add<StylePicker>("lock",
                          std::vector<Option>{{"Centered", "center"},
                                              {"Card", "card"},
                                              {"Split", "split"},
                                              {"Minimal", "minimal"},
                                              {"Stacked", "stacked"},
                                              {"Terminal", "terminal"}},
                          bind("lock.style"), 128.0F);
    }

    {
      auto* g = page.add<Group>("Power menu", "Lock, log out, suspend, reboot, shut down (Super+` / Ctrl+Alt+Del). Letters work too: L E S R P.",
                                0xf0425);
      g->add<StylePicker>(
          "power",
          std::vector<Option>{{"Row", "row"}, {"Tiles", "tiles"}, {"List", "list"}, {"Fullscreen", "fullscreen"}, {"Pill", "pill"}},
          bind("power.style", [](const json&) { (void)ipc("panel-open session"); }), 128.0F);
    }

    {
      auto* g = page.add<Group>("Look", "Applies to the Kusanagi lock screen.");
      g->add<Slider>(bind("lock.blur"), SliderOpts{
                                            .icon = 0xf0e09,
                                            .label = "Background blur",
                                            .toUnit = [](const json& v) { return v.get<float>(); },
                                            .fromUnit = [](float u) { return json(std::round(u * 100.0) / 100.0); },
                                            .text = [](const json& v) { return std::to_string(std::lround(v.get<double>() * 100.0)) + "%"; },
                                        });
      g->add<Slider>(bind("lock.dim"), SliderOpts{
                                           .icon = 0xf050e,
                                           .label = "Darken",
                                           .toUnit = [](const json& v) { return v.get<float>() / 0.8F; },
                                           .fromUnit = [](float u) { return json(std::round(u * 0.8 * 100.0) / 100.0); },
                                           .text = [](const json& v) { return std::to_string(std::lround(v.get<double>() * 100.0)) + "%"; },
                                       });
      g->add<Row>("Clock", seg("lock.clock", {{"20:31", "HH:mm"}, {"20:31:07", "HH:mm:ss"}, {"8:31 PM", "h:mm AP"}}, 300.0F));
      g->add<Row>("Show your picture", "~/.face", std::make_unique<Switch>(bind("lock.avatar")));
      g->add<Row>("Media controls", std::make_unique<Switch>(bind("lock.media")));
      const char* user = std::getenv("USER");
      g->add<Row>("Greeting", "empty = your user name",
                  std::make_unique<Field>(bind("lock.greeting"),
                                          FieldOpts{.width = 220.0F, .placeholder = user != nullptr ? user : "", .applyOnEdit = true}));
    }

    {
      auto* g = page.add<Group>("Password prompts",
                                "When an app needs admin rights (GParted, mounting a disk, pkexec …) Kusanagi asks for your password "
                                "in its own style.",
                                0xf0483);
      g->add<Row>("Kusanagi asks for passwords", std::make_unique<Switch>(bind("polkit.enabled")))->bindHint([]() -> std::string {
        if (!get<bool>("polkit.enabled", true)) return "off — another polkit agent has to run";
        return polkitRegistered() ? "active" : "waiting: another polkit agent is running (or no polkit daemon)";
      });
    }
  }

} // namespace kusanagi::sp
