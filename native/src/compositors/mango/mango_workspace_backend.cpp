#include "compositors/mango/mango_workspace_backend.h"

#include "compositors/mango/mango_runtime.h"
#include "core/log.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

  constexpr Logger kLog("workspace_mango");

  [[nodiscard]] std::string jsonString(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    return it != json.end() && it->is_string() ? it->get<std::string>() : std::string{};
  }

  [[nodiscard]] std::int32_t jsonInt(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end()) {
      return 0;
    }
    if (it->is_number_integer()) {
      return it->get<std::int32_t>();
    }
    if (it->is_number_unsigned()) {
      return static_cast<std::int32_t>(it->get<std::uint32_t>());
    }
    return 0;
  }

  [[nodiscard]] bool jsonBool(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    return it != json.end() && it->is_boolean() && it->get<bool>();
  }

  [[nodiscard]] std::string jsonIdString(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || it->is_null()) {
      return {};
    }
    if (it->is_string()) {
      return it->get<std::string>();
    }
    if (it->is_number_unsigned()) {
      return std::to_string(it->get<std::uint64_t>());
    }
    if (it->is_number_integer()) {
      return std::to_string(it->get<std::int64_t>());
    }
    return {};
  }

  [[nodiscard]] std::vector<std::uint32_t> jsonTagArray(const nlohmann::json& json, const char* key) {
    std::vector<std::uint32_t> result;
    const auto it = json.find(key);
    if (it == json.end() || !it->is_array()) {
      return result;
    }
    result.reserve(it->size());
    for (const auto& item : *it) {
      if (item.is_number_unsigned()) {
        result.push_back(item.get<std::uint32_t>());
      } else if (item.is_number_integer()) {
        const auto value = item.get<std::int32_t>();
        if (value > 0) {
          result.push_back(static_cast<std::uint32_t>(value));
        }
      }
    }
    return result;
  }

  [[nodiscard]] bool sendAll(int fd, std::string_view payload) {
    std::size_t offset = 0;
    while (offset < payload.size()) {
      const ssize_t written = ::send(fd, payload.data() + offset, payload.size() - offset, MSG_NOSIGNAL);
      if (written <= 0) {
        if (written < 0 && errno == EINTR) {
          continue;
        }
        return false;
      }
      offset += static_cast<std::size_t>(written);
    }
    return true;
  }

} // namespace

MangoWorkspaceBackend::MangoWorkspaceBackend(compositors::mango::MangoRuntime& runtime) : m_runtime(runtime) {}

bool MangoWorkspaceBackend::isAvailable() const noexcept { return m_watchFd >= 0; }

void MangoWorkspaceBackend::setChangeCallback(ChangeCallback callback) { m_changeCallback = std::move(callback); }

void MangoWorkspaceBackend::setOutputNameResolver(WorkspaceOutputNameResolver::Resolver resolver) {
  m_outputNameResolver = std::move(resolver);
}

bool MangoWorkspaceBackend::connectSocket() {
  if (m_watchFd >= 0) {
    return true;
  }
  return openWatchSocket();
}

void MangoWorkspaceBackend::activate(const std::string& id) {
  if (const auto* active = activeOutputState(); active != nullptr && !active->name.empty()) {
    dispatchAsync("viewcrossmon," + id + "," + active->name);
    return;
  }
  dispatchAsync("view," + id);
}

void MangoWorkspaceBackend::activateForOutput(wl_output* output, const std::string& id) {
  const std::string name = outputName(output);
  if (!name.empty()) {
    dispatchAsync("viewcrossmon," + id + "," + name);
    return;
  }
  activate(id);
}

void MangoWorkspaceBackend::activateForOutput(wl_output* output, const Workspace& workspace) {
  const auto index = parseTagIndex(workspace);
  if (!index.has_value()) {
    return;
  }
  activateForOutput(output, std::to_string(*index + 1));
}

std::vector<Workspace> MangoWorkspaceBackend::all() const {
  const auto* state = activeOutputState();
  return state != nullptr ? forOutput(nullptr) : std::vector<Workspace>{};
}

std::vector<Workspace> MangoWorkspaceBackend::forOutput(wl_output* output) const {
  const auto* state = output != nullptr ? outputStateFor(output) : activeOutputState();
  if (state == nullptr) {
    return {};
  }

  std::vector<Workspace> result;
  result.reserve(state->tags.size());
  for (const auto& tag : state->tags) {
    result.push_back(makeWorkspace(tag));
  }
  return result;
}

