// SysInfo.hpp — /proc + /sys stats (v4 SysInfo.qml): sampled only while something shows them.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace ks {

struct SysInfo {
    int cpu = 0;                  // %
    int ram = 0;                  // %
    double ramUsed = 0, ramTotal = 0;   // GiB

    static SysInfo& get();
    void start();                 // bar timers (cpu 2 s, ram 3 s)
    void sampleCpu();
    void sampleRam();
    void onChanged(std::function<void()> fn) { m_listeners.push_back(std::move(fn)); }
    void notify() { for (auto& l : m_listeners) l(); }

private:
    unsigned long long m_lastIdle = 0, m_lastTotal = 0;
    std::vector<std::function<void()>> m_listeners;
};

inline SysInfo& sysinfo() { return SysInfo::get(); }

} // namespace ks
