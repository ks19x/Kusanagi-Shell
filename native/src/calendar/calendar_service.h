#pragma once

#include "calendar/caldav_client.h"
#include "calendar/calendar_credential_store.h"
#include "calendar/calendar_types.h"
#include "config/config_types.h"
#include "core/inotify/inotify.h"
#include "security/storage_key_provider.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <poll.h>
#include <set>
#include <span>
#include <string>
#include <vector>

class ConfigService;
class HttpClient;
class NotificationManager;
namespace security {
  class SecretStore;
}

// Background service that syncs configured calendars (CalDAV, ICS subscriptions and local vdir folders)
// and exposes a merged, read-only event snapshot. Modeled on
// WeatherService: timer-driven via pollTimeoutMs()/tick(), durable credentials come from the
// account's explicit source, and an encrypted disk cache provides last-known-good data across
// restarts and network failures.
class CalendarService {
public:
  using ChangeCallback = std::function<void()>;
  using ChangeCallbackId = std::uint64_t;
  enum class CachePersistenceState : std::uint8_t {
    Opening,
    Ready,
    Unavailable,
    Cancelled,
    DeniedOrLocked,
    MissingKey,
    RecoveryRequired,
    BackendError,
  };
  enum class CredentialOperationResult : std::uint8_t {
    Success,
    MissingCredential,
    Unavailable,
    Cancelled,
    DeniedOrLocked,
    BackendError,
    FileError,
    ConfigError,
    CleanupError,
  };
  using ConfigMutation = std::function<bool()>;
  using CredentialOperationCallback = std::function<void(CredentialOperationResult)>;

  CalendarService(
      ConfigService& configService, HttpClient& httpClient, security::SecretStore& secretStore,
      security::StorageKeyProvider& storageKeyProvider, NotificationManager* notifications = nullptr
  );
  ~CalendarService();

  void initialize();
  [[nodiscard]] ChangeCallbackId addChangeCallback(ChangeCallback callback);
  void removeChangeCallback(ChangeCallbackId callbackId);
  void setCredentialChangeCallback(ChangeCallback callback) { m_credentialChangeCallback = std::move(callback); }

  [[nodiscard]] int pollTimeoutMs() const;
  void tick();
  void addPollFds(std::vector<pollfd>& fds);
  void dispatchPoll(const std::vector<pollfd>& fds, std::size_t startIdx);

  [[nodiscard]] bool enabled() const noexcept { return m_activeConfig.enabled; }
  [[nodiscard]] bool hasData() const noexcept { return m_snapshot.valid; }
  [[nodiscard]] const CalendarSnapshot& snapshot() const noexcept { return m_snapshot; }

  void saveCalDavAccount(
      const std::string& accountId, CalendarCredentialSource credentialSource, const std::string& passwordFile,
      std::string password, ConfigMutation persistConfig, CredentialOperationCallback callback
  );
  void deleteAccount(const std::string& accountId, ConfigMutation removeConfig, CredentialOperationCallback callback);
  void retryCredentialMigration();
  void syncCachePersistence();
  void retryCachePersistence();
  [[nodiscard]] bool clearEncryptedCacheForRecovery();
  // Schedule an immediate sync (used after saving CalDAV credentials).
  void requestRefresh();
  [[nodiscard]] calendar::CredentialState credentialState() const noexcept { return m_credentials.state(); }
  [[nodiscard]] bool credentialMigrationPending() const noexcept { return m_credentials.migrationPending(); }
  [[nodiscard]] CachePersistenceState cachePersistenceState() const noexcept { return m_cachePersistenceState; }
  [[nodiscard]] bool cacheMigrationPending() const noexcept { return m_cacheMigrationPending; }
  [[nodiscard]] bool hasEncryptedCache() const;

private:
  void onConfigReload();
  void notifyChanged();
  void startRefresh();
  void accountDone(const std::string& accountId, bool ok, std::vector<CalendarEvent> events);
  void rebuildSnapshot();
  void scheduleNextRefresh();

  void fetchCalDav(const CalendarConfig::Account& account);
  void lookupCalDavPassword(
      const CalendarConfig::Account& account, calendar::CalendarCredentialStore::LookupCallback callback
  );
  void fetchIcs(const CalendarConfig::Account& account);
  void fetchVdir(const CalendarConfig::Account& account);
  void updateVdirWatches();
  void initializeCredentials();
  [[nodiscard]] calendar::CredentialMigration credentialMigration();
  [[nodiscard]] static CredentialOperationResult operationResult(security::SecretStoreStatus status);

  [[nodiscard]] bool loadCache();
  [[nodiscard]] bool parseCache(std::span<const std::uint8_t> contents);
  void saveCache();
  [[nodiscard]] static std::filesystem::path cacheFilePath();
  [[nodiscard]] static std::filesystem::path legacyCacheFilePath();
  void setCachePersistenceState(CachePersistenceState state, bool migrationPending);

  ConfigService& m_configService;
  HttpClient& m_httpClient;
  NotificationManager* m_notifications = nullptr;
  CalendarConfig m_activeConfig;
  std::vector<std::pair<ChangeCallbackId, ChangeCallback>> m_callbacks;
  ChangeCallbackId m_nextCallbackId = 1;
  ChangeCallback m_credentialChangeCallback;

  calendar::CalendarCredentialStore m_credentials;
  security::StorageKeyProvider& m_storageKeyProvider;
  std::optional<security::SecureKey> m_cacheKey;

  CalendarSnapshot m_snapshot;
  std::map<std::string, std::vector<CalendarEvent>> m_eventsByAccount;
  std::chrono::steady_clock::time_point m_nextRefreshAt;
  bool m_refreshing = false;
  bool m_credentialsInitialized = false;
  bool m_initialized = false;
  bool m_cacheWritable = false;
  CachePersistenceState m_cachePersistenceState = CachePersistenceState::Opening;
  bool m_cacheMigrationPending = false;
  std::size_t m_pendingAccounts = 0;
  calendar::CalDavClient m_caldav;
  Inotify m_vdirInotify;
  std::map<std::filesystem::path, int> m_watchedVdirPaths;
  std::map<int, std::filesystem::path> m_watchedVdirWds;
  std::chrono::steady_clock::time_point m_vdirDebounceUntil;
  bool m_vdirDebouncePending = false;
  struct VdirWorker;
  std::shared_ptr<VdirWorker> m_vdirWorker;
  std::map<std::string, std::vector<std::filesystem::path>> m_discoveredVdirPathsByAccount;
};