std::unordered_map<std::string, std::vector<std::string>>
MangoWorkspaceBackend::appIdsByWorkspace(wl_output* output) const {
  std::unordered_map<std::string, std::vector<std::string>> result;
  const auto* state = output != nullptr ? outputStateFor(output) : nullptr;
  const std::string outputFilter = state != nullptr ? state->name : std::string{};
  for (const auto& client : m_clients) {
    if (!outputFilter.empty() && client.monitorName != outputFilter) {
      continue;
    }
    if (client.appId.empty()) {
      continue;
    }
    for (const std::uint32_t tag : client.tags) {
      if (tag == 0) {
        continue;
      }
      auto& apps = result[std::to_string(tag)];
      if (!std::ranges::contains(apps, client.appId)) {
        apps.push_back(client.appId);
      }
    }
  }
  return result;
}

std::vector<WorkspaceWindow> MangoWorkspaceBackend::workspaceWindows(wl_output* output) const {
  std::vector<WorkspaceWindow> result;
  const auto* state = output != nullptr ? outputStateFor(output) : nullptr;
  const std::string outputFilter = state != nullptr ? state->name : std::string{};
  for (const auto& client : m_clients) {
    if (!outputFilter.empty() && client.monitorName != outputFilter) {
      continue;
    }
    for (const std::uint32_t tag : client.tags) {
      if (tag == 0) {
        continue;
      }
      result.push_back(
          WorkspaceWindow{
              .windowId = client.id,
              .workspaceKey = std::to_string(tag),
              .appId = client.appId,
              .title = client.title,
              .x = client.x,
              .y = client.y,
              .outputName = {},
          }
      );
    }
  }
  return result;
}

void MangoWorkspaceBackend::focusWindow(const std::string& windowId) {
  if (!windowId.empty()) {
    dispatchAsync("focusid client," + windowId);
  }
}

void MangoWorkspaceBackend::cleanup() {
  closeWatchSocket();
  if (m_epollFd >= 0) {
    ::close(m_epollFd);
    m_epollFd = -1;
  }
  m_knownOutputs.clear();
  m_outputsByName.clear();
  m_rawOutputsByName.clear();
  m_clients.clear();
}

void MangoWorkspaceBackend::onOutputAdded(wl_output* output) {
  if (output == nullptr || std::ranges::contains(m_knownOutputs, output)) {
    return;
  }
  m_knownOutputs.push_back(output);
}

void MangoWorkspaceBackend::onOutputRemoved(wl_output* output) { std::erase(m_knownOutputs, output); }

int MangoWorkspaceBackend::pollFd() const noexcept { return m_watchFd >= 0 ? m_epollFd : -1; }

int MangoWorkspaceBackend::pollTimeoutMs() const noexcept { return m_watchFd >= 0 ? -1 : 2000; }

void MangoWorkspaceBackend::dispatchPoll(short revents) {
  if (m_watchFd < 0) {
    (void)connectSocket();
    return;
  }
  if ((revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
    closeWatchSocket();
    notifyChanged();
    return;
  }
  if ((revents & POLLIN) == 0) {
    return;
  }

  epoll_event events[16];
  const int count = ::epoll_wait(m_epollFd, events, 16, 0);
  bool changed = false;
  bool lost = false;
  for (int i = 0; i < count && !lost; ++i) {
    const int fd = events[i].data.fd;
    const bool hangup = (events[i].events & (EPOLLHUP | EPOLLERR)) != 0;
    if (fd == m_watchFd || fd == m_clientsFd) {
      bool closed = false;
      const bool monitors = fd == m_watchFd;
      const auto line = readLatestLine(fd, monitors ? m_readBuffer : m_clientsBuffer, closed);
      if (line.has_value()) {
        changed = (monitors ? handleMonitors(*line) : handleClients(*line)) || changed;
      }
      if (closed) {
        lost = true;
      }
    } else {
      drainPending(fd, hangup);
    }
  }
  if (lost) {
    closeWatchSocket();
    changed = true;
  }
  if (changed) {
    notifyChanged();
  }
}

wl_output* MangoWorkspaceBackend::ipcSelectedOutput() const {
  for (const auto& [name, state] : m_outputsByName) {
    if (!state.active) {
      continue;
    }
    for (wl_output* output : m_knownOutputs) {
      if (outputName(output) == name) {
        return output;
      }
    }
  }
  return nullptr;
}

std::optional<std::pair<std::string, std::string>>
MangoWorkspaceBackend::ipcFocusedClientForOutput(wl_output* output) const {
  const auto* state = outputStateFor(output);
  if (state == nullptr) {
    state = activeOutputState();
  }
  if (state == nullptr) {
    return std::nullopt;
  }
  return std::pair<std::string, std::string>{state->activeClientTitle, state->activeClientAppId};
}

namespace {

  // Connects to mango's IPC socket and sends one request line. Connect and send block, which is fine for
  // a tiny local request; the fd is switched to non-blocking for the replies.
  int connectAndSend(const std::string& socketPath, std::string_view payload) {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
      return -1;
    }
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (socketPath.size() >= sizeof(addr.sun_path)) {
      ::close(fd);
      return -1;
    }
    std::memcpy(addr.sun_path, socketPath.c_str(), socketPath.size() + 1);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0 || !sendAll(fd, payload)) {
      ::close(fd);
      return -1;
    }
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
      (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    return fd;
  }

  bool epollAdd(int epollFd, int fd) {
    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLRDHUP;
    ev.data.fd = fd;
    return ::epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) == 0;
  }

  constexpr std::size_t kMaxPendingDispatches = 32;

} // namespace

