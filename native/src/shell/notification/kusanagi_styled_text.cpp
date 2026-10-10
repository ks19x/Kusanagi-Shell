// Styled text to Pango markup (see kusanagi_styled_text.h). The parser follows Qt 6's qquickstyledtext.cpp
// closely and works byte-wise on UTF-8, since every character it reacts to is ASCII.
#include "shell/notification/kusanagi_styled_text.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <glib.h>
#include <optional>
#include <pango/pango.h>
#include <utility>
#include <vector>

namespace kusanagi {

  namespace {

    struct Entity {
      std::string_view name;
      std::uint32_t cp;
    };

    // Named entities: HTML 4 plus apos, sorted for a binary search.
    constexpr Entity kEntities[] = {
      {"AElig", 0xC6}, {"Aacute", 0xC1}, {"Acirc", 0xC2}, {"Agrave", 0xC0}, {"Alpha", 0x391}, {"Aring", 0xC5},
      {"Atilde", 0xC3}, {"Auml", 0xC4}, {"Beta", 0x392}, {"Ccedil", 0xC7}, {"Chi", 0x3A7}, {"Dagger", 0x2021},
      {"Delta", 0x394}, {"ETH", 0xD0}, {"Eacute", 0xC9}, {"Ecirc", 0xCA}, {"Egrave", 0xC8}, {"Epsilon", 0x395},
      {"Eta", 0x397}, {"Euml", 0xCB}, {"Gamma", 0x393}, {"Iacute", 0xCD}, {"Icirc", 0xCE}, {"Igrave", 0xCC},
      {"Iota", 0x399}, {"Iuml", 0xCF}, {"Kappa", 0x39A}, {"Lambda", 0x39B}, {"Mu", 0x39C}, {"Ntilde", 0xD1},
      {"Nu", 0x39D}, {"OElig", 0x152}, {"Oacute", 0xD3}, {"Ocirc", 0xD4}, {"Ograve", 0xD2}, {"Omega", 0x3A9},
      {"Omicron", 0x39F}, {"Oslash", 0xD8}, {"Otilde", 0xD5}, {"Ouml", 0xD6}, {"Phi", 0x3A6}, {"Pi", 0x3A0},
      {"Prime", 0x2033}, {"Psi", 0x3A8}, {"Rho", 0x3A1}, {"Scaron", 0x160}, {"Sigma", 0x3A3}, {"THORN", 0xDE},
      {"Tau", 0x3A4}, {"Theta", 0x398}, {"Uacute", 0xDA}, {"Ucirc", 0xDB}, {"Ugrave", 0xD9}, {"Upsilon", 0x3A5},
      {"Uuml", 0xDC}, {"Xi", 0x39E}, {"Yacute", 0xDD}, {"Yuml", 0x178}, {"Zeta", 0x396}, {"aacute", 0xE1},
      {"acirc", 0xE2}, {"acute", 0xB4}, {"aelig", 0xE6}, {"agrave", 0xE0}, {"alefsym", 0x2135}, {"alpha", 0x3B1},
      {"amp", 0x26}, {"and", 0x2227}, {"ang", 0x2220}, {"apos", 0x27}, {"aring", 0xE5}, {"asymp", 0x2248},
      {"atilde", 0xE3}, {"auml", 0xE4}, {"bdquo", 0x201E}, {"beta", 0x3B2}, {"brvbar", 0xA6}, {"bull", 0x2022},
      {"cap", 0x2229}, {"ccedil", 0xE7}, {"cedil", 0xB8}, {"cent", 0xA2}, {"chi", 0x3C7}, {"circ", 0x2C6},
      {"clubs", 0x2663}, {"cong", 0x2245}, {"copy", 0xA9}, {"crarr", 0x21B5}, {"cup", 0x222A}, {"curren", 0xA4},
      {"dArr", 0x21D3}, {"dagger", 0x2020}, {"darr", 0x2193}, {"deg", 0xB0}, {"delta", 0x3B4}, {"diams", 0x2666},
      {"divide", 0xF7}, {"eacute", 0xE9}, {"ecirc", 0xEA}, {"egrave", 0xE8}, {"empty", 0x2205}, {"emsp", 0x2003},
      {"ensp", 0x2002}, {"epsilon", 0x3B5}, {"equiv", 0x2261}, {"eta", 0x3B7}, {"eth", 0xF0}, {"euml", 0xEB},
      {"euro", 0x20AC}, {"exist", 0x2203}, {"fnof", 0x192}, {"forall", 0x2200}, {"frac12", 0xBD}, {"frac14", 0xBC},
      {"frac34", 0xBE}, {"frasl", 0x2044}, {"gamma", 0x3B3}, {"ge", 0x2265}, {"gt", 0x3E}, {"hArr", 0x21D4},
      {"harr", 0x2194}, {"hearts", 0x2665}, {"hellip", 0x2026}, {"iacute", 0xED}, {"icirc", 0xEE}, {"iexcl", 0xA1},
      {"igrave", 0xEC}, {"image", 0x2111}, {"infin", 0x221E}, {"int", 0x222B}, {"iota", 0x3B9}, {"iquest", 0xBF},
      {"isin", 0x2208}, {"iuml", 0xEF}, {"kappa", 0x3BA}, {"lArr", 0x21D0}, {"lambda", 0x3BB}, {"lang", 0x2329},
      {"laquo", 0xAB}, {"larr", 0x2190}, {"lceil", 0x2308}, {"ldquo", 0x201C}, {"le", 0x2264}, {"lfloor", 0x230A},
      {"lowast", 0x2217}, {"loz", 0x25CA}, {"lrm", 0x200E}, {"lsaquo", 0x2039}, {"lsquo", 0x2018}, {"lt", 0x3C},
      {"macr", 0xAF}, {"mdash", 0x2014}, {"micro", 0xB5}, {"middot", 0xB7}, {"minus", 0x2212}, {"mu", 0x3BC},
      {"nabla", 0x2207}, {"nbsp", 0xA0}, {"ndash", 0x2013}, {"ne", 0x2260}, {"ni", 0x220B}, {"not", 0xAC},
      {"notin", 0x2209}, {"nsub", 0x2284}, {"ntilde", 0xF1}, {"nu", 0x3BD}, {"oacute", 0xF3}, {"ocirc", 0xF4},
      {"oelig", 0x153}, {"ograve", 0xF2}, {"oline", 0x203E}, {"omega", 0x3C9}, {"omicron", 0x3BF}, {"oplus", 0x2295},
      {"or", 0x2228}, {"ordf", 0xAA}, {"ordm", 0xBA}, {"oslash", 0xF8}, {"otilde", 0xF5}, {"otimes", 0x2297},
      {"ouml", 0xF6}, {"para", 0xB6}, {"part", 0x2202}, {"permil", 0x2030}, {"perp", 0x22A5}, {"phi", 0x3C6},
      {"pi", 0x3C0}, {"piv", 0x3D6}, {"plusmn", 0xB1}, {"pound", 0xA3}, {"prime", 0x2032}, {"prod", 0x220F},
      {"prop", 0x221D}, {"psi", 0x3C8}, {"quot", 0x22}, {"rArr", 0x21D2}, {"radic", 0x221A}, {"rang", 0x232A},
      {"raquo", 0xBB}, {"rarr", 0x2192}, {"rceil", 0x2309}, {"rdquo", 0x201D}, {"real", 0x211C}, {"reg", 0xAE},
      {"rfloor", 0x230B}, {"rho", 0x3C1}, {"rlm", 0x200F}, {"rsaquo", 0x203A}, {"rsquo", 0x2019}, {"sbquo", 0x201A},
      {"scaron", 0x161}, {"sdot", 0x22C5}, {"sect", 0xA7}, {"shy", 0xAD}, {"sigma", 0x3C3}, {"sigmaf", 0x3C2},
      {"sim", 0x223C}, {"spades", 0x2660}, {"sub", 0x2282}, {"sube", 0x2286}, {"sum", 0x2211}, {"sup", 0x2283},
      {"sup1", 0xB9}, {"sup2", 0xB2}, {"sup3", 0xB3}, {"supe", 0x2287}, {"szlig", 0xDF}, {"tau", 0x3C4},
      {"there4", 0x2234}, {"theta", 0x3B8}, {"thetasym", 0x3D1}, {"thinsp", 0x2009}, {"thorn", 0xFE},
      {"tilde", 0x2DC}, {"times", 0xD7}, {"trade", 0x2122}, {"uArr", 0x21D1}, {"uacute", 0xFA}, {"uarr", 0x2191},
      {"ucirc", 0xFB}, {"ugrave", 0xF9}, {"uml", 0xA8}, {"upsih", 0x3D2}, {"upsilon", 0x3C5}, {"uuml", 0xFC},
      {"weierp", 0x2118}, {"xi", 0x3BE}, {"yacute", 0xFD}, {"yen", 0xA5}, {"yuml", 0xFF}, {"zeta", 0x3B6},
      {"zwj", 0x200D}, {"zwnj", 0x200C},
    };

