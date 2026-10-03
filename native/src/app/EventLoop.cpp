#include "app/EventLoop.hpp"
#include "app/Log.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace ks {

static EventLoop* g_main = nullptr;

EventLoop& EventLoop::main() { return *g_main; }

EventLoop::EventLoop() {
    m_epoll = epoll_create1(EPOLL_CLOEXEC);
    m_timerFd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    m_eventFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    watch(m_timerFd, EPOLLIN, [this](uint32_t) {
        uint64_t n;
        while (read(m_timerFd, &n, sizeof n) > 0) {}
        runTimers();
    });
    watch(m_eventFd, EPOLLIN, [this](uint32_t) {
        uint64_t n;
        while (read(m_eventFd, &n, sizeof n) > 0) {}
        runPosts();
    });
    if (!g_main) g_main = this;
}

EventLoop::~EventLoop() {
    for (auto& [id, w] : m_wall) close(w.fd);
    close(m_eventFd);
    close(m_timerFd);
    close(m_epoll);
    if (g_main == this) g_main = nullptr;
}

void EventLoop::watch(int fd, uint32_t events, FdCallback cb) {
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (m_fds.contains(fd)) epoll_ctl(m_epoll, EPOLL_CTL_MOD, fd, &ev);
    else epoll_ctl(m_epoll, EPOLL_CTL_ADD, fd, &ev);
    m_fds[fd] = std::move(cb);
}

void EventLoop::unwatch(int fd) {
    if (m_fds.erase(fd)) epoll_ctl(m_epoll, EPOLL_CTL_DEL, fd, nullptr);
}

EventLoop::TimerId EventLoop::after(Ms delay, std::function<void()> fn) {
    TimerId id = m_nextId++;
    auto due = std::chrono::steady_clock::now() + delay;
    m_timerIndex[id] = m_timers.emplace(due, Timer{id, due, Ms{0}, std::move(fn)});
    armTimerFd();
    return id;
}

EventLoop::TimerId EventLoop::every(Ms interval, std::function<void()> fn) {
    TimerId id = m_nextId++;
    auto due = std::chrono::steady_clock::now() + interval;
    m_timerIndex[id] = m_timers.emplace(due, Timer{id, due, interval, std::move(fn)});
    armTimerFd();
    return id;
}

void EventLoop::cancel(TimerId id) {
    if (auto it = m_timerIndex.find(id); it != m_timerIndex.end()) {
        m_timers.erase(it->second);
        m_timerIndex.erase(it);
        armTimerFd();
        return;
    }
    if (auto it = m_wall.find(id); it != m_wall.end()) {
        unwatch(it->second.fd);
        close(it->second.fd);
        m_wall.erase(it);
    }
}

void EventLoop::armTimerFd() {
    itimerspec spec{};
    if (!m_timers.empty()) {
        auto due = m_timers.begin()->first;
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(due.time_since_epoch()).count();
        if (ns <= 0) ns = 1;
        spec.it_value.tv_sec = ns / 1'000'000'000;
        spec.it_value.tv_nsec = ns % 1'000'000'000;
    }
    timerfd_settime(m_timerFd, TFD_TIMER_ABSTIME, &spec, nullptr);
}

void EventLoop::runTimers() {
    auto now = std::chrono::steady_clock::now();
    std::vector<Timer> due;
    while (!m_timers.empty() && m_timers.begin()->first <= now) {
        auto node = m_timers.extract(m_timers.begin());
        m_timerIndex.erase(node.mapped().id);
        due.push_back(std::move(node.mapped()));
    }
    for (auto& t : due) {
        if (t.interval.count() > 0) {   // re-arm first so the callback may cancel it
            auto next = t.due + t.interval;
            if (next <= now) next = now + t.interval;
            Timer copy{t.id, next, t.interval, t.fn};
            m_timerIndex[t.id] = m_timers.emplace(next, std::move(copy));
        }
        t.fn();
    }
    armTimerFd();
}

EventLoop::TimerId EventLoop::onWallClock(int period, std::function<void()> fn) {
    TimerId id = m_nextId++;
    int fd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
    auto arm = [fd, period] {
        timespec now{};
        clock_gettime(CLOCK_REALTIME, &now);
        itimerspec spec{};
        spec.it_value.tv_sec = (now.tv_sec / period + 1) * period;
        spec.it_value.tv_nsec = 0;
        timerfd_settime(fd, TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET, &spec, nullptr);
    };
    arm();
    m_wall[id] = WallTimer{fd, period, std::move(fn)};
    watch(fd, EPOLLIN, [this, id, fd, arm](uint32_t) {
        uint64_t n;
        ssize_t r = read(fd, &n, sizeof n);   // ECANCELED = clock was set / resumed: re-arm and fire
        (void)r;
        arm();
        if (auto it = m_wall.find(id); it != m_wall.end()) it->second.fn();
    });
    return id;
}

void EventLoop::post(std::function<void()> fn) {
    {
        std::lock_guard lock(m_postMutex);
        m_posts.push_back(std::move(fn));
    }
    uint64_t one = 1;
    ssize_t r = ::write(m_eventFd, &one, sizeof one);
    (void)r;
}

void EventLoop::runPosts() {
    std::vector<std::function<void()>> posts;
    {
        std::lock_guard lock(m_postMutex);
        posts.swap(m_posts);
    }
    for (auto& p : posts) p();
}

int EventLoop::run() {
    m_running = true;
    epoll_event events[32];
    while (m_running) {
        for (auto& hook : m_beforeWait) hook();
        int n = epoll_wait(m_epoll, events, 32, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            log::error("loop", "epoll_wait: {}", strerror(errno));
            return 1;
        }
        for (int i = 0; i < n && m_running; i++) {
            auto it = m_fds.find(events[i].data.fd);
            if (it == m_fds.end()) continue;
            auto cb = it->second;   // copy: the callback may unwatch itself
            cb(events[i].events);
        }
    }
    return m_exitCode;
}

} // namespace ks
