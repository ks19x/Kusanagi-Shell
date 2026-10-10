#include "shell/kusanagi/game_mode.h"

#include "cli/schema_msg.h"
#include "compositors/compositor_detect.h"
#include "compositors/compositor_platform.h"
#include "core/log.h"
#include "core/process/process.h"
#include "ipc/ipc_service.h"
#include "shell/kusanagi/kusanagi_style.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>

namespace {
  constexpr Logger kLog("gamemode");
  GameModeService* gInstance = nullptr;

  std::string configHome() {
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg != nullptr && xdg[0] != '\0') return xdg;
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr ? home : "") + "/.config";
  }
} // namespace

GameModeService::GameModeService(CompositorPlatform& platform) : m_platform(platform) {
  gInstance = this;
  readMangoDefaults();
}

GameModeService::~GameModeService() {
  if (m_active) {
    effects(true); // don't leave the compositor without its effects
  }
  if (m_feralPid) process::terminateTracked(*m_feralPid);
  if (gInstance == this) gInstance = nullptr;
}

GameModeService* GameModeService::instance() noexcept { return gInstance; }

bool GameModeService::quiet() const { return m_active && kusanagi::opt<bool>("gamemode", "quiet", true); }

// Reads the user's own mango values for the effects game mode turns off, so they come back exactly.
void GameModeService::readMangoDefaults() {
  static const std::regex line("^(blur|shadows|animations|layer_animations)=(\\d+)");
  for (const char* file : {"/mango/config.conf", "/mango/rice.conf"}) {
    std::ifstream in(configHome() + file);
    for (std::string l; std::getline(in, l);) {
      std::smatch m;
      if (std::regex_search(l, m, line)) m_mangoDefaults[m[1].str()] = std::stoi(m[2].str());
    }
  }
}

void GameModeService::effects(bool on) {
  if (!kusanagi::opt<bool>("gamemode", "effects", true)) return;
  if (compositors::isMango()) {
    std::string cmd;
    for (const auto& [key, value] : m_mangoDefaults) {
      cmd += "mmsg dispatch setoption," + key + "," + std::to_string(on ? value : 0) + "; ";
    }
    (void)process::runAsync(cmd);
  } else if (compositors::isHyprland()) {
    const std::string v = on ? "true" : "false";
    if (std::filesystem::exists(configHome() + "/hypr/hyprland.lua")) {
      (void)process::runAsync(std::vector<std::string>{
          "hyprctl", "eval",
          "hl.config({ decoration = { blur = { enabled = " + v + " }, shadow = { enabled = " + v + " } }, animations = { enabled = " + v + " } })"});
    } else {
      const std::string n = on ? "1" : "0";
      (void)process::runAsync(std::vector<std::string>{
          "hyprctl", "--batch",
          "keyword decoration:blur:enabled " + n + "; keyword decoration:shadow:enabled " + n + "; keyword animations:enabled " + n});
    }
  }
  // niri keeps blur and animations in its config file, which game mode leaves alone.
}

void GameModeService::enable(bool byHand) {
  m_offGrace.stop();
  if (byHand) m_manual = true;
  if (m_active) return;
  m_active = true;
  effects(false);
  if (kusanagi::opt<bool>("gamemode", "feral", true)) m_feralPid = process::launchDetachedTracked({"gamemoded", "-r"});
  kLog.info("on{}", byHand ? " (by hand)" : " (fullscreen)");
  if (m_changed) m_changed(true, byHand);
}

void GameModeService::disable(bool byHand) {
  m_offGrace.stop();
  m_manual = false;
  if (!m_active) return;
  m_active = false;
  effects(true);
  if (m_feralPid) {
    process::terminateTracked(*m_feralPid);
    m_feralPid.reset();
  }
  kLog.info("off");
  if (m_changed) m_changed(false, byHand);
}

void GameModeService::onToplevelsChanged() {
  const auto top = m_platform.activeToplevel();
  bool fullscreen = false;
  if (top.has_value()) {
    fullscreen = top->fullscreen;
    m_lastHandle = top->handle;
  } else if (m_lastHandle != nullptr && m_platform.containsWlrToplevelHandle(m_lastHandle)) {
    // Overlays like the launcher leave no active window while the game is still there: no change.
    return;
  }
  if (fullscreen == m_fullscreen) return;
  m_fullscreen = fullscreen;
  if (!kusanagi::opt<bool>("gamemode", "auto", true)) return;
  if (fullscreen) {
    enable(false);
  } else if (!m_manual) {
    const int grace = kusanagi::opt<int>("gamemode", "grace", 800);
    m_offGrace.start(std::chrono::milliseconds(grace), [this]() {
      if (!m_fullscreen && !m_manual) disable(false);
    });
  }
}

void GameModeService::registerIpc(IpcService& ipc) {
  ipc.bind(kusanagi::cli::msg::gamemode, [this](const std::string& args) -> std::string {
    const std::string a = args.empty() ? "toggle" : args.substr(0, args.find(' '));
    if (a == "toggle") toggle();
    else if (a == "on") enable(true);
    else if (a == "off") disable(true);
    else if (a == "auto-on" || a == "auto-off") {
      kusanagi::setOption("gamemode", "auto", a == "auto-on");
      if (a == "auto-on" && m_fullscreen) enable(false);
    } else if (a != "status") return "error: unknown action (toggle | on | off | auto-on | auto-off | status)\n";
    return std::string(m_active ? (m_manual ? "on (by hand)" : "on (fullscreen)") : "off") + "\n";
  });
}
