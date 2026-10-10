#pragma once

// Game mode: turns on when the focused window goes fullscreen (gamemode.auto) or by hand through the
// `gamemode` IPC target. It turns off after a grace period once nothing is fullscreen, so alt-tab doesn't
// flap it, unless it was switched on by hand. While on, compositor blur, shadows and animations are off
// (gamemode.effects) and feral gamemoded is held (gamemode.feral).

#include <functional>
#include <map>
#include <optional>
#include <string>

#include "core/timer_manager.h"

class CompositorPlatform;
class IpcService;
struct zwlr_foreign_toplevel_handle_v1;

class GameModeService {
public:
  explicit GameModeService(CompositorPlatform& platform);
  ~GameModeService();

  // The running service, or nullptr. Bar modules read it.
  [[nodiscard]] static GameModeService* instance() noexcept;

  void registerIpc(IpcService& ipc);
  void onToplevelsChanged();

  void enable(bool byHand);
  void disable(bool byHand);
  void toggle() { m_active ? disable(true) : enable(true); }

  [[nodiscard]] bool active() const noexcept { return m_active; }
  [[nodiscard]] bool manual() const noexcept { return m_manual; }
  // gamemode.quiet: the shell's own background work pauses while game mode is on.
  [[nodiscard]] bool quiet() const;

  // Called with (active, byHand) on every switch, for the OSD.
  void setChangeCallback(std::function<void(bool, bool)> callback) { m_changed = std::move(callback); }

private:
  void effects(bool on);
  void readMangoDefaults();

  CompositorPlatform& m_platform;
  bool m_active = false;
  bool m_manual = false;
  bool m_fullscreen = false;
  zwlr_foreign_toplevel_handle_v1* m_lastHandle = nullptr; // the window that was focused last
  Timer m_offGrace;
  std::optional<int> m_feralPid;
  std::map<std::string, int> m_mangoDefaults{{"blur", 1}, {"shadows", 1}, {"animations", 1}, {"layer_animations", 1}};
  std::function<void(bool, bool)> m_changed;
};
