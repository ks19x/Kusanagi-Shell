#pragma once

// Keeps the login screen (`kusanagi greeter`) in step with the shell. When greetd is set up to start
// kusanagi-greeter, a change to the lock or login design, colours or wallpaper runs `kusanagi greeter sync -q`
// a few seconds later. It also syncs once per start, to pick up the session you just logged in to.

#include "core/files/file_watcher.h"
#include "core/timer_manager.h"

#include <string>

namespace kusanagi {

  class GreeterSync {
  public:
    static GreeterSync& instance();

    // Watches greetd's config through the shell's file watcher; checks once now.
    void start(FileWatcher& watcher);
    // Called on a config reload: settings.json, colors.json or the wallpaper file changed.
    void onConfigReload();

  private:
    GreeterSync() = default;
    void check(bool force);

    bool m_installed = false;
    std::string m_watched;
    Timer m_later;
  };

} // namespace kusanagi
