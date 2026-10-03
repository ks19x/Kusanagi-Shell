// Log.hpp — tiny leveled logger: stderr, plus $XDG_STATE_HOME/kusanagi/log when running as the shell.
#pragma once
#include <format>
#include <string_view>

namespace ks::log {

enum class Level { Debug, Info, Warn, Error };

void setFile(const std::string& path);
void setMinLevel(Level l);
void write(Level l, std::string_view tag, std::string_view msg);

template <class... A> void debug(std::string_view tag, std::format_string<A...> f, A&&... a) { write(Level::Debug, tag, std::format(f, std::forward<A>(a)...)); }
template <class... A> void info(std::string_view tag, std::format_string<A...> f, A&&... a) { write(Level::Info, tag, std::format(f, std::forward<A>(a)...)); }
template <class... A> void warn(std::string_view tag, std::format_string<A...> f, A&&... a) { write(Level::Warn, tag, std::format(f, std::forward<A>(a)...)); }
template <class... A> void error(std::string_view tag, std::format_string<A...> f, A&&... a) { write(Level::Error, tag, std::format(f, std::forward<A>(a)...)); }

} // namespace ks::log
