#include "system/location_service.h"

#include <cmath>
#include <optional>
#include <print>
#include <string>
#include <string_view>

namespace {

  bool expectResult(
      std::string_view label, const std::optional<LocationLookupResult>& actual, double latitude, double longitude,
      std::string_view name, std::string_view country
  ) {
    if (!actual.has_value()) {
      std::println(stderr, "location_lookup_test: {}: no result", label);
      return false;
    }
    if (std::abs(actual->latitude - latitude) > 1e-6
        || std::abs(actual->longitude - longitude) > 1e-6
        || actual->name != name
        || actual->country != country) {
      std::println(
          stderr, "location_lookup_test: {}: got {} {} '{}' '{}'", label, actual->latitude, actual->longitude,
          actual->name, actual->country
      );
      return false;
    }
    return true;
  }

  bool expectNone(std::string_view label, const std::optional<LocationLookupResult>& actual) {
    if (actual.has_value()) {
      std::println(stderr, "location_lookup_test: {}: expected no result", label);
      return false;
    }
    return true;
  }

  bool expectEqual(std::string_view label, const std::string& actual, std::string_view expected) {
    if (actual != expected) {
      std::println(stderr, "location_lookup_test: {}: got '{}', expected '{}'", label, actual, expected);
      return false;
    }
    return true;
  }

  // Trimmed answers in the shape the services return.
  constexpr std::string_view kIpWhoIs =
      R"({"ip":"203.0.113.7","success":true,"type":"IPv4","continent":"Europe","country":"Czechia",)"
      R"("country_code":"CZ","region":"Prague","city":"Prague","latitude":50.0880352,"longitude":14.4207571})";
  constexpr std::string_view kIpWhoIsFailure = R"({"ip":"203.0.113.7","success":false,"message":"Reserved range"})";
  constexpr std::string_view kIpApiStyle =
      R"({"ip":"203.0.113.7","city":"Berlin","country_name":"Germany","country":"DE","latitude":52.52,"longitude":13.405})";
  constexpr std::string_view kIpApiRateLimited = R"({"error":true,"reason":"RateLimited","message":"slow down"})";

  constexpr std::string_view kGeocodeBerlin =
      R"({"results":[)"
      R"({"id":2950159,"name":"Berlin","latitude":52.52437,"longitude":13.41053,"country_code":"DE",)"
      R"("country":"Germany","admin1":"State of Berlin"},)"
      R"({"id":5083330,"name":"Berlin","latitude":44.46867,"longitude":-71.18508,"country_code":"US",)"
      R"("country":"United States","admin1":"New Hampshire"}],"generationtime_ms":0.5})";
  constexpr std::string_view kGeocodeNothing = R"({"generationtime_ms":0.3})";

} // namespace

int main() {
  bool ok = true;

  ok = expectResult("ipwho.is", LocationService::parseIpLookup(kIpWhoIs), 50.0880352, 14.4207571, "Prague", "Czechia")
      && ok;
  ok = expectNone("ipwho.is failure", LocationService::parseIpLookup(kIpWhoIsFailure)) && ok;
  ok = expectResult("ipapi style", LocationService::parseIpLookup(kIpApiStyle), 52.52, 13.405, "Berlin", "Germany")
      && ok;
  ok = expectNone("rate limited", LocationService::parseIpLookup(kIpApiRateLimited)) && ok;
  ok = expectNone("not json", LocationService::parseIpLookup("<html>bad gateway</html>")) && ok;

  ok = expectResult(
           "geocode first", LocationService::parseGeocodeLookup(kGeocodeBerlin, "Berlin"), 52.52437, 13.41053, "Berlin",
           "Germany"
       )
      && ok;
  ok = expectResult(
           "geocode by country code", LocationService::parseGeocodeLookup(kGeocodeBerlin, "Berlin, us"), 44.46867,
           -71.18508, "Berlin", "United States"
       )
      && ok;
  ok = expectResult(
           "geocode by region", LocationService::parseGeocodeLookup(kGeocodeBerlin, "Berlin, New Hampshire"),
           44.46867, -71.18508, "Berlin", "United States"
       )
      && ok;
  ok = expectResult(
           "geocode unknown qualifier", LocationService::parseGeocodeLookup(kGeocodeBerlin, "Berlin, Atlantis"),
           52.52437, 13.41053, "Berlin", "Germany"
       )
      && ok;
  ok = expectNone("geocode nothing found", LocationService::parseGeocodeLookup(kGeocodeNothing, "Nowhere")) && ok;

  ok = expectEqual(
           "geocode url", LocationService::geocodeUrl("São Paulo"),
           "https://geocoding-api.open-meteo.com/v1/search?name=S%C3%A3o%20Paulo&count=1&language=en&format=json"
       )
      && ok;
  ok = expectEqual(
           "geocode url with qualifier", LocationService::geocodeUrl(" Berlin , Germany "),
           "https://geocoding-api.open-meteo.com/v1/search?name=Berlin&count=10&language=en&format=json"
       )
      && ok;

  return ok ? 0 : 1;
}
