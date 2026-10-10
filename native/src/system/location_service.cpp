#include "system/location_service.h"

#include "config/config_service.h"
#include "core/log.h"
#include "i18n/i18n.h"
#include "net/http_client.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace {

  constexpr Logger kLog("location");

  using Clock = std::chrono::system_clock;

  // IP-derived position can change while the shell runs (travel/VPN); re-resolve periodically.
  constexpr auto kRefreshInterval = std::chrono::hours(6);
  constexpr auto kRetryInterval = std::chrono::minutes(5);
  // Address geocoding is stable once resolved; idle until the address changes.
  constexpr auto kIdleInterval = std::chrono::hours(24 * 30);

  double readNumber(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_number()) {
      throw std::runtime_error(std::string("missing numeric key: ") + key);
    }
    return it->get<double>();
  }

  std::string readString(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_string()) {
      return {};
    }
    return it->get<std::string>();
  }

  constexpr std::string_view kIpLookupUrl = "https://ipwho.is/";
  constexpr std::string_view kGeocodeUrlBase = "https://geocoding-api.open-meteo.com/v1/search";

  std::string trimCopy(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
      return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
  }

  std::string lowerCopy(std::string_view text) {
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
  }

  // "Berlin, Germany" -> place "Berlin", qualifier "Germany". Open-Meteo matches place names only, so
  // the qualifier (country, country code or region) picks among the candidates instead.
  std::pair<std::string, std::string> splitAddress(std::string_view address) {
    const auto comma = address.find(',');
    if (comma == std::string_view::npos) {
      return {trimCopy(address), {}};
    }
    return {trimCopy(address.substr(0, comma)), trimCopy(address.substr(comma + 1))};
  }

  bool readBool(const nlohmann::json& json, const char* key) {
    const auto it = json.find(key);
    if (it == json.end() || !it->is_boolean()) {
      return false;
    }
    return it->get<bool>();
  }

} // namespace

LocationService::LocationService(ConfigService& configService, HttpClient& httpClient)
    : m_configService(configService), m_httpClient(httpClient) {}

void LocationService::initialize() {
  m_config = m_configService.config().location;
  m_configService.addReloadCallback([this]() { onConfigReload(); });
  loadCache();
  if (networkResolutionConfigured() && !m_resolved) {
    requestRefresh();
  }
}

void LocationService::addChangeCallback(ChangeCallback callback) { m_callbacks.push_back(std::move(callback)); }

bool LocationService::networkResolutionConfigured() const noexcept {
  return m_config.autoLocate || !m_config.address.empty();
}

bool LocationService::resolving() const noexcept { return networkResolutionConfigured() && !m_resolved; }

bool LocationService::hasResolvedLocation() const noexcept {
  return (m_resolved && resolvedCoordinatesValid()) || (!networkResolutionConfigured() && manualCoordinatesValid());
}

std::optional<ResolvedLocation> LocationService::resolvedLocation() const noexcept {
  if (m_resolved && resolvedCoordinatesValid()) {
    return ResolvedLocation{
        .latitude = m_latitude, .longitude = m_longitude, .name = m_name, .sourceLabel = m_sourceLabel
    };
  }
  if (networkResolutionConfigured() || !manualCoordinatesValid()) {
    return std::nullopt;
  }

  const double latitude = *m_config.latitude;
  const double longitude = *m_config.longitude;
  return ResolvedLocation{
      .latitude = latitude,
      .longitude = longitude,
      .name = std::format("{:.4f}, {:.4f}", latitude, longitude),
      .sourceLabel = i18n::tr("location.source.manual"),
  };
}

int LocationService::pollTimeoutMs() const {
  if (m_requestKind != RequestKind::None) {
    return -1;
  }
  if (m_refreshQueued) {
    return 0;
  }
  if (!networkResolutionConfigured()) {
    return -1;
  }
  const auto now = Clock::now();
  if (m_nextRefreshAt <= now) {
    return 0;
  }
  return static_cast<int>(std::chrono::ceil<std::chrono::milliseconds>(m_nextRefreshAt - now).count());
}

void LocationService::tick() {
  if (m_requestKind != RequestKind::None) {
    return;
  }
  if (!networkResolutionConfigured()) {
    m_refreshQueued = false;
    if (m_resolved || !m_error.empty()) {
      clearResolved();
      m_error.clear();
      notifyChanged();
    }
    return;
  }

  const auto now = Clock::now();
  if (!m_refreshQueued && now < m_nextRefreshAt) {
    return;
  }
  m_refreshQueued = false;

  if (m_config.autoLocate) {
    startGeolocate();
    return;
  }
  if (!m_resolved || m_resolvedAutoLocate || m_resolvedAddress != m_config.address) {
    startAddressGeocode();
    return;
  }
  m_nextRefreshAt = now + kIdleInterval;
}

