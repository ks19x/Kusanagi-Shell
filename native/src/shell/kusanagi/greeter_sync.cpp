#include "shell/kusanagi/greeter_sync.h"

#include "core/log.h"
#include "core/process/process.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "util/file_utils.h"

#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iterator>
#include <regex>

#include <nlohmann/json.hpp>

namespace kusanagi {

  namespace {

    constexpr Logger kLog("greeter-sync");
    constexpr const char* kGreetdConfig = "/etc/greetd/config.toml";

    std::string readFile(const std::string& path) {
      std::ifstream in(path);
      return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    // True when greetd starts kusanagi-greeter, as `kusanagi greeter install` sets it up.
    bool greeterInstalled() {
      static const std::regex installed(R"(command\s*=\s*"kusanagi-greeter")");
      return std::regex_search(readFile(kGreetdConfig), installed);
    }

    // Everything the login screen shows. The theme colours are derived from colors.json and the look palette
    // and accent, so the colours file stands in for them.
    std::string watchedState() {
      const nlohmann::json& s = settings();
      auto get = [&s](const char* section, const char* key) {
        const auto sec = s.find(section);
        if (sec == s.end() || !sec->is_object()) {
          return nlohmann::json();
        }
        const auto it = sec->find(key);
        return it == sec->end() ? nlohmann::json() : *it;
      };
      const std::string dir = FileUtils::configDir();
      nlohmann::json w = nlohmann::json::array({
          get("greeter", "style"), get("greeter", "user"), get("greeter", "session"), get("lock", "style"),
          get("lock", "blur"), get("lock", "dim"), get("lock", "clock"), get("lock", "avatar"), get("lock", "greeting"),
          get("look", "font"), get("look", "palette"), get("look", "accent"), readFile(dir + "/colors.json"),
          readFile(dir + "/wallpaper"),
      });
      return w.dump();
    }

  } // namespace

  GreeterSync& GreeterSync::instance() {
    static GreeterSync sync;
    return sync;
  }

  void GreeterSync::start(FileWatcher& watcher) {
    (void)watcher.watch(kGreetdConfig, [this]() { check(false); });
    // Once per start, to pick up the session you just logged in to.
    check(true);
  }

  void GreeterSync::onConfigReload() { check(false); }

  void GreeterSync::check(bool force) {
    // A dev test shell runs on a copy of the settings and must never sync the real login screen.
    if (std::getenv("KUSANAGI_DEV_SLOT") != nullptr) {
      return;
    }
    const bool installed = greeterInstalled();
    const bool becameInstalled = installed && !m_installed;
    m_installed = installed;
    if (!installed) {
      m_later.stop();
      m_watched.clear();
      return;
    }
    std::string watched = watchedState();
    if (!force && !becameInstalled && watched == m_watched) {
      return;
    }
    m_watched = std::move(watched);
    m_later.start(std::chrono::milliseconds(4000), []() {
      kLog.info("login screen: syncing");
      if (!process::runAsync({"kusanagi", "greeter", "sync", "-q"})) {
        kLog.warn("couldn't run kusanagi greeter sync");
      }
    });
  }

} // namespace kusanagi
