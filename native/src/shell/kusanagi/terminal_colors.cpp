#include "shell/kusanagi/terminal_colors.h"

#include "config/atomic_file.h"
#include "core/log.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/terminal_theme.h"
#include "ui/palette.h"
#include "util/file_utils.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string_view>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace kusanagi {

  namespace {

    constexpr Logger kLog("terminal-colors");
    namespace fs = std::filesystem;
    using terminal::Kind;

    std::string readFile(const fs::path& path) {
      std::ifstream in(path);
      return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    // Writes `text` unless the file already holds it. A config of the user's is copied to <name>.bak-kusanagi the
    // first time Kusanagi changes it. True when the file changed.
    bool writeIfChanged(const fs::path& path, const std::string& text, bool backup) {
      std::error_code ec;
      const bool exists = fs::exists(path, ec);
      if (exists && readFile(path) == text) return false;
      if (!exists && text.empty()) return false;
      fs::create_directories(path.parent_path(), ec);
      if (backup && exists) {
        const fs::path saved = path.string() + ".bak-kusanagi";
        if (!fs::exists(saved, ec)) fs::copy_file(path, saved, ec);
      }
      if (!writeTextFileAtomic(path, text)) {
        kLog.warn("couldn't write {}", path.string());
        return false;
      }
      return true;
    }

    fs::path configHome() {
      if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && xdg[0] != '\0') return xdg;
      const char* home = std::getenv("HOME");
      return fs::path(home != nullptr ? home : "") / ".config";
    }

    bool installed(std::string_view program) {
      const char* path = std::getenv("PATH");
      std::string_view dirs = path != nullptr ? path : "/usr/local/bin:/usr/bin:/bin";
      while (!dirs.empty()) {
        const std::size_t colon = dirs.find(':');
        const std::string_view dir = dirs.substr(0, colon);
        if (!dir.empty() && ::access((std::string(dir) + "/" + std::string(program)).c_str(), X_OK) == 0) return true;
        if (colon == std::string_view::npos) break;
        dirs.remove_prefix(colon + 1);
      }
      return false;
    }

    struct Target {
      Kind kind;
      const char* program;
      const char* themeFile; // in ~/.config/kusanagi/terminal
      fs::path config;
    };

    std::array<Target, 4> targets() {
      const fs::path home = configHome();
      fs::path kitty = home / "kitty";
      if (const char* dir = std::getenv("KITTY_CONFIG_DIRECTORY"); dir != nullptr && dir[0] != '\0') kitty = dir;
      std::error_code ec;
      // Ghostty 1.2 reads config.ghostty, older ones config; use whichever is there.
      const fs::path ghostty = fs::exists(home / "ghostty/config.ghostty", ec) ? home / "ghostty/config.ghostty"
                                                                               : home / "ghostty/config";
      return {{
          {Kind::Foot, "foot", "foot.ini", home / "foot/foot.ini"},
          {Kind::Kitty, "kitty", "kitty.conf", kitty / "kitty.conf"},
          {Kind::Alacritty, "alacritty", "alacritty.toml", home / "alacritty/alacritty.toml"},
          {Kind::Ghostty, "ghostty", "ghostty", ghostty},
      }};
    }

    terminal::Rgb rgb(const Color& c) { return {c.r, c.g, c.b}; }

    // True when the process runs in this shell's session: the same Wayland display and runtime dir. Terminals of
    // another login (or of a dev test shell) are left alone.
    bool sameSession(const fs::path& proc) {
      const std::string env = readFile(proc / "environ");
      const auto value = [&env](std::string_view key) -> std::string {
        for (std::size_t at = 0; at < env.size();) {
          const std::size_t end = env.find('\0', at);
          const std::string_view entry(env.data() + at, (end == std::string::npos ? env.size() : end) - at);
          if (entry.size() > key.size() && entry.starts_with(key) && entry[key.size()] == '=') {
            return std::string(entry.substr(key.size() + 1));
          }
          if (end == std::string::npos) break;
          at = end + 1;
        }
        return {};
      };
      for (const char* key : {"WAYLAND_DISPLAY", "XDG_RUNTIME_DIR"}) {
        const char* mine = std::getenv(key);
        if (value(key) != (mine != nullptr ? mine : "")) return false;
      }
      return true;
    }

    // The pseudo-terminals whose other end belongs to a terminal emulator of this session. Only those get the
    // live recolour, so the escape codes never land in some other program that reads a terminal (an IDE,
    // `script`).
    std::set<int> terminalWindows() {
      static const std::set<std::string> kEmulators{"foot", "kitty", "alacritty", "ghostty", "wezterm-gui"};
      std::set<int> ttys;
      const uid_t me = ::getuid();
      std::error_code ec;
      for (const auto& proc : fs::directory_iterator("/proc", ec)) {
        const std::string pid = proc.path().filename().string();
        if (pid.empty() || pid.find_first_not_of("0123456789") != std::string::npos) continue;
        struct stat st{};
        if (::stat(proc.path().c_str(), &st) != 0 || st.st_uid != me) continue;
        std::string comm = readFile(proc.path() / "comm");
        if (!comm.empty() && comm.back() == '\n') comm.pop_back();
        if (!kEmulators.contains(comm) || !sameSession(proc.path())) continue;
        std::error_code fdEc;
        for (const auto& fd : fs::directory_iterator(proc.path() / "fd", fdEc)) {
          std::error_code linkEc;
          const std::string target = fs::read_symlink(fd.path(), linkEc).string();
          if (target != "/dev/ptmx" && target != "/dev/pts/ptmx") continue;
          const std::string info = readFile(proc.path() / "fdinfo" / fd.path().filename());
          const std::size_t at = info.find("tty-index:");
          if (at != std::string::npos) ttys.insert(std::atoi(info.c_str() + at + 10));
        }
      }
      return ttys;
    }

    void sendToOpenWindows(const std::string& sequence) {
      int sent = 0;
      for (const int tty : terminalWindows()) {
        const std::string path = "/dev/pts/" + std::to_string(tty);
        const int fd = ::open(path.c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (::write(fd, sequence.data(), sequence.size()) == static_cast<ssize_t>(sequence.size())) ++sent;
        ::close(fd);
      }
      kLog.debug("recoloured {} open terminal window(s)", sent);
    }

  } // namespace

  std::string terminalsFound() {
    std::string names;
    for (const auto& [program, name] : {std::pair{"foot", "foot"}, std::pair{"kitty", "kitty"},
                                        std::pair{"alacritty", "Alacritty"}, std::pair{"ghostty", "Ghostty"},
                                        std::pair{"wezterm", "WezTerm"}}) {
      if (installed(program)) names += (names.empty() ? "" : ", ") + std::string(name);
    }
    return names;
  }

  TerminalColors& TerminalColors::instance() {
    static TerminalColors colors;
    return colors;
  }

  void TerminalColors::start() {
    m_started = true;
    // A theme change cross-fades through many in-between palettes; act once it has settled.
    m_paletteConn = paletteChanged().connect([this]() { m_later.start(std::chrono::milliseconds(700), [this]() { apply(); }); });
    // Not right away: at startup the theme may still be loading.
    m_later.start(std::chrono::milliseconds(1500), [this]() { apply(); });
  }

  void TerminalColors::onConfigReload() {
    if (m_started) m_later.start(std::chrono::milliseconds(300), [this]() { apply(); });
  }

  void TerminalColors::apply() {
    const bool on = opt<bool>("terminal", "colors", false);
    const fs::path themes = fs::path(FileUtils::configDir()) / "terminal";

    if (!on) {
      for (const Target& t : targets()) {
        std::error_code ec;
        if (!fs::exists(t.config, ec)) continue;
        const std::string text = readFile(t.config);
        const std::string rest = terminal::unhook(t.kind, text, (themes / t.themeFile).string());
        if (rest != text && rest.find_first_not_of(" \t\n") == std::string::npos) {
          fs::remove(t.config, ec); // a config Kusanagi made just for the hook
        } else {
          (void)writeIfChanged(t.config, rest, false);
        }
      }
      if (m_on) {
        sendToOpenWindows(terminal::resetSequence());
        kLog.info("off: terminals have their own colours again");
      }
      m_on = false;
      return;
    }

    const terminal::Scheme scheme = terminal::makeScheme(rgb(colorForRole(ColorRole::Surface)),
                                                         rgb(colorForRole(ColorRole::OnSurface)),
                                                         rgb(colorForRole(ColorRole::Primary)));
    bool changed = false;
    changed |= writeIfChanged(themes / "foot.ini", terminal::footTheme(scheme), false);
    changed |= writeIfChanged(themes / "kitty.conf", terminal::kittyTheme(scheme), false);
    changed |= writeIfChanged(themes / "alacritty.toml", terminal::alacrittyTheme(scheme), false);
    changed |= writeIfChanged(themes / "ghostty", terminal::ghosttyTheme(scheme), false);
    if (installed("wezterm")) {
      changed |= writeIfChanged(configHome() / "wezterm/colors/Kusanagi.toml", terminal::weztermTheme(scheme), false);
    }

    for (const Target& t : targets()) {
      if (!installed(t.program)) continue;
      std::error_code ec;
      const std::string text = fs::exists(t.config, ec) ? readFile(t.config) : std::string();
      if (writeIfChanged(t.config, terminal::hook(t.kind, text, (themes / t.themeFile).string()), true)) {
        kLog.info("hooked terminal colours into {}", t.config.string());
      }
    }

    if (changed || !m_on) sendToOpenWindows(terminal::liveSequence(scheme));
    m_on = true;
  }

} // namespace kusanagi
