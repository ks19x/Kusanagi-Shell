#pragma once

// greetd IPC for Kusanagi's login screen: JSON requests and responses over $GREETD_SOCK, each framed by a
// 4-byte native-endian length. greetd answers every request with exactly one response, in order, so the
// client remembers what each pending request was. Writes are small and blocking; reads are non-blocking.

#include <nlohmann/json.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace kusanagi::greeter {

  class GreetdClient {
  public:
    enum class Request : std::uint8_t { CreateSession, PostResponse, StartSession, CancelSession };

    struct Response {
      Request request = Request::CreateSession;   // what it answers
      std::string type;                           // success, error or auth_message
      std::string errorType;                      // auth_error or error
      std::string description;                    // error text
      std::string authType;                       // visible, secret, info or error
      std::string authMessage;
    };

    GreetdClient() = default;
    ~GreetdClient();
    GreetdClient(const GreetdClient&) = delete;
    GreetdClient& operator=(const GreetdClient&) = delete;

    bool connect(const std::string& path, std::string& error);
    void close();
    [[nodiscard]] bool connected() const noexcept { return m_fd >= 0; }
    [[nodiscard]] int fd() const noexcept { return m_fd; }
    [[nodiscard]] bool idle() const noexcept { return m_pending.empty(); }

    bool createSession(const std::string& user);
    bool postResponse(std::optional<std::string> response);
    bool startSession(const std::vector<std::string>& cmd, const std::vector<std::string>& env);
    bool cancelSession();

    // Read what arrived (call when fd() is readable). False when greetd went away.
    bool readAvailable(const std::function<void(const Response&)>& onResponse);

  private:
    bool send(Request kind, const nlohmann::json& message);

    int m_fd = -1;
    std::deque<Request> m_pending;
    std::vector<std::uint8_t> m_buffer;
  };

} // namespace kusanagi::greeter