void LocationService::onConfigReload() {
  const LocationConfig next = m_configService.config().location;
  if (next == m_config) {
    return;
  }
  const bool resolutionInputsChanged = next.autoLocate != m_config.autoLocate || next.address != m_config.address;
  const bool manualCoordinatesChanged = next.latitude != m_config.latitude || next.longitude != m_config.longitude;
  m_config = next;

  if (resolutionInputsChanged) {
    m_error.clear();
    if (networkResolutionConfigured()) {
      requestRefresh();
    } else {
      clearResolved();
    }
    notifyChanged();
  } else if (!networkResolutionConfigured() && manualCoordinatesChanged) {
    notifyChanged();
  }
}

void LocationService::notifyChanged() {
  for (const auto& callback : m_callbacks) {
    callback();
  }
}

void LocationService::requestRefresh() {
  m_refreshQueued = true;
  m_nextRefreshAt = Clock::time_point{};
}

void LocationService::startGeolocate() {
  std::error_code ec;
  std::filesystem::create_directories(transportCacheDir(), ec);
  const auto path = transportCacheDir() / "geolocate.json";
  const std::uint64_t serial = ++m_requestSerial;
  m_requestKind = RequestKind::Geolocate;
  m_httpClient.download(std::string(kIpLookupUrl), path, [this, path, serial](bool success) {
    handleResponse(path, true, success, serial);
  });
}

void LocationService::startAddressGeocode() {
  std::error_code ec;
  std::filesystem::create_directories(transportCacheDir(), ec);
  const auto path = transportCacheDir() / "geocode.json";
  const std::string url = geocodeUrl(m_config.address);
  const std::uint64_t serial = ++m_requestSerial;
  m_requestKind = RequestKind::GeocodeAddress;
  m_httpClient.download(url, path, [this, path, serial](bool success) {
    handleResponse(path, false, success, serial);
  });
}

void LocationService::handleResponse(
    const std::filesystem::path& path, bool autoLocated, bool success, std::uint64_t serial
) {
  if (serial != m_requestSerial) {
    return;
  }
  m_requestKind = RequestKind::None;

  if (!success) {
    m_error = autoLocated ? i18n::tr("location.errors.ip-geolocation-failed")
                          : i18n::tr("location.errors.address-lookup-failed");
    kLog.warn("{}", m_error);
    scheduleRetryAfterFailure();
    notifyChanged(); // serve last-known-good coordinates if we have them
    return;
  }

  try {
    std::ifstream file(path);
    const std::string body{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const auto result = autoLocated ? parseIpLookup(body) : parseGeocodeLookup(body, m_config.address);
    if (!result.has_value()) {
      throw std::runtime_error("no usable location in the response");
    }
    const std::string& name = result->name;
    const std::string& country = result->country;

    m_latitude = result->latitude;
    m_longitude = result->longitude;
    m_resolved = true;
    m_resolvedAutoLocate = autoLocated;
    m_resolvedAddress = m_config.address;
    m_sourceLabel = autoLocated ? i18n::tr("location.source.auto") : i18n::tr("location.source.address");
    m_name = compactLocationLabel(name, country);
    if (m_name.empty()) {
      m_name = autoLocated ? i18n::tr("location.locations.current") : m_config.address;
    }
    m_error.clear();
    m_nextRefreshAt = Clock::now() + kRefreshInterval;
    kLog.info("location resolved");
    saveCache();
    notifyChanged();
  } catch (const std::exception& e) {
    m_error =
        autoLocated ? i18n::tr("location.errors.parse-ip-geolocation") : i18n::tr("location.errors.parse-geocode");
    kLog.warn("{}: {}", m_error, e.what());
    scheduleRetryAfterFailure();
    notifyChanged();
  }
}

void LocationService::scheduleRetryAfterFailure() {
  m_refreshQueued = false;
  m_nextRefreshAt = Clock::now() + kRetryInterval;
}

void LocationService::clearResolved() {
  ++m_requestSerial;
  m_requestKind = RequestKind::None;
  m_resolved = false;
  m_resolvedAutoLocate = false;
  m_resolvedAddress.clear();
  m_name.clear();
  m_sourceLabel.clear();
  m_latitude = 0.0;
  m_longitude = 0.0;
  m_nextRefreshAt = Clock::time_point{};
}

bool LocationService::resolvedCoordinatesValid() const noexcept { return coordinatesValid(m_latitude, m_longitude); }

bool LocationService::manualCoordinatesValid() const noexcept {
  return m_config.latitude.has_value()
      && m_config.longitude.has_value()
      && coordinatesValid(*m_config.latitude, *m_config.longitude);
}

bool LocationService::coordinatesValid(double latitude, double longitude) noexcept {
  return std::isfinite(latitude)
      && std::isfinite(longitude)
      && latitude >= -90.0
      && latitude <= 90.0
      && longitude >= -180.0
      && longitude <= 180.0;
}

std::string LocationService::geocodeUrl(std::string_view address) {
  const auto [place, qualifier] = splitAddress(address);
  // With a qualifier, fetch a few candidates so parseGeocodeLookup can pick the matching one.
  return std::format(
      "{}?name={}&count={}&language=en&format=json", kGeocodeUrlBase, StringUtils::urlEncode(place),
      qualifier.empty() ? 1 : 10
  );
}

std::optional<LocationLookupResult> LocationService::parseIpLookup(std::string_view body) {
  const auto json = nlohmann::json::parse(body, nullptr, false);
  // ipwho.is answers {"success": false, "message": ...} on failure; ipapi.co-style services use "error".
  if (!json.is_object() || readBool(json, "error")) {
    return std::nullopt;
  }
  if (const auto success = json.find("success");
      success != json.end() && success->is_boolean() && !success->get<bool>()) {
    return std::nullopt;
  }
  const auto lat = json.find("latitude");
  const auto lon = json.find("longitude");
  if (lat == json.end() || lon == json.end() || !lat->is_number() || !lon->is_number()) {
    return std::nullopt;
  }
  LocationLookupResult result{
      .latitude = lat->get<double>(),
      .longitude = lon->get<double>(),
      .name = readString(json, "city"),
      .country = json.contains("country_name") ? readString(json, "country_name") : readString(json, "country"),
  };
  if (!coordinatesValid(result.latitude, result.longitude)) {
    return std::nullopt;
  }
  return result;
}

std::optional<LocationLookupResult>
LocationService::parseGeocodeLookup(std::string_view body, std::string_view address) {
  const auto json = nlohmann::json::parse(body, nullptr, false);
  if (!json.is_object()) {
    return std::nullopt;
  }
  const auto results = json.find("results");
  if (results == json.end() || !results->is_array()) {
    return std::nullopt;
  }

  const std::string qualifier = lowerCopy(splitAddress(address).second);
  const nlohmann::json* chosen = nullptr;
  for (const auto& entry : *results) {
    if (!entry.is_object()) {
      continue;
    }
    if (chosen == nullptr) {
      chosen = &entry; // best match by the service's own ranking
    }
    if (qualifier.empty()) {
      break;
    }
    if (lowerCopy(readString(entry, "country")) == qualifier
        || lowerCopy(readString(entry, "country_code")) == qualifier
        || lowerCopy(readString(entry, "admin1")) == qualifier) {
      chosen = &entry;
      break;
    }
  }
  if (chosen == nullptr) {
    return std::nullopt;
  }
  const auto lat = chosen->find("latitude");
  const auto lon = chosen->find("longitude");
  if (lat == chosen->end() || lon == chosen->end() || !lat->is_number() || !lon->is_number()) {
    return std::nullopt;
  }
  LocationLookupResult result{
      .latitude = lat->get<double>(),
      .longitude = lon->get<double>(),
      .name = readString(*chosen, "name"),
      .country = readString(*chosen, "country"),
  };
  if (!coordinatesValid(result.latitude, result.longitude)) {
    return std::nullopt;
  }
  return result;
}

std::filesystem::path LocationService::transportCacheDir() {
  return std::filesystem::path("/tmp") / "kusanagi-location";
}

std::filesystem::path LocationService::stateCacheFilePath() {
  if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && xdg[0] != '\0') {
    return std::filesystem::path(xdg) / "kusanagi" / "location.json";
  }
  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
    return std::filesystem::path(home) / ".cache" / "kusanagi" / "location.json";
  }
  return std::filesystem::path("/tmp") / "kusanagi-location-cache.json";
}