    // &#128; to &#159; mean their Windows-1252 characters.
    constexpr std::uint16_t kWindows1252[32] = {
        0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
        0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD, 0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
    };

    std::string utf8(std::uint32_t cp) {
      char buf[8];
      const int n = g_unichar_to_utf8(static_cast<gunichar>(cp), buf);
      return {buf, static_cast<std::size_t>(n)};
    }

    // The text of `&entity;` (given without & and ;), or nothing for an unknown entity.
    std::optional<std::string> decodeEntity(std::string_view e) {
      if (e.size() >= 2 && e[0] == '#') {
        int base = 10;
        std::string_view digits = e.substr(1);
        if (digits[0] == 'x' || digits[0] == 'X') {
          base = 16;
          digits = digits.substr(1);
        }
        if (digits.empty() || digits.size() > 8) return std::nullopt;
        std::uint32_t cp = 0;
        for (const char c : digits) {
          const int d = g_ascii_xdigit_value(c);
          if (d < 0 || d >= base) return std::nullopt;
          cp = cp * static_cast<std::uint32_t>(base) + static_cast<std::uint32_t>(d);
        }
        if (cp >= 0x80 && cp < 0xA0) cp = kWindows1252[cp - 0x80];
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return std::nullopt;
        return utf8(cp);
      }
      const auto* end = std::end(kEntities);
      const auto* it = std::lower_bound(std::begin(kEntities), end, e, [](const Entity& a, std::string_view b) {
        return a.name < b;
      });
      if (it == end || it->name != e) return std::nullopt;
      return utf8(it->cp);
    }

