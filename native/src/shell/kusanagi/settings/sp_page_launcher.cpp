// Settings > Launcher: look and layout, search options and clipboard history.

#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"

namespace kusanagi::sp {

  void buildLauncher(Column& page) {
    {
      auto* g = page.add<Group>("App launcher", "Super+Space · type to search · = calculator · > run a command");
      g->add<Heading>("Design");
      g->add<StylePicker>("launcher",
                          std::vector<Option>{{"Card", "card", 0, "a list under the search"},
                                              {"Spotlight", "spotlight", 0, "just a search, results as you type"},
                                              {"Fullscreen", "fullscreen", 0, "every app in a big grid"},
                                              {"Side panel", "side", 0, "slides in from the left"}},
                          bind("launcher.style"));
      g->add<Row>("Position", std::make_unique<Segmented>(bind("launcher.position"),
                                                          std::vector<Option>{{"Upper third", "upper"}, {"Centre", "center"}},
                                                          240.0F))
          ->showIf([]() {
            const auto s = get<std::string>("launcher.style", "card");
            return s == "card" || s == "spotlight";
          });
      g->add<Row>("Layout", std::make_unique<Segmented>(bind("launcher.layout"),
                                                        std::vector<Option>{{"List", "list"}, {"Grid", "grid"}}, 220.0F))
          ->showIf([]() { return get<std::string>("launcher.style", "card") != "fullscreen"; });
      g->add<Row>("Icon size", std::make_unique<Stepper>(bind("launcher.iconSize"),
                                                         StepperOpts{.from = 20, .to = 64, .step = 4, .suffix = "px"}));
      g->add<Row>("Width", std::make_unique<Stepper>(bind("launcher.width"),
                                                     StepperOpts{.from = 480, .to = 900, .step = 40, .suffix = "px"}));
      g->add<Row>("Visible results", std::make_unique<Stepper>(bind("launcher.rows"), StepperOpts{.from = 3, .to = 12}));
      g->add<Row>("Show descriptions", std::make_unique<Switch>(bind("launcher.descriptions")));
      g->add<Row>("Most-used apps first", std::make_unique<Switch>(bind("launcher.sortByUsage")));
      g->add<Row>("Terminal", "for terminal apps and Shift+Enter on > commands", textField("launcher.terminal", 160.0F));
      auto* row = g->add<HRow>(8.0F);
      row->add<Chip>("Open launcher", 0xf003b)->onClick([]() { (void)ipc("panel-open launcher"); });
      row->add<Chip>("Forget app usage", 0xf02da)->onClick([]() {
        spawn({"sh", "-c", "printf '{\"counts\": {}}\\n' > \"$HOME/.config/kusanagi/launcher-usage.json\""});
      });
    }

    {
      auto* g = page.add<Group>("Search", "Start with  =  to calculate,  >  to run a command,  :  for emoji and symbols,  /  for files,  ?  for the web.");
      g->add<Row>("Kusanagi commands", "lock, settings pages, presets, recording… show up when you type",
                  std::make_unique<Switch>(bind("launcher.commands")));
      g->add<Row>("Web search as the last result", std::make_unique<Switch>(bind("launcher.webSearch")));
      g->add<Row>("Search engine", std::make_unique<Segmented>(
                                       bind("launcher.searchEngine"),
                                       std::vector<Option>{{"DuckDuckGo", "https://duckduckgo.com/?q=%s"},
                                                           {"Google", "https://www.google.com/search?q=%s"},
                                                           {"Brave", "https://search.brave.com/search?q=%s"},
                                                           {"Startpage", "https://www.startpage.com/do/search?q=%s"}},
                                       360.0F));
      g->add<Row>("…or your own", "%s is the search",
                  std::make_unique<Field>(bind("launcher.searchEngine"),
                                          FieldOpts{
                                              .width = 300.0F,
                                              .applyOnEdit = true,
                                              .convert = [](const std::string& t) -> std::optional<json> {
                                                if (t.find("%s") == std::string::npos) return std::nullopt;
                                                return json(t);
                                              },
                                          }));
    }

    {
      auto* g = page.add<Group>("Clipboard", "Super+V · Enter copies · Delete removes");
      auto* row = g->add<HRow>(8.0F);
      row->add<Chip>("Open history", 0xf0147)->onClick([]() { (void)ipc("panel-toggle clipboard"); });
      row->add<Chip>("Clear history", 0xf0a7a)->onClick([]() { spawn({"cliphist", "wipe"}); });
    }
  }

} // namespace kusanagi::sp
