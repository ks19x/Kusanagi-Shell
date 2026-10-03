#include "util/Time.hpp"

#include <format>
#include <langinfo.h>

namespace ks {

bool formatHasSeconds(const std::string& f) { return f.find('s') != std::string::npos; }

std::string formatQt(const std::string& f, time_t when) {
    if (!when) when = time(nullptr);
    tm t{};
    localtime_r(&when, &t);
    std::string out;
    bool ampm = f.find("AP") != std::string::npos || f.find("ap") != std::string::npos;
    for (size_t i = 0; i < f.size();) {
        char c = f[i];
        if (c == '\'') {   // literal
            size_t end = f.find('\'', i + 1);
            if (end == std::string::npos) end = f.size();
            out += end == i + 1 ? "'" : f.substr(i + 1, end - i - 1);
            i = end + 1;
            continue;
        }
        size_t n = 1;
        while (i + n < f.size() && f[i + n] == c) n++;
        auto two = [](int v) { return std::format("{:02}", v); };
        switch (c) {
        case 'd':
            if (n == 1) out += std::to_string(t.tm_mday);
            else if (n == 2) out += two(t.tm_mday);
            else if (n == 3) out += nl_langinfo(nl_item(ABDAY_1 + t.tm_wday));
            else out += nl_langinfo(nl_item(DAY_1 + t.tm_wday));
            break;
        case 'M':
            if (n == 1) out += std::to_string(t.tm_mon + 1);
            else if (n == 2) out += two(t.tm_mon + 1);
            else if (n == 3) out += nl_langinfo(nl_item(ABMON_1 + t.tm_mon));
            else out += nl_langinfo(nl_item(MON_1 + t.tm_mon));
            break;
        case 'y':
            out += n >= 4 ? std::to_string(t.tm_year + 1900) : two((t.tm_year + 1900) % 100);
            break;
        case 'h': {
            int hr = ampm ? (t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12) : t.tm_hour;
            out += n >= 2 ? two(hr) : std::to_string(hr);
            break;
        }
        case 'H': out += n >= 2 ? two(t.tm_hour) : std::to_string(t.tm_hour); break;
        case 'm': out += n >= 2 ? two(t.tm_min) : std::to_string(t.tm_min); break;
        case 's': out += n >= 2 ? two(t.tm_sec) : std::to_string(t.tm_sec); break;
        case 'A':
        case 'a':
            if (i + 1 < f.size() && (f[i + 1] == 'P' || f[i + 1] == 'p')) {
                out += c == 'A' ? (t.tm_hour < 12 ? "AM" : "PM") : (t.tm_hour < 12 ? "am" : "pm");
                n = 2;
            } else out += std::string(n, c);
            break;
        default: out += std::string(n, c);
        }
        i += n;
    }
    return out;
}

} // namespace ks