    // Whitespace in the ASCII range; the parser only ever looks at ASCII.
    bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

    // SVG colour names that Pango's X11 table reads differently.
    std::optional<std::string_view> svgOverride(std::string_view name) {
      static constexpr std::pair<std::string_view, std::string_view> kNames[] = {
          {"gray", "#808080"}, {"grey", "#808080"}, {"green", "#008000"}, {"maroon", "#800000"}, {"purple", "#800080"},
      };
      for (const auto& [n, hex] : kNames) {
        if (g_ascii_strcasecmp(std::string(name).c_str(), std::string(n).c_str()) == 0) return hex;
      }
      return std::nullopt;
    }

    struct Format {
      bool bold = false;
      bool italic = false;
      bool underline = false;
      bool strike = false;
      bool mono = false;
      std::string color;   // Pango colour, empty = the label's
      int colorAlpha = -1; // 0..100 for #aarrggbb
      float scale = 0.0F;  // times the base size (0 = base)
      bool operator==(const Format&) const = default;
    };

    struct Run {
      std::string text;
      Format format;
    };

    enum class ListFormat { Bullet, Disc, Square, Decimal, LowerAlpha, UpperAlpha, LowerRoman, UpperRoman };

    struct List {
      int level = 0;
      ListFormat format = ListFormat::Bullet;
    };

