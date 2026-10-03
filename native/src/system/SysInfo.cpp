#include "system/SysInfo.hpp"
#include "app/EventLoop.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace ks {

SysInfo& SysInfo::get() {
    static SysInfo s;
    return s;
}

void SysInfo::start() {
    sampleCpu();
    sampleRam();
    EventLoop::main().every(EventLoop::Ms(2000), [this] {
        sampleCpu();
        notify();
    });
    EventLoop::main().every(EventLoop::Ms(3000), [this] {
        sampleRam();
        notify();
    });
}

void SysInfo::sampleCpu() {
    FILE* f = fopen("/proc/stat", "r");
    if (!f) return;
    unsigned long long u, n, s, idle, io, irq, sirq, steal;
    if (fscanf(f, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", &u, &n, &s, &idle, &io, &irq, &sirq, &steal) == 8) {
        unsigned long long idleAll = idle + io, total = u + n + s + idle + io + irq + sirq + steal;
        if (m_lastTotal && total > m_lastTotal) cpu = int(std::lround(100.0 * (1.0 - double(idleAll - m_lastIdle) / double(total - m_lastTotal))));
        m_lastIdle = idleAll;
        m_lastTotal = total;
    }
    fclose(f);
}

void SysInfo::sampleRam() {
    FILE* f = fopen("/proc/meminfo", "r");
    if (!f) return;
    char key[64];
    unsigned long long v;
    unsigned long long total = 0, avail = 0;
    while (fscanf(f, "%63s %llu kB\n", key, &v) == 2) {
        if (!strcmp(key, "MemTotal:")) total = v;
        else if (!strcmp(key, "MemAvailable:")) avail = v;
    }
    fclose(f);
    if (total) {
        ram = int(std::lround(100.0 * double(total - avail) / double(total)));
        ramTotal = double(total) / 1048576.0;
        ramUsed = double(total - avail) / 1048576.0;
    }
}

} // namespace ks
