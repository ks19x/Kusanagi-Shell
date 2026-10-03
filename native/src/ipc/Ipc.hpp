// Ipc.hpp — Unix socket at $XDG_RUNTIME_DIR/kusanagi/<WAYLAND_DISPLAY>.sock.
// Wire: one request line = JSON array of strings (argv), one reply line = {"ok":…,"result"|"error"}.
// Targets/functions keep v4's names: ["launcher","toggle"], ["settings","page","sound"], …
#pragma once
#include "util/Json.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ks::ipc {

using Args = std::vector<std::string>;
using Handler = std::function<json::Value(const Args& args)>;   // throw std::runtime_error for errors

class Server {
public:
    static Server& get();
    bool listen();
    void stop();
    // fn "" = catch-all for the target (args[0] is then the function)
    void on(const std::string& target, const std::string& fn, Handler h, const std::string& help = "");
    json::Value call(const Args& argv);
    std::string show() const;

private:
    void accept();
    int m_fd = -1;
    int m_lock = -1;
    struct Entry { Handler h; std::string help; };
    std::map<std::string, std::map<std::string, Entry>> m_targets;
};

// client: returns exit status; prints the result
int request(const Args& argv, bool quiet = false);
bool ping();

} // namespace ks::ipc
