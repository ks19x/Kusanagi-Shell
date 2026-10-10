#pragma once

// `kusanagi-shell --greeter [--preview]`: Kusanagi's login screen as its own lean process. It shows the lock
// screen design with a user and session picker plus reboot and power off, talks to greetd ($GREETD_SOCK) and
// starts none of the shell's services. Without $GREETD_SOCK (or with --preview) it runs as a window and logs
// nobody in. It exits non-zero when it can't run, so the kusanagi-greeter launcher can fall back.

namespace kusanagi::greeter {

  [[nodiscard]] int run(int argc, char* argv[]);

} // namespace kusanagi::greeter