bool MangoWorkspaceBackend::openWatchSocket() {
  const auto& socketPath = m_runtime.socketPath();
  if (socketPath.empty()) {
    return false;
  }
  if (m_epollFd < 0) {
    m_epollFd = ::epoll_create1(EPOLL_CLOEXEC);
    if (m_epollFd < 0) {
      return false;
    }
  }

  // Both streams send a full snapshot right away, then one per change.
  const int monitorsFd = connectAndSend(socketPath, "watch all-monitors\n");
  const int clientsFd = monitorsFd >= 0 ? connectAndSend(socketPath, "watch all-clients\n") : -1;
  if (clientsFd < 0 || !epollAdd(m_epollFd, monitorsFd) || !epollAdd(m_epollFd, clientsFd)) {
    if (monitorsFd >= 0) {
      ::close(monitorsFd);
    }
    if (clientsFd >= 0) {
      ::close(clientsFd);
    }
    return false;
  }

  m_watchFd = monitorsFd;
  m_clientsFd = clientsFd;
  kLog.info("connected Mango IPC workspace socket={}", socketPath);
  return true;
}

void MangoWorkspaceBackend::closeWatchSocket() {
  for (int* fd : {&m_watchFd, &m_clientsFd}) {
    if (*fd >= 0) {
      ::close(*fd); // Closing also removes it from the epoll set.
      *fd = -1;
    }
  }
  for (const int fd : m_pendingFds) {
    ::close(fd);
  }
  m_pendingFds.clear();
  m_readBuffer.clear();
  m_clientsBuffer.clear();
}

std::optional<std::string> MangoWorkspaceBackend::readLatestLine(int fd, std::string& buffer, bool& closed) {
  char chunk[8192];
  while (true) {
    const ssize_t count = ::recv(fd, chunk, sizeof(chunk), MSG_DONTWAIT);
    if (count > 0) {
      buffer.append(chunk, static_cast<std::size_t>(count));
      continue;
    }
    if (count == 0) {
      closed = true;
    } else if (errno == EINTR) {
      continue;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
      closed = true;
    }
    break;
  }
  const std::size_t last = buffer.rfind('\n');
  if (last == std::string::npos) {
    return std::nullopt;
  }
  const std::size_t prev = last == 0 ? std::string::npos : buffer.rfind('\n', last - 1);
  const std::size_t start = prev == std::string::npos ? 0 : prev + 1;
  std::string line = buffer.substr(start, last - start);
  buffer.erase(0, last + 1);
  return line;
}

void MangoWorkspaceBackend::dispatchAsync(const std::string& command) {
  if (m_watchFd < 0 || m_epollFd < 0) {
    (void)m_runtime.dispatch(command);
    return;
  }
  const int fd = connectAndSend(m_runtime.socketPath(), "dispatch " + command + "\n");
  if (fd < 0) {
    (void)m_runtime.dispatch(command);
    return;
  }
  if (!epollAdd(m_epollFd, fd)) {
    ::close(fd);
    return;
  }
  if (m_pendingFds.size() >= kMaxPendingDispatches) {
    ::close(m_pendingFds.front());
    m_pendingFds.erase(m_pendingFds.begin());
  }
  m_pendingFds.push_back(fd);
}

