#include "theme/community_templates.h"

#include "core/deferred_call.h"
#include "core/log.h"
#include "core/toml.h" // IWYU pragma: keep
#include "net/http_client.h"
#include "util/checksum.h"
#include "util/file_utils.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace kusanagi::theme {

  namespace {

    constexpr Logger kLog("community_templates");

    struct CommunityTemplateFile {
      std::string name;
      std::string md5;
      std::optional<std::filesystem::perms> mode;
    };

    struct CommunityTemplateEntry {
      std::string id;
      std::string inputPath;
      std::vector<std::string> outputPaths;
      std::string preHook;
      std::string postHook;
      std::string postAction;
      std::optional<int> index;
    };

    struct CommunityTemplateInfo {
      std::string id;
      std::string displayName;
      std::string category;
      std::vector<CommunityTemplateFile> files;
      std::vector<CommunityTemplateEntry> entries;
    };

    std::filesystem::path catalogCachePath() { return communityTemplatesCacheDir() / "catalog.json"; }

    std::string stringField(const nlohmann::json& obj, std::string_view snake, std::string_view camel = {}) {
      auto read = [&](std::string_view key) -> std::string {
        if (key.empty())
          return {};
        auto it = obj.find(std::string(key));
        if (it == obj.end() || !it->is_string())
          return {};
        return it->get<std::string>();
      };
      std::string value = read(snake);
      if (value.empty())
        value = read(camel);
      return value;
    }

    std::vector<std::string> readJsonStringArrayOrString(const nlohmann::json& obj, std::string_view key) {
      std::vector<std::string> out;
      auto it = obj.find(std::string(key));
      if (it == obj.end())
        return out;
      if (it->is_string()) {
        out.push_back(it->get<std::string>());
        return out;
      }
      if (!it->is_array())
        return out;
      for (const auto& item : *it) {
        if (item.is_string())
          out.push_back(item.get<std::string>());
      }
      return out;
    }

    std::optional<CommunityTemplateEntry> parseEntry(std::string_view fallbackId, const nlohmann::json& obj) {
      if (!obj.is_object())
        return std::nullopt;

      CommunityTemplateEntry entry;
      entry.id = stringField(obj, "id", "name");
      if (entry.id.empty())
        entry.id = std::string(fallbackId);
      entry.inputPath = stringField(obj, "input_path", "inputPath");
      entry.outputPaths = readJsonStringArrayOrString(obj, "output_path");
      if (entry.outputPaths.empty())
        entry.outputPaths = readJsonStringArrayOrString(obj, "outputPath");
      entry.preHook = stringField(obj, "pre_hook", "preHook");
      entry.postHook = stringField(obj, "post_hook", "postHook");
      entry.postAction = stringField(obj, "post_action", "postAction");
      if (auto index = obj.find("index"); index != obj.end() && index->is_number_integer())
        entry.index = index->get<int>();

      if (entry.id.empty() || entry.inputPath.empty())
        return std::nullopt;
      return entry;
    }

    std::vector<CommunityTemplateEntry> parseEntries(const nlohmann::json& obj) {
      std::vector<CommunityTemplateEntry> out;
      auto it = obj.find("templates");
      if (it == obj.end())
        return out;

      if (it->is_array()) {
        for (const auto& item : *it) {
          if (auto entry = parseEntry({}, item))
            out.push_back(std::move(*entry));
        }
      } else if (it->is_object()) {
        for (const auto& [key, value] : it->items()) {
          if (auto entry = parseEntry(key, value))
            out.push_back(std::move(*entry));
        }
      }
      return out;
    }

    std::optional<std::filesystem::perms> parseFileMode(std::string_view mode) {
      if (mode.size() != 4)
        return std::nullopt;

      unsigned int value = 0;
      for (char ch : mode) {
        if (ch < '0' || ch > '7')
          return std::nullopt;
        value = (value << 3U) | static_cast<unsigned int>(ch - '0');
      }
      if ((value & 07000U) != 0)
        return std::nullopt;
      return static_cast<std::filesystem::perms>(value);
    }

    std::vector<CommunityTemplateFile> parseFiles(std::string_view templateId, const nlohmann::json& obj) {
      std::vector<CommunityTemplateFile> out;
      auto it = obj.find("files");
      if (it == obj.end() || !it->is_array())
        return out;
      for (const auto& item : *it) {
        if (!item.is_object())
          continue;
        CommunityTemplateFile file;
        file.name = stringField(item, "name");
        file.md5 = stringField(item, "md5");
        if (file.name.empty())
          continue;

        const std::string mode = stringField(item, "mode");
        if (!mode.empty()) {
          if (auto parsed = parseFileMode(mode)) {
            file.mode = parsed;
          } else {
            kLog.warn("community template '{}' file '{}' has invalid mode metadata '{}'", templateId, file.name, mode);
          }
        }
        out.push_back(std::move(file));
      }
      return out;
    }

    std::optional<CommunityTemplateInfo> parseInfo(const nlohmann::json& obj) {
      if (!obj.is_object())
        return std::nullopt;
      CommunityTemplateInfo info;
      info.id = stringField(obj, "name", "id");
      if (info.id.empty() || !isSafeCommunityTemplateId(info.id))
        return std::nullopt;
      info.displayName = stringField(obj, "display_name", "displayName");
      if (info.displayName.empty())
        info.displayName = info.id;
      info.category = stringField(obj, "category");
      info.files = parseFiles(info.id, obj);
      info.entries = parseEntries(obj);
      return info;
    }

    std::vector<CommunityTemplateInfo> parseCatalogFile(const std::filesystem::path& path) {
      std::ifstream in(path);
      if (!in)
        return {};
      try {
        std::stringstream buf;
        buf << in.rdbuf();
        const auto root = nlohmann::json::parse(buf.str());
        const nlohmann::json* entries = &root;
        if (root.is_object()) {
          auto it = root.find("templates");
          if (it != root.end())
            entries = &*it;
        }
        if (!entries->is_array())
          return {};

        std::vector<CommunityTemplateInfo> out;
        for (const auto& item : *entries) {
          if (auto info = parseInfo(item))
            out.push_back(std::move(*info));
        }
        return out;
      } catch (const std::exception& e) {
        kLog.warn("failed to parse community template catalog {}: {}", path.string(), e.what());
        return {};
      }
    }

    void appendTemplateOutputMetadata(AvailableTemplate& output, const toml::table& root) {
      const toml::table* templates = root["templates"].as_table();
      if (templates == nullptr)
        return;
      for (const auto& [idNode, node] : *templates) {
        const toml::table* tpl = node.as_table();
        if (tpl == nullptr)
          continue;
        if (tpl->get_as<std::string>("output_path_dynamic") != nullptr) {
          output.outputDynamic = true;
        }
        const toml::node* op = tpl->get("output_path");
        if (op == nullptr)
          continue;
        if (const auto str = op->as_string()) {
          output.outputPaths.push_back(str->get());
        } else if (const auto arr = op->as_array()) {
          for (const auto& item : *arr) {
            if (const auto itemStr = item.as_string())
              output.outputPaths.push_back(itemStr->get());
          }
        }
      }
    }

    std::optional<AvailableTemplate> readTemplateTomlInfo(const std::filesystem::path& path, std::string_view cacheId) {
      if (!isSafeCommunityTemplateId(cacheId))
        return std::nullopt;

      try {
        toml::table root = toml::parse_file(path.string());
        const toml::table* catalog = root["catalog"].as_table();
        if (catalog == nullptr || catalog->empty())
          return std::nullopt;

        AvailableTemplate out;
        out.id = std::string(cacheId);
        out.displayName = out.id;

        const toml::table* info = catalog->get_as<toml::table>(cacheId);
        if (info == nullptr) {
          kLog.warn(
              "cached community template metadata {} does not contain catalog entry '{}'; using cache directory name",
              path.string(), cacheId
          );
          appendTemplateOutputMetadata(out, root);
          return out;
        }

        {
          if (const auto name = info->get_as<std::string>("name"))
            out.displayName = name->get();
          if (const auto category = info->get_as<std::string>("category"))
            out.category = category->get();
        }
        appendTemplateOutputMetadata(out, root);
        return out;
      } catch (const toml::parse_error&) {
        return std::nullopt;
      }
    }

    void appendOutputPathsFromCacheToml(AvailableTemplate& t) {
      const auto tomlPath = communityTemplateConfigPath(t.id);
      if (!std::filesystem::exists(tomlPath))
        return;
      try {
        const toml::table root = toml::parse_file(tomlPath.string());
        appendTemplateOutputMetadata(t, root);
      } catch (const toml::parse_error&) {
      }
    }

  } // namespace

  std::vector<AvailableTemplate> CommunityTemplateService::availableTemplates() {
    std::vector<AvailableTemplate> out;
    const auto catalog = parseCatalogFile(catalogCachePath());
    out.reserve(catalog.size());
    for (const auto& info : catalog) {
      AvailableTemplate t;
      t.id = info.id;
      t.displayName = info.displayName.empty() ? info.id : info.displayName;
      t.category = info.category;
      appendOutputPathsFromCacheToml(t);
      out.push_back(std::move(t));
    }

    const std::filesystem::path cacheDir = communityTemplatesCacheDir();
    std::error_code ec;
    if (!std::filesystem::is_directory(cacheDir, ec))
      return out;

    for (const auto& entry : std::filesystem::directory_iterator(cacheDir, ec)) {
      if (!entry.is_directory())
        continue;
      const std::string cacheId = entry.path().filename().string();
      const bool exists = std::ranges::contains(out, cacheId, &AvailableTemplate::id);
      if (exists)
        continue;
      const auto toml = entry.path() / "template.toml";
      if (!std::filesystem::exists(toml))
        continue;
      if (auto info = readTemplateTomlInfo(toml, cacheId)) {
        out.push_back(std::move(*info));
      }
    }

    std::ranges::sort(out, [](const AvailableTemplate& a, const AvailableTemplate& b) {
      if (a.category != b.category)
        return a.category < b.category;
      if (a.displayName != b.displayName)
        return a.displayName < b.displayName;
      return a.id < b.id;
    });
    return out;
  }

  // Stored under the state dir (not XDG cache): app-managed, re-fetchable data that
  // should persist between sessions and not be auto-reclaimed by cache cleaners.
  std::filesystem::path communityTemplatesCacheDir() {
    const std::string state = FileUtils::stateDir();
    if (!state.empty())
      return std::filesystem::path(state) / "community-templates";
    return std::filesystem::path("/tmp") / "kusanagi" / "community-templates";
  }

  std::filesystem::path communityTemplateDir(std::string_view id) {
    return communityTemplatesCacheDir() / std::string(id);
  }

  std::filesystem::path communityTemplateConfigPath(std::string_view id) {
    return communityTemplateDir(id) / "template.toml";
  }

  bool isSafeCommunityTemplateId(std::string_view id) {
    return !id.empty() && std::ranges::all_of(id, [](unsigned char ch) {
      return std::isalnum(ch) != 0 || ch == '_' || ch == '-' || ch == '.';
    });
  }

} // namespace kusanagi::theme