std::string LocationService::compactLocationLabel(const std::string& name, const std::string& country) {
  if (!name.empty() && !country.empty()) {
    return std::format("{}, {}", name, country);
  }
  if (!name.empty()) {
    return name;
  }
  return country;
}

void LocationService::loadCache() {
  const auto path = stateCacheFilePath();
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    return;
  }
  try {
    std::ifstream file(path);
    const auto json = nlohmann::json::parse(file);
    const bool cachedAutoLocate = readBool(json, "auto_locate");
    const std::string cachedAddress = readString(json, "address");
    if (cachedAutoLocate != m_config.autoLocate) {
      return;
    }
    if (!cachedAutoLocate && cachedAddress != m_config.address) {
      return;
    }

    m_latitude = readNumber(json, "latitude");
    m_longitude = readNumber(json, "longitude");
    if (!resolvedCoordinatesValid()) {
      m_latitude = 0.0;
      m_longitude = 0.0;
      return;
    }
    m_name = readString(json, "name");
    m_sourceLabel = readString(json, "source_label");
    m_resolvedAutoLocate = cachedAutoLocate;
    m_resolvedAddress = cachedAddress;
    m_resolved = true;
    m_nextRefreshAt = Clock::now() + kRefreshInterval;
    kLog.info("loaded cached location");
  } catch (const std::exception& e) {
    kLog.warn("failed to load location cache: {}", e.what());
  }
}

void LocationService::saveCache() const {
  if (!m_resolved) {
    return;
  }
  const auto path = stateCacheFilePath();
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);

  const nlohmann::json json{
      {"auto_locate", m_resolvedAutoLocate},
      {"address", m_resolvedAddress},
      {"latitude", m_latitude},
      {"longitude", m_longitude},
      {"name", m_name},
      {"source_label", m_sourceLabel},
  };
  try {
    std::ofstream file(path);
    file << json.dump(2);
  } catch (const std::exception& e) {
    kLog.warn("failed to save location cache: {}", e.what());
  }
}
