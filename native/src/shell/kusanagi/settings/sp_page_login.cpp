// Settings > Login screen: Kusanagi as the greetd greeter. Actions go through `kusanagi greeter`; install
// and uninstall need sudo, so they run in a terminal.

#include "core/deferred_call.h"
#include "core/process/process.h"
#include "shell/kusanagi/kusanagi_ipc.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>

namespace kusanagi::sp {

  namespace {

    std::string readFile(const std::string& path) {
      std::ifstream f(path);
      std::stringstream ss;
      ss << f.rdbuf();
      return ss.str();
    }

    std::string trimmed(std::string s) {
      s.erase(0, s.find_first_not_of(" \t\r\n"));
      s.erase(s.find_last_not_of(" \t\r\n") + 1);
      return s;
    }

    // Parsed from /etc/greetd/config.toml (world-readable): is greetd set up, and what does it run.
    struct Greetd {
      std::string text;
      [[nodiscard]] bool has() const { return !text.empty(); }
      [[nodiscard]] bool installed() const {
        static const std::regex re(R"re(command\s*=\s*"kusanagi-greeter")re");
        return std::regex_search(text, re);
      }
      [[nodiscard]] std::string current() const {
        static const std::regex re(R"re(\[default_session\][^\[]*?command\s*=\s*"([^"]*)")re");
        std::smatch m;
        return std::regex_search(text, m, re) ? m[1].str() : std::string();
      }
    };

    struct State {
      Greetd greetd;
      std::string note;
      std::string pendingEngine; // Picked, but the CLI hasn't answered yet.
    };

    std::string configDir() {
      const char* xdg = std::getenv("XDG_CONFIG_HOME");
      return (xdg != nullptr && *xdg != '\0' ? std::string(xdg) : expandHome("~/.config")) + "/kusanagi";
    }

    // `kusanagi greeter engine` prints qml (the default) or native.
    std::string engine() {
      const std::string e = trimmed(readFile(configDir() + "/greeter-engine"));
      return e.empty() ? "qml" : e;
    }

    // Installed Wayland sessions, sorted by file name: value is the file stem, label is its Name=.
    std::vector<Option> sessions() {
      std::vector<std::filesystem::path> files;
      std::error_code ec;
      for (const auto& e : std::filesystem::directory_iterator("/usr/share/wayland-sessions", ec)) {
        if (e.path().extension() == ".desktop" && e.is_regular_file(ec)) files.push_back(e.path());
      }
      std::ranges::sort(files);
      std::vector<Option> out;
      for (const auto& f : files) {
        std::ifstream in(f);
        std::string name;
        for (std::string line; std::getline(in, line);) {
          if (line.starts_with("Name=")) {
            name = line.substr(5);
            break;
          }
        }
        const std::string id = f.stem().string();
        out.push_back(Option{.label = name.empty() ? id : name, .value = id});
      }
      return out;
    }

    // Admin steps ask for a password, so run them in a terminal.
    void inTerminal(const std::string& cmd) {
      std::string term = get<std::string>("launcher.terminal", "");
      if (term.empty()) term = "foot";
      spawn({term, "-e", "sh", "-c", cmd + "; printf '\\nPress Enter to close '; read x"});
    }

  } // namespace

