#include "shell/session/session_ipc.h"

#include "config/config_service.h"
#include "config/config_types.h"
#include "core/process/process.h"
#include "ipc/ipc_arg_parse.h"
#include "ipc/ipc_service.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/lockscreen/lock_screen.h"
#include "shell/session/session_action_meta.h"
#include "shell/session/session_action_runner.h"

#include <string>
#include <string_view>
#include <vector>

namespace {

  [[nodiscard]] SessionPanelActionConfig builtinAction(std::string_view action) {
    return SessionPanelActionConfig{.action = std::string(action)};
  }

  [[nodiscard]] std::string unknownSessionActionError(std::string_view action) {
    return "error: unknown session action \""
        + std::string(action)
        + "\" (try: lock, suspend, lock-and-suspend, logout, reboot, shutdown)\n";
  }

} // namespace

void registerSessionIpc(IpcService& ipc, SessionActionRunner& runner, LockScreen& lockScreen, ConfigService& config) {
  const auto dispatch = [&runner, &lockScreen, &config](const std::string& args) -> std::string {
    const auto parts = kusanagi::ipc::splitWords(args);
    if (parts.empty()) {
      return "error: session requires <lock|suspend|lock-and-suspend|logout|reboot|shutdown>\n";
    }

    const std::string& ipcAction = parts[0];
    const auto action = session_action::canonicalActionName(ipcAction);
    if (!action.has_value()) {
      return unknownSessionActionError(ipcAction);
    }

    if (*action == "lock") {
      if (!config.isLockScreenEnabled()) {
        return "error: lock screen disabled\n";
      }
      if (lockScreen.lock()) {
        return "ok\n";
      }
      return "error: lock screen unavailable\n";
    }
    if (*action == "lock_and_suspend") {
      if (!config.isLockScreenEnabled()) {
        runner.invoke(builtinAction("suspend"));
        return "ok\n";
      }
      runner.invoke(builtinAction(*action));
      return "ok\n";
    }

    runner.invoke(builtinAction(*action));
    return "ok\n";
  };

  ipc.bind(kusanagi::cli::msg::session, dispatch);

  // The `lock` target. `lock` uses this lock screen when lock.engine is "kusanagi" and hyprlock
  // otherwise; `test` shows this lock screen in test mode (no password; Esc or a 30 s timeout unlocks it).
  const auto lockTest = [&lockScreen, &config]() -> std::string {
    if (!config.isLockScreenEnabled()) {
      return "error: lock screen disabled\n";
    }
    if (lockScreen.isActive()) {
      return "error: already locked\n";
    }
    return lockScreen.lockTest() ? "ok\n" : "error: lock screen unavailable\n";
  };
  ipc.bind(kusanagi::cli::msg::lockTest, [lockTest](const std::string&) -> std::string { return lockTest(); });
  ipc.bind(kusanagi::cli::msg::lock, [&lockScreen, &config, lockTest](const std::string& args) -> std::string {
    const auto parts = kusanagi::ipc::splitWords(args);
    const std::string action = parts.empty() ? "lock" : parts[0];
    if (action == "test") {
      return lockTest();
    }
    if (action != "lock") {
      return "error: lock takes lock | test\n";
    }
    if (kusanagi::opt<std::string>("lock", "engine", "kusanagi") != "kusanagi") {
      // The rice's hyprlock config if it has one, else plain hyprlock, else swaylock. Does nothing if
      // hyprlock is already running.
      const bool started = process::runAsync(std::vector<std::string>{
          "sh", "-c",
          "pidof hyprlock || { c=\"$HOME/.config/rices/zei/generated/hyprlock.conf\"; if [ -f \"$c\" ]; then "
          "hyprlock -c \"$c\"; else hyprlock; fi; } || swaylock -f"
      });
      return started ? "ok\n" : "error: couldn't start hyprlock\n";
    }
    if (!config.isLockScreenEnabled()) {
      return "error: lock screen disabled\n";
    }
    if (lockScreen.lock()) {
      return "ok\n";
    }
    // No session-lock protocol here (some dwl builds): better another locker than an unlocked screen.
    (void)process::runAsync(std::vector<std::string>{
        "sh", "-c",
        "pidof hyprlock swaylock >/dev/null || swaylock -f || hyprlock || notify-send -u critical Kusanagi "
        "\"Couldn't lock the screen: this compositor has no session lock (ext-session-lock-v1)\""
    });
    return "error: lock screen unavailable — tried swaylock / hyprlock\n";
  });
}