void MangoWorkspaceBackend::drainPending(int fd, bool hangup) {
  char chunk[1024];
  bool done = hangup;
  while (!done) {
    const ssize_t count = ::recv(fd, chunk, sizeof(chunk), MSG_DONTWAIT);
    if (count > 0) {
      // One reply line per dispatch. Once it arrives the connection is done.
      done = std::memchr(chunk, '\n', static_cast<std::size_t>(count)) != nullptr;
      continue;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    done = count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK);
    break;
  }
  if (done) {
    ::close(fd);
    std::erase(m_pendingFds, fd);
  }
}

bool MangoWorkspaceBackend::handleMonitors(std::string_view line) {
  if (line.empty()) {
    return false;
  }

  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(line);
  } catch (const nlohmann::json::exception&) {
    return false;
  }

  const auto monitorsIt = parsed.find("monitors");
  if (monitorsIt == parsed.end() || !monitorsIt->is_array()) {
    return false;
  }

  std::unordered_map<std::string, OutputState> nextOutputs;
  for (const auto& monitorJson : *monitorsIt) {
    auto monitor = parseMonitor(monitorJson);
    if (monitor.has_value() && !monitor->name.empty()) {
      nextOutputs.emplace(monitor->name, std::move(*monitor));
    }
  }

  m_rawOutputsByName = std::move(nextOutputs);
  m_outputsByName = m_rawOutputsByName;
  syncFocusedClientTags();
  return true;
}

bool MangoWorkspaceBackend::handleClients(std::string_view line) {
  if (line.empty()) {
    return false;
  }

  nlohmann::json parsed;
  try {
    parsed = nlohmann::json::parse(line);
  } catch (const nlohmann::json::exception&) {
    return false;
  }

  const auto clientsIt = parsed.find("clients");
  if (clientsIt == parsed.end() || !clientsIt->is_array()) {
    return false;
  }

  std::vector<ClientState> nextClients;
  nextClients.reserve(clientsIt->size());
  for (const auto& clientJson : *clientsIt) {
    auto client = parseClient(clientJson);
    if (client.has_value()) {
      nextClients.push_back(std::move(*client));
    }
  }
  m_clients = std::move(nextClients);
  // Focused-client tags and the overview fix-up depend on both snapshots, so redo them from the raw one.
  m_outputsByName = m_rawOutputsByName;
  syncFocusedClientTags();
  return true;
}

void MangoWorkspaceBackend::syncFocusedClientTags() {
  for (auto& [monitorName, state] : m_outputsByName) {
    for (auto& tag : state.tags) {
      tag.hasFocusedClient = false;
    }

    const ClientState* focusedClient = nullptr;
    if (!state.activeClientId.empty()) {
      for (const auto& client : m_clients) {
        if (client.id == state.activeClientId && client.monitorName == monitorName) {
          focusedClient = &client;
          break;
        }
      }
    }
    if (focusedClient == nullptr) {
      for (const auto& client : m_clients) {
        if (client.focused && client.monitorName == monitorName) {
          focusedClient = &client;
          break;
        }
      }
    }
    if (focusedClient == nullptr) {
      continue;
    }

    for (const std::uint32_t tagIndex : focusedClient->tags) {
      if (tagIndex == 0) {
        continue;
      }
      for (auto& tag : state.tags) {
        if (tag.index == tagIndex) {
          tag.hasFocusedClient = true;
        }
      }
    }

    // In Mango, the overview mode is equivalent to viewing all tags. If all tags are active, this messes with tag merge
    // view, so we only show active client when all tags are "active".
    bool allActive = !state.tags.empty();
    for (const auto& tag : state.tags) {
      if (!tag.active) {
        allActive = false;
        break;
      }
    }

    if (allActive) {
      bool foundFocused = false;
      for (auto& tag : state.tags) {
        if (tag.hasFocusedClient) {
          tag.active = true;
          foundFocused = true;
        } else {
          tag.active = false;
        }
      }
      if (!foundFocused && !state.tags.empty()) {
        state.tags.front().active = true;
      }
    }
  }
}

void MangoWorkspaceBackend::notifyChanged() {
  if (m_changeCallback) {
    m_changeCallback();
  }
}

std::string MangoWorkspaceBackend::outputName(wl_output* output) const {
  return m_outputNameResolver && output != nullptr ? m_outputNameResolver(output) : std::string{};
}

MangoWorkspaceBackend::OutputState* MangoWorkspaceBackend::activeOutputState() {
  for (auto& [_, state] : m_outputsByName) {
    if (state.active) {
      return &state;
    }
  }
  return !m_outputsByName.empty() ? &m_outputsByName.begin()->second : nullptr;
}