    std::string toAlpha(int value, bool upper) {
      std::string out;
      const char base = upper ? 'A' : 'a';
      for (int c = value; c > 0; c /= 26) {
        --c;
        out.insert(out.begin(), static_cast<char>(base + c % 26));
      }
      return out;
    }

    std::string toRoman(int value, bool upper) {
      if (value <= 0 || value >= 5000) return std::to_string(value);
      static constexpr std::pair<int, std::string_view> kSteps[] = {
          {1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"}, {90, "xc"}, {50, "l"},
          {40, "xl"},  {10, "x"},   {9, "ix"},  {5, "v"},    {4, "iv"},  {1, "i"},
      };
      std::string out;
      for (const auto& [n, s] : kSteps) {
        while (value >= n) {
          out += s;
          value -= n;
        }
      }
      if (upper) std::transform(out.begin(), out.end(), out.begin(), [](char c) { return g_ascii_toupper(c); });
      return out;
    }

    constexpr std::string_view kLineSeparator = "\n";
    constexpr std::string_view kNbsp = " ";
    constexpr int kTabSize = 6;
    // Scales for <font size="1".."7"> and <hN>.
    constexpr float kFontScaling[7] = {0.7F, 0.8F, 1.0F, 1.2F, 1.5F, 2.0F, 2.4F};

    class Parser {
    public:
      explicit Parser(std::string_view in) : m_in(in) {}

      std::vector<Run> parse() {
        std::size_t textStart = 0;
        std::size_t textLength = 0;
        while (m_i < m_in.size()) {
          const char c = m_in[m_i];
          if (c == '<') {
            if (textLength != 0) {
              appendText(textStart, textLength);
            } else if (m_prependSpace) {
              emit(" ");
              m_prependSpace = false;
              m_hasSpace = true;
            }
            ++m_i;
            if (m_i < m_in.size() && m_in[m_i] == '/') {
              ++m_i;
              if (parseCloseTag() && !m_stack.empty()) m_stack.pop_back();
            } else {
              Format format = top();
              if (parseTag(format)) m_stack.push_back(std::move(format));
            }
            textStart = m_i + 1;
            textLength = 0;
          } else if (c == '&') {
            ++m_i;
            appendText(textStart, textLength);
            parseEntity();
            textStart = m_i + 1;
            textLength = 0;
          } else if (isSpace(c)) {
            if (textLength != 0) appendText(textStart, textLength);
            if (!m_preFormat) {
              m_prependSpace = !m_hasSpace;
              while (m_i + 1 < m_in.size() && isSpace(m_in[m_i + 1])) ++m_i;
              m_hasNewLine = false;
            } else if (c == '\n') {
              emit(kLineSeparator);
              m_hasNewLine = true;
            } else {
              emit(kNbsp);
              m_hasNewLine = false;
            }
            textStart = m_i + 1;
            textLength = 0;
          } else {
            ++textLength;
          }
          if (m_i < m_in.size()) ++m_i;
        }
        if (textLength != 0) appendText(textStart, textLength);
        return std::move(m_runs);
      }

    private:
      [[nodiscard]] Format top() const { return m_stack.empty() ? Format{} : m_stack.back(); }
      [[nodiscard]] bool atEnd() const { return m_i >= m_in.size(); }
      [[nodiscard]] char ch() const { return atEnd() ? '\0' : m_in[m_i]; }

      void emit(std::string_view s) {
        if (s.empty()) return;
        const Format f = top();
        if (!m_runs.empty() && m_runs.back().format == f) {
          m_runs.back().text += s;
        } else {
          m_runs.push_back({std::string(s), f});
        }
      }

      void appendText(std::size_t start, std::size_t length) {
        if (m_prependSpace) emit(" ");
        if (start < m_in.size()) emit(m_in.substr(start, length));
        m_prependSpace = false;
        m_hasSpace = false;
        m_hasNewLine = false;
      }

      void skipSpace() {
        while (!atEnd() && isSpace(m_in[m_i])) ++m_i;
      }

      void newLineUnlessFresh() {
        if (!m_hasNewLine) emit(kLineSeparator);
      }

      void setFontSize(int size, Format& f) { f.scale = kFontScaling[std::clamp(size, 1, 7) - 1]; }

      bool parseTag(Format& format) {
        skipSpace();
        const std::size_t tagStart = m_i;
        std::size_t tagLength = 0;
        while (!atEnd()) {
          const char c = m_in[m_i];
          if (c == '>') {
            if (tagLength == 0) return false;
            const std::string_view tag = m_in.substr(tagStart, tagLength);
            const char c0 = g_ascii_tolower(tag[0]);
            if (c0 == 'b') {
              if (tagLength == 1) {
                format.bold = true;
                return true;
              }
              if (tagLength == 2 && g_ascii_tolower(tag[1]) == 'r') {
                emit(kLineSeparator);
                m_hasSpace = true;
                m_prependSpace = false;
                return false;
              }
            } else if (c0 == 'i') {
              if (tagLength == 1) {
                format.italic = true;
                return true;
              }
            } else if (c0 == 'p') {
              if (tagLength == 1) {
                newLineUnlessFresh();
                m_hasSpace = true;
                m_prependSpace = false;
              } else if (tag == "pre") {
                m_preFormat = true;
                newLineUnlessFresh();
                format.mono = true;
                return true;
              }
            } else if (c0 == 'u') {
              if (tagLength == 1) {
                format.underline = true;
                return true;
              }
              if (tag == "ul") m_lists.push_back({0, ListFormat::Bullet});
            } else if (c0 == 's') {
              if (tagLength == 1) {
                format.strike = true;
                return true;
              }
              if (tag == "strong") {
                format.bold = true;
                return true;
              }
            } else if (c0 == 'h' && tagLength == 2) {
              const int level = g_ascii_digit_value(tag[1]);
              if (level >= 1 && level <= 6) {
                newLineUnlessFresh();
                m_hasSpace = true;
                m_prependSpace = false;
                setFontSize(7 - level, format);
                format.bold = true;
                return true;
              }
            } else if (tag == "del") {
              format.strike = true;
              return true;
            } else if (tag == "ol") {
              m_lists.push_back({0, ListFormat::Decimal});
            } else if (tag == "li") {
              newLineUnlessFresh();
              if (!m_lists.empty()) {
                const int count = ++m_lists.back().level;
                std::string prefix;
                for (std::size_t i = 0; i < m_lists.size() * kTabSize; ++i) prefix += kNbsp;
                switch (m_lists.back().format) {
                case ListFormat::Decimal: prefix += std::to_string(count) + "."; break;
                case ListFormat::LowerAlpha: prefix += toAlpha(count, false) + "."; break;
                case ListFormat::UpperAlpha: prefix += toAlpha(count, true) + "."; break;
                case ListFormat::LowerRoman: prefix += toRoman(count, false) + "."; break;
                case ListFormat::UpperRoman: prefix += toRoman(count, true) + "."; break;
                case ListFormat::Bullet: prefix += "•"; break;
                case ListFormat::Disc: prefix += "◦"; break;
                case ListFormat::Square: prefix += "□"; break;
                }
                prefix += kNbsp;
                prefix += kNbsp;
                emit(prefix);
              }
            }
            return false;
          }
          if (isSpace(c)) {
            // may have attributes
            const std::string_view tag = m_in.substr(tagStart, tagLength);
            if (tag == "font") return parseFontAttributes(format);
            if (tag == "ol" || tag == "ul") {
              parseListAttributes(tag == "ol");
              return false;
            }
            if (tag == "a") return parseAnchorAttributes(format);
            if (tag == "img") {
              // Stripped: read past its attributes and draw nothing.
              std::pair<std::string_view, std::string_view> attr;
              do {
                attr = parseAttribute();
              } while (!atEnd() && !attr.first.empty());
              return false;
            }
          } else if (c != '/') {
            ++tagLength;
          }
          ++m_i;
        }
        return false;
      }

      bool parseCloseTag() {
        skipSpace();
        const std::size_t tagStart = m_i;
        std::size_t tagLength = 0;
        while (!atEnd()) {
          const char c = m_in[m_i];
          if (c == '>') {
            if (tagLength == 0) return false;
            const std::string_view tag = m_in.substr(tagStart, tagLength);
            const char c0 = g_ascii_tolower(tag[0]);
            m_hasNewLine = false;
            if (c0 == 'b') {
              return tagLength == 1;
            }
            if (c0 == 'i' || c0 == 'a') {
              return tagLength == 1;
            }
            if (c0 == 'p') {
              if (tagLength == 1) {
                emit(kLineSeparator);
                m_hasNewLine = true;
                m_hasSpace = true;
                return false;
              }
              if (tag == "pre") {
                m_preFormat = false;
                newLineUnlessFresh();
                m_hasNewLine = true;
                m_hasSpace = true;
                return true;
              }
              return false;
            }
            if (c0 == 'u') {
              if (tagLength == 1) return true;
              if (tag == "ul") closeList();
              return false;
            }
            if (c0 == 's') {
              return tagLength == 1 || tag == "strong";
            }
            if (c0 == 'h' && tagLength == 2) {
              emit(kLineSeparator);
              m_hasNewLine = true;
              m_hasSpace = true;
              return true;
            }
            if (tag == "font" || tag == "del") return true;
            if (tag == "ol") closeList();
            return false;
          }
          if (!isSpace(c)) ++tagLength;
          ++m_i;
        }
        return false;
      }

      void closeList() {
        if (m_lists.empty()) return;
        m_lists.pop_back();
        if (m_lists.empty()) emit(kLineSeparator);
      }

      void parseEntity() {
        const std::size_t start = m_i;
        std::size_t length = 0;
        while (!atEnd()) {
          const char c = m_in[m_i];
          if (c == ';') {
            if (auto text = decodeEntity(m_in.substr(start, length))) emit(*text);
            return;
          }
          if (c == ' ') {
            // Not an entity after all: emit "&" and what followed as they were.
            emit(m_in.substr(start - 1, length + 1));
            emit(" ");
            return;
          }
          ++length;
          ++m_i;
        }
      }

      std::pair<std::string_view, std::string_view> parseAttribute() {
        skipSpace();
        const std::size_t attrStart = m_i;
        std::size_t attrLength = 0;
        while (!atEnd()) {
          const char c = m_in[m_i];
          if (c == '>') break;
          if (c == '=') {
            ++m_i;
            if (ch() != '\'' && ch() != '"') {
              while (!atEnd() && m_in[m_i] != '>') ++m_i;
              break;
            }
            ++m_i;
            if (attrLength == 0) break;
            const std::string_view attr = m_in.substr(attrStart, attrLength);
            const std::size_t valStart = m_i;
            std::size_t valLength = 0;
            while (!atEnd() && m_in[m_i] != '\'' && m_in[m_i] != '"') {
              ++valLength;
              ++m_i;
            }
            if (atEnd()) break;
            ++m_i; // the closing quote
            if (valLength != 0) return {attr, m_in.substr(valStart, valLength)};
            break;
          }
          ++attrLength;
          ++m_i;
        }
        return {};
      }

      bool parseFontAttributes(Format& format) {
        bool valid = false;
        std::pair<std::string_view, std::string_view> attr;
        do {
          attr = parseAttribute();
          if (attr.first == "color") {
            valid = true;
            setColor(attr.second, format);
          } else if (attr.first == "size") {
            valid = true;
            const std::string value(attr.second);
            int size = static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
            if (value[0] == '-' || value[0] == '+') size += 3;
            if (size >= 1 && size <= 7) setFontSize(size, format);
          }
        } while (!atEnd() && !attr.first.empty());
        return valid;
      }

      static void setColor(std::string_view value, Format& format) {
        // #rgb, #rrggbb, #aarrggbb, #rrrgggbbb, #rrrrggggbbbb or an SVG name.
        if (auto svg = svgOverride(value)) {
          format.color = std::string(*svg);
          format.colorAlpha = -1;
          return;
        }
        std::string v(value);
        if (v.size() == 9 && v[0] == '#') {
          const int a = g_ascii_xdigit_value(v[1]) * 16 + g_ascii_xdigit_value(v[2]);
          if (a >= 0) {
            format.color = "#" + v.substr(3);
            format.colorAlpha = static_cast<int>(std::lround(a * 100.0 / 255.0));
            return;
          }
        }
        PangoColor parsed;
        if (pango_color_parse(&parsed, v.c_str()) == 0) return; // invalid colour: keep the text's
        char hex[8];
        std::snprintf(hex, sizeof(hex), "#%02x%02x%02x", parsed.red >> 8, parsed.green >> 8, parsed.blue >> 8);
        format.color = hex;
        format.colorAlpha = -1;
      }

      bool parseAnchorAttributes(Format& format) {
        bool valid = false;
        std::pair<std::string_view, std::string_view> attr;
        do {
          attr = parseAttribute();
          if (g_ascii_strcasecmp(std::string(attr.first).c_str(), "href") == 0) {
            // Links are drawn underlined in blue.
            format.underline = true;
            format.color = "#0000ff";
            format.colorAlpha = -1;
            valid = true;
          }
        } while (!atEnd() && !attr.first.empty());
        return valid;
      }

      void parseListAttributes(bool ordered) {
        List list{0, ordered ? ListFormat::Decimal : ListFormat::Bullet};
        std::pair<std::string_view, std::string_view> attr;
        do {
          attr = parseAttribute();
          if (attr.first != "type") continue;
          if (ordered) {
            if (attr.second == "a") list.format = ListFormat::LowerAlpha;
            else if (attr.second == "A") list.format = ListFormat::UpperAlpha;
            else if (attr.second == "i") list.format = ListFormat::LowerRoman;
            else if (attr.second == "I") list.format = ListFormat::UpperRoman;
          } else {
            if (attr.second == "disc") list.format = ListFormat::Disc;
            else if (attr.second == "square") list.format = ListFormat::Square;
          }
        } while (!atEnd() && !attr.first.empty());
        m_lists.push_back(list);
      }

      std::string_view m_in;
      std::size_t m_i = 0;
      std::vector<Format> m_stack;
      std::vector<List> m_lists;
      std::vector<Run> m_runs;
      bool m_hasSpace = true;
      bool m_hasNewLine = true;
      bool m_prependSpace = false;
      bool m_preFormat = false;
    };

    std::string escape(std::string_view s) {
      gchar* e = g_markup_escape_text(s.data(), static_cast<gssize>(s.size()));
      std::string out = e != nullptr ? e : "";
      g_free(e);
      return out;
    }

  } // namespace

