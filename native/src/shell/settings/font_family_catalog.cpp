#include "shell/settings/font_family_catalog.h"

#include "core/process/process.h"
#include "i18n/i18n.h"
#include "util/string_utils.h"

#include <algorithm>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace settings {
  namespace {

    std::vector<std::string> splitList(std::string_view s) {
      std::vector<std::string> out;
      std::size_t start = 0;
      while (start <= s.size()) {
        const std::size_t end = s.find(',', start);
        out.push_back(StringUtils::trim(std::string(s.substr(start, end == std::string_view::npos ? s.size() - start : end - start))));
        if (end == std::string_view::npos) break;
        start = end + 1;
      }
      return out;
    }

    // Family names the way Qt's font database lists them: each font's first family, plus a
    // subfamily only when it names a different style in the same language (a "Light" family with
    // its own Regular). Other names, like a family repeated per style or another language's name,
    // are aliases and stay out of the list.
    std::vector<std::string> discoverFontFamiliesUncached() {
      std::vector<std::string> families;
      if (!process::commandExists("fc-list")) {
        return families;
      }

      const auto result = process::runSync({"fc-list", "--format", "%{family}\t%{familylang}\t%{style}\n"});
      if (!result) {
        return families;
      }

      std::unordered_set<std::string> seen;
      seen.reserve(2048);

      std::size_t lineStart = 0;
      while (lineStart < result.out.size()) {
        std::size_t lineEnd = result.out.find('\n', lineStart);
        if (lineEnd == std::string::npos) {
          lineEnd = result.out.size();
        }
        const std::string_view line = std::string_view(result.out).substr(lineStart, lineEnd - lineStart);
        lineStart = lineEnd + 1;

        const std::size_t tab1 = line.find('\t');
        const std::size_t tab2 = tab1 == std::string_view::npos ? std::string_view::npos : line.find('\t', tab1 + 1);
        const std::vector<std::string> names = splitList(line.substr(0, tab1));
        const std::vector<std::string> langs =
            tab1 == std::string_view::npos ? std::vector<std::string>{} : splitList(line.substr(tab1 + 1, tab2 - tab1 - 1));
        const std::vector<std::string> styles =
            tab2 == std::string_view::npos ? std::vector<std::string>{} : splitList(line.substr(tab2 + 1));
        if (names.empty() || names[0].empty()) {
          continue;
        }
        const std::string lang = langs.empty() ? std::string() : langs[0];
        const std::string style = styles.empty() ? std::string() : styles[0];
        seen.insert(names[0]);
        for (std::size_t k = 1; k < names.size(); ++k) {
          const std::string& altLang = k < langs.size() ? langs[k] : lang;
          const std::string& altStyle = k < styles.size() ? styles[k] : style;
          if (!names[k].empty() && altLang == lang && altStyle != style) {
            seen.insert(names[k]);
          }
        }
      }

      families.assign(seen.begin(), seen.end());
      std::ranges::sort(families, [](const std::string& a, const std::string& b) {
        return StringUtils::toLower(a) < StringUtils::toLower(b);
      });
      return families;
    }

  } // namespace

  const std::vector<std::string>& discoverFontFamilies() {
    static const std::vector<std::string> kFamilies = discoverFontFamiliesUncached();
    return kFamilies;
  }

  std::vector<WidgetSettingSelectOption> buildFontFamilySelectOptions() {
    const std::vector<std::string>& families = discoverFontFamilies();

    std::vector<WidgetSettingSelectOption> options;
    options.reserve(families.size() + 1);
    options.push_back(WidgetSettingSelectOption{"", i18n::tr("desktop-widgets.editor.settings.font-family-default")});
    for (const std::string& family : families) {
      options.push_back(WidgetSettingSelectOption{family, family});
    }
    return options;
  }

} // namespace settings