const MangoWorkspaceBackend::OutputState* MangoWorkspaceBackend::activeOutputState() const {
  for (const auto& [_, state] : m_outputsByName) {
    if (state.active) {
      return &state;
    }
  }
  return !m_outputsByName.empty() ? &m_outputsByName.begin()->second : nullptr;
}

const MangoWorkspaceBackend::OutputState* MangoWorkspaceBackend::outputStateFor(wl_output* output) const {
  const std::string name = outputName(output);
  if (name.empty()) {
    return nullptr;
  }
  const auto it = m_outputsByName.find(name);
  return it != m_outputsByName.end() ? &it->second : nullptr;
}

std::optional<std::size_t> MangoWorkspaceBackend::parseTagIndex(const Workspace& workspace) {
  if (!workspace.coordinates.empty()) {
    return static_cast<std::size_t>(workspace.coordinates[0]);
  }
  return parseTagIndex(workspace.id.empty() ? workspace.name : workspace.id);
}

std::optional<std::size_t> MangoWorkspaceBackend::parseTagIndex(const std::string& id) {
  if (id.empty()) {
    return std::nullopt;
  }

  std::size_t value = 0;
  const char* start = id.data();
  const char* end = id.data() + id.size();
  const auto [ptr, ec] = std::from_chars(start, end, value);
  if (ec != std::errc{} || ptr != end || value == 0) {
    return std::nullopt;
  }
  return value - 1;
}

Workspace MangoWorkspaceBackend::makeWorkspace(const TagInfo& tag) {
  return Workspace{
      .id = std::to_string(tag.index),
      .name = std::to_string(tag.index),
      .coordinates = {tag.index > 0 ? tag.index - 1 : 0},
      .index = tag.index,
      .active = tag.active,
      .urgent = tag.urgent,
      .occupied = tag.occupied,
  };
}

std::optional<MangoWorkspaceBackend::OutputState> MangoWorkspaceBackend::parseMonitor(const nlohmann::json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }

  OutputState state{};
  state.name = jsonString(json, "name");
  state.active = jsonBool(json, "active");
  state.x = jsonInt(json, "x");
  state.y = jsonInt(json, "y");
  state.width = jsonInt(json, "width");
  state.height = jsonInt(json, "height");

  const auto activeClientIt = json.find("active_client");
  if (activeClientIt != json.end() && activeClientIt->is_object()) {
    state.activeClientId = jsonIdString(*activeClientIt, "id");
    state.activeClientTitle = StringUtils::windowTitleSingleLine(jsonString(*activeClientIt, "title"));
    state.activeClientAppId = jsonString(*activeClientIt, "appid");
  }

  const auto tagsIt = json.find("tags");
  if (tagsIt != json.end() && tagsIt->is_array()) {
    state.tags.reserve(tagsIt->size());
    for (const auto& tagJson : *tagsIt) {
      if (!tagJson.is_object()) {
        continue;
      }
      TagInfo tag{};
      tag.index = static_cast<std::uint32_t>(jsonInt(tagJson, "index"));
      tag.active = jsonBool(tagJson, "is_active");
      tag.urgent = jsonBool(tagJson, "is_urgent");
      tag.occupied = jsonInt(tagJson, "client_count") > 0;
      state.tags.push_back(tag);
    }
  }

  const auto activeTags = jsonTagArray(json, "active_tags");
  if (!activeTags.empty() && !(activeTags.size() == 1 && activeTags.front() == 0)) {
    for (auto& tag : state.tags) {
      tag.active = std::ranges::contains(activeTags, tag.index);
    }
  }

  for (auto& tag : state.tags) {
    tag.hasFocusedClient = false;
  }

  return state;
}

std::optional<MangoWorkspaceBackend::ClientState> MangoWorkspaceBackend::parseClient(const nlohmann::json& json) {
  if (!json.is_object()) {
    return std::nullopt;
  }

  ClientState client{};
  const auto idIt = json.find("id");
  if (idIt != json.end()) {
    if (idIt->is_number_unsigned()) {
      client.id = std::to_string(idIt->get<std::uint64_t>());
    } else if (idIt->is_number_integer()) {
      client.id = std::to_string(idIt->get<std::int64_t>());
    }
  }
  client.title = StringUtils::windowTitleSingleLine(jsonString(json, "title"));
  client.appId = jsonString(json, "appid");
  client.monitorName = jsonString(json, "monitor");
  client.tags = jsonTagArray(json, "tags");
  client.focused = jsonBool(json, "is_focused");
  client.x = jsonInt(json, "x");
  client.y = jsonInt(json, "y");
  return client;
}