  void buildLogin(Column& page) {
    auto st = std::make_shared<State>();
    const auto readConf = [st]() {
      std::string t = readFile("/etc/greetd/config.toml");
      if (t == st->greetd.text) return false;
      st->greetd.text = std::move(t);
      return true;
    };
    readConf();
    // Watch the greetd config and the engine file the CLI writes.
    page.add<Poll>(2000, [readConf]() {
      (void)readConf();
      refresh();
    });

    const auto run = [st](std::vector<std::string> args, std::string msg) {
      std::vector<std::string> argv{cliCommand(), "greeter"};
      argv.insert(argv.end(), args.begin(), args.end());
      spawn(std::move(argv));
      st->note = std::move(msg);
      refresh();
    };

    {
      auto* g = page.add<Group>("Login screen", std::string(), 0xf0004);
      g->bindHint([st]() -> std::string {
        if (st->greetd.installed()) return "Kusanagi greets you when you log in — your lock screen design, with you and your session to pick.";
        if (st->greetd.has()) {
          return "greetd (your login daemon) uses \"" + st->greetd.current() + "\" now. Kusanagi can be the login screen instead.";
        }
        return "Needs greetd, the login daemon (install it from your distro).";
      });
      auto* flow = g->add<Flow>(6.0F);
      flow->add<Chip>("Use Kusanagi to log in…", 0xf0415)
          ->onWhen([]() { return true; })
          ->onClick([]() { inTerminal(cliCommand() + " greeter install"); })
          ->showIf([st]() { return st->greetd.has() && !st->greetd.installed(); });
      flow->add<Chip>("Preview", 0xf0208)->onClick([run]() { run({"preview"}, ""); });
      flow->add<Chip>("Sync now", 0xf0450)
          ->onClick([run]() { run({"sync"}, "Synced — it shows your current design, colours and wallpaper."); })
          ->showIf([st]() { return st->greetd.installed(); });
      flow->add<Chip>("Go back…", 0xf0a7a)
          ->labelFrom([st]() {
            return "Go back to " + std::string(st->greetd.current() == "kusanagi-greeter" ? "the old login" : "it") + "…";
          })
          ->onClick([]() { inTerminal(cliCommand() + " greeter uninstall"); })
          ->showIf([st]() { return st->greetd.installed(); });
      g->add<Text>("", TextOpts{.px = 11.0F, .color = ok()})
          ->bindText([st]() { return st->note; })
          ->showIf([st]() { return !st->note.empty(); });
      g->add<Text>("Install asks for your password once (sudo): it adds cage (a tiny compositor for the login screen), "
                   "/var/lib/kusanagi-greeter and /usr/local/bin/kusanagi-greeter, and points /etc/greetd/config.toml at it — "
                   "the old one is kept, and if the Kusanagi login ever fails to start the old one takes over. "
                   "TTY logins (Ctrl+Alt+F1 / F2) stay as they are. It shows up after a reboot (greetd only reads its config when it starts).",
                   TextOpts{.px = 10.0F, .color = dim(), .wrap = true});
    }

    {
      auto* g = page.add<Group>("Design", "Your lock screen's design, or a different one just for logging in.");
      g->add<StylePicker>("lock",
                          std::vector<Option>{{"Same as lock", ""},
                                              {"Centered", "center"},
                                              {"Card", "card"},
                                              {"Split", "split"},
                                              {"Minimal", "minimal"},
                                              {"Stacked", "stacked"},
                                              {"Terminal", "terminal"}},
                          bind("greeter.style"), 112.0F);
    }

    {
      auto* g = page.add<Group>("Defaults");
      std::vector<Option> ss = sessions();
      if (!ss.empty()) {
        const json first = ss.front().value;
        const float w = std::min(420.0F, 120.0F * static_cast<float>(std::max<std::size_t>(1, ss.size())));
        g->add<Row>("Session", "picked first (the button at the bottom left switches)",
                    std::make_unique<Segmented>(
                        Binding{
                            .get = [first]() -> json {
                              const json v = value("greeter.session");
                              return truthy(v) ? v : first;
                            },
                            .set = [](const json& v) { set("greeter.session", v); },
                        },
                        std::move(ss), w));
      } else {
        // No sessions found: show an empty track.
        g->add<Row>("Session", "picked first (the button at the bottom left switches)",
                    std::make_unique<Segmented>(bind("greeter.session"), std::vector<Option>{}, 120.0F));
      }
      const char* user = std::getenv("USER");
      g->add<Row>("User", "blank: the first person on this machine",
                  textField("greeter.user", 200.0F, user != nullptr ? user : "", true));

      // Which program draws the login screen (`kusanagi greeter engine qml|native`).
      g->add<Row>("Engine", "native = kusanagi-shell --greeter (falls back to the QML one if it can't start)",
                  std::make_unique<Segmented>(
                      Binding{
                          .get = [st]() -> json { return st->pendingEngine.empty() ? engine() : st->pendingEngine; },
                          .set = [st](const json& v) {
                            if (!v.is_string()) return;
                            std::weak_ptr<State> weak = st;
                            st->pendingEngine = v.get<std::string>();
                            const bool started = process::runAsync(
                                std::vector<std::string>{cliCommand(), "greeter", "engine", v.get<std::string>()},
                                process::RunCallbacks{
                                    .onExit = [weak](process::RunResult r) {
                                      // The CLI's answer: the new engine, what to run next, or why it failed.
                                      std::string msg;
                                      std::istringstream in(trimmed(r.out + "\n" + r.err));
                                      for (std::string l; std::getline(in, l);) {
                                        l = trimmed(l);
                                        if (!l.empty()) msg += (msg.empty() ? "" : " · ") + l;
                                      }
                                      DeferredCall::callLater([weak, msg]() {
                                        auto s = weak.lock();
                                        if (!s) return;
                                        s->note = msg;
                                        s->pendingEngine.clear();
                                        refresh();
                                      });
                                    },
                                });
                            if (!started) st->pendingEngine.clear();
                          },
                      },
                      std::vector<Option>{{"QML", "qml"}, {"Native", "native"}}, 240.0F));
    }
  }

} // namespace kusanagi::sp
