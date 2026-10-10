#pragma once

// Kusanagi's IPC targets that don't belong to a surface: preset, bar-layout, idle, record, updates,
// launcher-actions, brightness-get, brightness-detect and bluetooth-summary. bin/kusanagi's native_call() maps
// `kusanagi msg <target> <fn>` onto them. The recorder, updates and idle helpers are also used by Settings
// and the control panel.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

class BluetoothService;
class BrightnessService;
class GameModeService;
class IdleInhibitor;
class IpcService;
class LauncherPanel;
class MprisService;

namespace kusanagi {

  struct IpcServices {
    LauncherPanel* launcher = nullptr;
    BrightnessService* brightness = nullptr;
    BluetoothService* bluetooth = nullptr;
    IdleInhibitor* caffeine = nullptr;
    MprisService* mpris = nullptr;
    GameModeService* gameMode = nullptr;
  };

  void registerIpc(IpcService& ipc, const IpcServices& services);

  // The kusanagi command the shell runs: `kusanagi` on PATH unless KUSANAGI_CLI overrides it.
  [[nodiscard]] std::string cliCommand();

  namespace recorder {
    // off, replay, record or stream, read from $XDG_RUNTIME_DIR/kusanagi/record.json.
    [[nodiscard]] std::string mode();
    // Runs `kusanagi record <what>` with the recorder settings. replay, record and stream toggle.
    void run(std::string_view what);
    // One-button control: saves a clip while the replay buffer runs, stops a recording or stream, and
    // otherwise starts recording.
    void smart();
  } // namespace recorder

  namespace updates {
    void check();                 // runs `kusanagi updates raw` in the background, notifies if updates.notify
    [[nodiscard]] std::string count(); // the last count, "?" until one is known
    void upgrade();               // opens a terminal and counts again 2 minutes later
    // For the updates settings page.
    struct Status {
      int count = 0;
      bool known = false;      // false until a check worked
      bool checking = false;
      long long checkedAt = 0; // ms since the epoch, 0 = never
      std::vector<std::pair<std::string, std::string>> list; // { name, source }
    };
    [[nodiscard]] Status status();
  } // namespace updates

  // "off", "armed", or "awake: <reason>" (caffeine, game mode, media playing).
  [[nodiscard]] std::string idleStatus(const IpcServices& services);

  // Whether the shell's polkit agent is registered. Set by the application, shown in the lock settings.
  void setPolkitRegistered(bool registered);
  [[nodiscard]] bool polkitRegistered();

} // namespace kusanagi