  StyledMarkup styledTextToPango(
      std::string_view styled, float baseAlpha, const std::function<long(float, bool)>& lineHeight
  ) {
    StyledMarkup out;
    if (!g_utf8_validate(styled.data(), static_cast<gssize>(styled.size()), nullptr)) return out;
    const std::vector<Run> runs = Parser(styled).parse();
    for (const Run& r : runs) out.colored = out.colored || !r.format.color.empty();
    const int base = static_cast<int>(std::lround(std::clamp(baseAlpha, 0.0F, 1.0F) * 100.0F));

    StyledParagraph paragraph;
    std::string plain; // the paragraph without its styling, should Pango refuse the markup
    const auto finish = [&]() {
      // Never hand Pango something it can't read.
      if (!paragraph.markup.empty() && !pango_parse_markup(paragraph.markup.c_str(), -1, 0, nullptr, nullptr, nullptr, nullptr)) {
        paragraph.markup = escape(plain);
        if (lineHeight) {
          paragraph.markup = "<span line_height=\"" + std::to_string(lineHeight(1.0F, false)) + "\">" + paragraph.markup + "</span>";
        }
        paragraph.scaled = false;
      }
      out.paragraphs.push_back(std::move(paragraph));
      paragraph = {};
      plain.clear();
    };
    for (const Run& r : runs) {
      const Format& f = r.format;
      std::string attrs;
      if (f.bold) attrs += " weight=\"bold\"";
      if (f.italic) attrs += " style=\"italic\"";
      if (f.underline) attrs += " underline=\"single\"";
      if (f.strike) attrs += " strikethrough=\"true\"";
      if (f.mono) attrs += " font_family=\"Courier New,courier,monospace\"";
      if (f.scale > 0.0F) attrs += " size=\"" + std::to_string(std::lround(f.scale * 100.0F)) + "%\"";
      if (lineHeight) {
        attrs += " line_height=\"" + std::to_string(lineHeight(f.scale > 0.0F ? f.scale : 1.0F, f.bold)) + "\"";
      }
      if (!f.color.empty()) {
        attrs += " foreground=\"" + f.color + "\"";
        if (f.colorAlpha >= 0) attrs += " alpha=\"" + std::to_string(std::max(1, f.colorAlpha)) + "%\"";
      } else if (out.colored && base < 100) {
        attrs += " alpha=\"" + std::to_string(std::max(1, base)) + "%\"";
      }
      // One paragraph per line break; the card's line limit counts over all of them, Pango's per paragraph.
      std::string_view text = r.text;
      while (true) {
        const std::size_t nl = text.find('\n');
        const std::string_view piece = text.substr(0, nl);
        if (!piece.empty()) {
          const std::string escaped = escape(piece);
          paragraph.markup += attrs.empty() ? escaped : "<span" + attrs + ">" + escaped + "</span>";
          paragraph.scaled = paragraph.scaled || f.scale > 0.0F;
          plain += piece;
        }
        if (nl == std::string_view::npos) break;
        finish();
        text = text.substr(nl + 1);
      }
    }
    if (!paragraph.markup.empty()) finish();
    return out;
  }

  std::string styledFirstLine(std::string_view styled) {
    std::string stripped;
    bool inTag = false;
    for (std::size_t i = 0; i < styled.size(); ++i) {
      const char c = styled[i];
      if (inTag) {
        inTag = c != '>';
      } else if (c == '<' && styled.find('>', i + 1) != std::string_view::npos) {
        inTag = true;
      } else if (c == '\n') {
        break;
      } else {
        stripped += c;
      }
    }
    // Decode entities, like the rest of the body.
    std::string out;
    for (std::size_t i = 0; i < stripped.size(); ++i) {
      if (stripped[i] == '&') {
        const std::size_t semi = stripped.find(';', i + 1);
        if (semi != std::string::npos && semi - i <= 10) {
          if (auto text = decodeEntity(std::string_view(stripped).substr(i + 1, semi - i - 1))) {
            out += *text;
            i = semi;
            continue;
          }
        }
      }
      out += stripped[i];
    }
    return out;
  }

} // namespace kusanagi
