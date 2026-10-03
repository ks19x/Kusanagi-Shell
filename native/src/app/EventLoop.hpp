// EventLoop.hpp — the one epoll loop everything runs on: fds, monotonic timers, wall-clock alarms
// (survive suspend / clock changes), cross-thread posts, and hooks that run before each wait
// (Wayland flushes its queue there).
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace ks {

class EventLoop {
public:
    using Ms = std::chrono::milliseconds;
    using TimerId = uint64_t;
    using FdCallback = std::function<void(uint32_t events)>;

    EventLoop();
    ~EventLoop();
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    static EventLoop& main();

    void watch(int fd, uint32_t events, FdCallback cb);   // EPOLLIN etc.
    void unwatch(int fd);

    TimerId after(Ms delay, std::function<void()> fn);
    TimerId every(Ms interval, std::function<void()> fn);
    void cancel(TimerId id);
    bool pending(TimerId id) const { return id && m_timerIndex.contains(id); }

    // fires at the next wall-clock multiple of `period` seconds (60 = on the minute), every time;
    // re-armed after suspend or when the clock is set
    TimerId onWallClock(int periodSeconds, std::function<void()> fn);

    void post(std::function<void()> fn);                  // thread-safe
    void beforeWait(std::function<void()> fn) { m_beforeWait.push_back(std::move(fn)); }

    int run();
    void quit(int code = 0) { m_running = false; m_exitCode = code; }

private:
    struct Timer {
        TimerId id;
        std::chrono::steady_clock::time_point due;
        Ms interval;
        std::function<void()> fn;
    };
    void armTimerFd();
    void runTimers();
    void runPosts();

    int m_epoll = -1;
    int m_timerFd = -1;
    int m_eventFd = -1;
    bool m_running = false;
    int m_exitCode = 0;
    TimerId m_nextId = 1;
    std::unordered_map<int, FdCallback> m_fds;
    std::multimap<std::chrono::steady_clock::time_point, Timer> m_timers;
    std::unordered_map<TimerId, std::multimap<std::chrono::steady_clock::time_point, Timer>::iterator> m_timerIndex;
    std::vector<std::function<void()>> m_beforeWait;
    std::mutex m_postMutex;
    std::vector<std::function<void()>> m_posts;
    struct WallTimer { int fd; int period; std::function<void()> fn; };
    std::unordered_map<TimerId, WallTimer> m_wall;
};

} // namespace ks
