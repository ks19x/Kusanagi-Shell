#pragma once

namespace kusanagi::ipc {

  // Entry point for `kusanagi-shell msg <command> [args...]`. Returns a process exit
  // code. Forwards the command to the running instance over the IPC socket.
  int runCli(int argc, char* argv[]);

} // namespace kusanagi::ipc
