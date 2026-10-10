#include "shell/greeter/greetd_client.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace kusanagi::greeter {

  namespace {

    // a response bigger than this is not greetd talking
    constexpr std::uint32_t kMaxMessage = 1U << 20;

    bool writeAll(int fd, const std::uint8_t* data, std::size_t size) {
      while (size > 0) {
        const ssize_t n = ::send(fd, data, size, MSG_NOSIGNAL);
        if (n < 0) {
          if (errno == EINTR) {
            continue;
          }
          if (errno == EAGAIN) {
            pollfd p{.fd = fd, .events = POLLOUT, .revents = 0};
            (void)::poll(&p, 1, 1000);
            continue;
          }
          return false;
        }
        data += n;
        size -= static_cast<std::size_t>(n);
      }
      return true;
    }

    std::string str(const nlohmann::json& j, const char* key) {
      const auto it = j.find(key);
      return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
    }

  } // namespace

  GreetdClient::~GreetdClient() { close(); }

  bool GreetdClient::connect(const std::string& path, std::string& error) {
    close();
    sockaddr_un addr{};
    if (path.empty() || path.size() >= sizeof(addr.sun_path)) {
      error = "bad socket path";
      return false;
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
      error = std::strerror(errno);
      return false;
    }
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size());
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
      error = std::strerror(errno);
      ::close(fd);
      return false;
    }
    (void)::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    m_fd = fd;
    return true;
  }

  void GreetdClient::close() {
    if (m_fd >= 0) {
      ::close(m_fd);
      m_fd = -1;
    }
    m_pending.clear();
    m_buffer.clear();
  }

  bool GreetdClient::send(Request kind, const nlohmann::json& message) {
    if (m_fd < 0) {
      return false;
    }
    const std::string body = message.dump();
    const auto length = static_cast<std::uint32_t>(body.size());
    std::vector<std::uint8_t> frame(sizeof(length) + body.size());
    std::memcpy(frame.data(), &length, sizeof(length)); // native endian, like greetd
    std::memcpy(frame.data() + sizeof(length), body.data(), body.size());
    // The password passes through here, so wipe the copy.
    const bool ok = writeAll(m_fd, frame.data(), frame.size());
    ::explicit_bzero(frame.data(), frame.size());
    if (ok) {
      m_pending.push_back(kind);
    }
    return ok;
  }

  bool GreetdClient::createSession(const std::string& user) {
    return send(Request::CreateSession, {{"type", "create_session"}, {"username", user}});
  }

  bool GreetdClient::postResponse(std::optional<std::string> response) {
    nlohmann::json message = {{"type", "post_auth_message_response"}};
    message["response"] = response.has_value() ? nlohmann::json(*response) : nlohmann::json(nullptr);
    const bool ok = send(Request::PostResponse, message);
    if (response.has_value()) {
      ::explicit_bzero(response->data(), response->size());
    }
    return ok;
  }

  bool GreetdClient::startSession(const std::vector<std::string>& cmd, const std::vector<std::string>& env) {
    return send(Request::StartSession, {{"type", "start_session"}, {"cmd", cmd}, {"env", env}});
  }

  bool GreetdClient::cancelSession() { return send(Request::CancelSession, {{"type", "cancel_session"}}); }

  bool GreetdClient::readAvailable(const std::function<void(const Response&)>& onResponse) {
    if (m_fd < 0) {
      return false;
    }
    for (;;) {
      std::uint8_t chunk[4096];
      const ssize_t n = ::recv(m_fd, chunk, sizeof(chunk), 0);
      if (n > 0) {
        m_buffer.insert(m_buffer.end(), chunk, chunk + n);
        continue;
      }
      if (n == 0) {
        close();
        return false;
      }
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      close();
      return false;
    }

    while (m_buffer.size() >= sizeof(std::uint32_t)) {
      std::uint32_t length = 0;
      std::memcpy(&length, m_buffer.data(), sizeof(length));
      if (length > kMaxMessage) {
        close();
        return false;
      }
      if (m_buffer.size() < sizeof(length) + length) {
        break;
      }
      const std::string body(m_buffer.begin() + sizeof(length), m_buffer.begin() + sizeof(length) + length);
      m_buffer.erase(m_buffer.begin(), m_buffer.begin() + sizeof(length) + length);

      Response response;
      if (!m_pending.empty()) {
        response.request = m_pending.front();
        m_pending.pop_front();
      }
      const auto j = nlohmann::json::parse(body, nullptr, false);
      if (!j.is_object()) {
        response.type = "error";
        response.errorType = "error";
        response.description = "greetd sent something unreadable";
      } else {
        response.type = str(j, "type");
        response.errorType = str(j, "error_type");
        response.description = str(j, "description");
        response.authType = str(j, "auth_message_type");
        response.authMessage = str(j, "auth_message");
      }
      onResponse(response);
      if (m_fd < 0) {
        return false; // the handler closed us
      }
    }
    return true;
  }

} // namespace kusanagi::greeter
