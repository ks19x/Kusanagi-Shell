#include "app/Log.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <unistd.h>

namespace ks::log {
namespace {
FILE* g_file = nullptr;
Level g_min = Level::Info;
bool g_tty = isatty(STDERR_FILENO);

const char* name(Level l) {
    switch (l) {
    case Level::Debug: return "debug";
    case Level::Info: return "info";
    case Level::Warn: return "warn";
    case Level::Error: return "error";
    }
    return "?";
}
const char* colour(Level l) {
    switch (l) {
    case Level::Debug: return "\033[2m";
    case Level::Info: return "\033[32m";
    case Level::Warn: return "\033[33m";
    case Level::Error: return "\033[31m";
    }
    return "";
}
} // namespace

void setFile(const std::string& path) {
    if (g_file) fclose(g_file);
    g_file = fopen(path.c_str(), "w");
    if (g_file) setvbuf(g_file, nullptr, _IOLBF, 0);
}

void setMinLevel(Level l) { g_min = l; }

void write(Level l, std::string_view tag, std::string_view msg) {
    if (l < g_min) return;
    auto now = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    auto stamp = std::format("{:%T}", std::chrono::zoned_time{std::chrono::current_zone(), now}.get_local_time());
    if (g_tty)
        fprintf(stderr, "%s%5s\033[0m \033[2m%s\033[0m %.*s: %.*s\n", colour(l), name(l), stamp.c_str(), int(tag.size()), tag.data(),
                int(msg.size()), msg.data());
    else
        fprintf(stderr, "%5s %s %.*s: %.*s\n", name(l), stamp.c_str(), int(tag.size()), tag.data(), int(msg.size()), msg.data());
    if (g_file)
        fprintf(g_file, "%5s %s %.*s: %.*s\n", name(l), stamp.c_str(), int(tag.size()), tag.data(), int(msg.size()), msg.data());
}

} // namespace ks::log
