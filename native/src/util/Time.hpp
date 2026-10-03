// Time.hpp — Qt.formatDateTime-compatible formatting, so users' bar.clock / lock.clock strings
// (HH:mm, h:mm AP, ddd d MMM, …) mean the same thing as in v4. Quoted 'text' is literal.
#pragma once
#include <ctime>
#include <string>

namespace ks {

std::string formatQt(const std::string& fmt, time_t when = 0);   // 0 = now
bool formatHasSeconds(const std::string& fmt);

} // namespace ks
