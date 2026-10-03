// Process.hpp — child processes on the event loop.
//   spawnDetached(argv)        fire and forget (own session, fds closed) — like Quickshell.execDetached
//   Process                    long-lived child with line-split stdout + exit callback (niri event-stream)
//   run(argv, onDone)          collect stdout, call back on exit
#pragma once
#include <functional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace ks {

void spawnDetached(const std::vector<std::string>& argv);
void spawnShell(const std::string& command);                 // sh -c, detached

class Process {
public:
    std::function<void(std::string_view line)> onLine;
    std::function<void(int status)> onExit;

    ~Process();
    bool start(const std::vector<std::string>& argv);
    void stop();
    bool running() const { return m_pid > 0; }

private:
    void readable();
    void reap();
    pid_t m_pid = -1;
    int m_out = -1;
    int m_pidfd = -1;
    std::string m_buf;
};

// run to completion, stdout collected (stderr dropped)
void run(const std::vector<std::string>& argv, std::function<void(int status, std::string out)> done);

} // namespace ks
