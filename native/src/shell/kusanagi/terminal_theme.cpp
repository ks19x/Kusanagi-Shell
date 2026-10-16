#include "shell/kusanagi/terminal_theme.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <regex>

namespace kusanagi::terminal {

  namespace {

    constexpr std::string_view kBegin = "# >>> kusanagi terminal colours (Kusanagi Settings > Appearance) >>>";
    constexpr std::string_view kEnd = "# <<< kusanagi terminal colours <<<";

    // ---- colour maths (OKLab / OKLCH) ----

    double toLinear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
    double toSrgb(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055; }

    struct Lch {
      double l = 0.0;
      double c = 0.0;
      double h = 0.0; // degrees
    };

    Lch oklch(const Rgb& rgb) {
      const double r = toLinear(rgb.r);
      const double g = toLinear(rgb.g);
      const double b = toLinear(rgb.b);
      const double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
      const double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
      const double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
      const double L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s;
      const double A = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
      const double B = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
      double h = std::atan2(B, A) * 180.0 / std::numbers::pi;
      if (h < 0.0) h += 360.0;
      return {L, std::hypot(A, B), h};
    }

    // OKLCH -> "rrggbb", pulling the chroma in until the colour fits sRGB.
    std::string hex(double L, double C, double H) {
      double r = 0.0;
      double g = 0.0;
      double b = 0.0;
      while (true) {
        const double rad = H * std::numbers::pi / 180.0;
        const double A = C * std::cos(rad);
        const double B = C * std::sin(rad);
        const double l = std::pow(L + 0.3963377774 * A + 0.2158037573 * B, 3.0);
        const double m = std::pow(L - 0.1055613458 * A - 0.0638541728 * B, 3.0);
        const double s = std::pow(L - 0.0894841775 * A - 1.2914855480 * B, 3.0);
        r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
        g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
        b = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
        const auto fits = [](double c) { return c >= -1e-4 && c <= 1.0 + 1e-4; };
        if ((fits(r) && fits(g) && fits(b)) || C < 0.001) break;
        C *= 0.95;
      }
      const auto byte = [](double c) {
        return static_cast<int>(std::lround(std::clamp(toSrgb(std::max(0.0, c)), 0.0, 1.0) * 255.0));
      };
      return std::format("{:02x}{:02x}{:02x}", byte(r), byte(g), byte(b));
    }

    std::string hex(const Rgb& c) {
      const auto byte = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
      return std::format("{:02x}{:02x}{:02x}", byte(c.r), byte(c.g), byte(c.b));
    }

    // ---- config text ----

    std::string block(std::string_view body) { return std::format("{}\n{}{}\n", kBegin, body, kEnd); }

    // `text` without the marked block, and without the blank line hook() put in front of it.
    std::string withoutBlock(std::string_view text) {
      std::string out(text);
      for (;;) {
        const std::size_t begin = out.find(kBegin);
        if (begin == std::string::npos) break;
        const std::size_t end = out.find(kEnd, begin);
        std::size_t stop = end == std::string::npos ? out.size() : end + kEnd.size();
        if (stop < out.size() && out[stop] == '\n') ++stop;
        std::size_t start = begin;
        if (start >= 2 && out[start - 1] == '\n' && out[start - 2] == '\n') --start;
        out.erase(start, stop - start);
      }
      return out;
    }

    std::string appendBlock(std::string_view config, std::string_view body) {
      std::string out = withoutBlock(config);
      if (!out.empty() && out.back() != '\n') out += '\n';
      if (!out.empty()) out += '\n';
      return out + block(body);
    }

    std::string quoted(std::string_view path) { return std::format("\"{}\"", path); }

  } // namespace

  Scheme makeScheme(const Rgb& background, const Rgb& foreground, const Rgb& accent) {
    const Lch bg = oklch(background);
    const Lch a = oklch(accent);
    // A grey accent has no hue worth leaning toward.
    const double pull = 0.15 * std::clamp(a.c / 0.04, 0.0, 1.0);
    const auto tone = [&](double hue, double L, double C) {
      const double d = std::fmod(a.h - hue + 540.0, 360.0) - 180.0;
      return hex(L, C, std::fmod(hue + d * pull + 360.0, 360.0));
    };
    static constexpr std::array<double, 6> kHues{25, 145, 85, 250, 330, 200}; // red green yellow blue magenta cyan

    Scheme s;
    s.light = bg.l > 0.6;
    s.background = hex(background);
    s.foreground = hex(foreground);
    s.cursor = hex(accent);
    s.cursorText = s.background;
    s.selectionBackground = hex(accent);
    s.selectionForeground = s.background;

    const double tint = std::min(a.c * 0.25, 0.025);
    if (s.light) {
      s.ansi[0] = hex(0.45, tint + 0.01, a.h);
      s.ansi[7] = hex(0.76, 0.015, a.h);
      s.ansi[8] = hex(0.56, tint + 0.005, a.h);
      s.ansi[15] = hex(0.82, 0.012, a.h);
      for (std::size_t i = 0; i < kHues.size(); ++i) {
        s.ansi[i + 1] = tone(kHues[i], 0.55, 0.15);
        s.ansi[i + 9] = tone(kHues[i], 0.50, 0.16);
      }
    } else {
      s.ansi[0] = hex(0.23, tint, a.h);
      s.ansi[7] = hex(0.85, 0.015, a.h);
      // Bright black is what shells use for hints and autosuggestions, so it stays readable on the background.
      s.ansi[8] = hex(0.46, std::min(a.c * 0.3, 0.03), a.h);
      s.ansi[15] = s.foreground;
      for (std::size_t i = 0; i < kHues.size(); ++i) {
        s.ansi[i + 1] = tone(kHues[i], 0.72, 0.12);
        s.ansi[i + 9] = tone(kHues[i], 0.82, 0.10);
      }
    }
    return s;
  }

  std::string footTheme(const Scheme& s) {
    // Colours only: alpha and blur stay in foot.ini. Both sections, so it works in either of foot's themes.
    std::string out = "# Written by Kusanagi (Settings > Appearance > Terminal colours). Changes are overwritten.\n";
    for (const char* section : {"colors-dark", "colors-light"}) {
      out += std::format("[{}]\nforeground={}\nbackground={}\n", section, s.foreground, s.background);
      for (int i = 0; i < 8; ++i) out += std::format("regular{}={}\n", i, s.ansi[static_cast<std::size_t>(i)]);
      for (int i = 0; i < 8; ++i) out += std::format("bright{}={}\n", i, s.ansi[static_cast<std::size_t>(i + 8)]);
      out += std::format("selection-foreground={}\nselection-background={}\ncursor={} {}\n", s.selectionForeground,
                         s.selectionBackground, s.cursorText, s.cursor);
    }
    return out;
  }

  std::string kittyTheme(const Scheme& s) {
    std::string out = "# Written by Kusanagi (Settings > Appearance > Terminal colours). Changes are overwritten.\n";
    out += std::format("foreground #{}\nbackground #{}\ncursor #{}\ncursor_text_color #{}\n"
                       "selection_foreground #{}\nselection_background #{}\nurl_color #{}\n",
                       s.foreground, s.background, s.cursor, s.cursorText, s.selectionForeground,
                       s.selectionBackground, s.cursor);
    for (std::size_t i = 0; i < s.ansi.size(); ++i) out += std::format("color{} #{}\n", i, s.ansi[i]);
    return out;
  }

  std::string alacrittyTheme(const Scheme& s) {
    static constexpr std::array<const char*, 8> kNames{"black", "red", "green", "yellow",
                                                       "blue",  "magenta", "cyan", "white"};
    std::string out = "# Written by Kusanagi (Settings > Appearance > Terminal colours). Changes are overwritten.\n";
    out += std::format("[colors.primary]\nbackground = \"#{}\"\nforeground = \"#{}\"\n\n"
                       "[colors.cursor]\ncursor = \"#{}\"\ntext = \"#{}\"\n\n"
                       "[colors.selection]\nbackground = \"#{}\"\ntext = \"#{}\"\n",
                       s.background, s.foreground, s.cursor, s.cursorText, s.selectionBackground,
                       s.selectionForeground);
    for (const auto& [table, offset] : {std::pair{"normal", 0}, std::pair{"bright", 8}}) {
      out += std::format("\n[colors.{}]\n", table);
      for (std::size_t i = 0; i < kNames.size(); ++i) {
        out += std::format("{} = \"#{}\"\n", kNames[i], s.ansi[i + static_cast<std::size_t>(offset)]);
      }
    }
    return out;
  }

  std::string ghosttyTheme(const Scheme& s) {
    std::string out = "# Written by Kusanagi (Settings > Appearance > Terminal colours). Changes are overwritten.\n";
    out += std::format("background = #{}\nforeground = #{}\ncursor-color = #{}\ncursor-text = #{}\n"
                       "selection-background = #{}\nselection-foreground = #{}\n",
                       s.background, s.foreground, s.cursor, s.cursorText, s.selectionBackground,
                       s.selectionForeground);
    for (std::size_t i = 0; i < s.ansi.size(); ++i) out += std::format("palette = {}=#{}\n", i, s.ansi[i]);
    return out;
  }

  std::string weztermTheme(const Scheme& s) {
    // A colour scheme file for ~/.config/wezterm/colors; pick it with config.color_scheme = "Kusanagi".
    const auto list = [&s](std::size_t from) {
      std::string items;
      for (std::size_t i = from; i < from + 8; ++i) items += std::format("{}\"#{}\"", i == from ? "" : ", ", s.ansi[i]);
      return items;
    };
    return std::format("# Written by Kusanagi (Settings > Appearance > Terminal colours). Changes are overwritten.\n"
                       "[colors]\nforeground = \"#{}\"\nbackground = \"#{}\"\ncursor_bg = \"#{}\"\n"
                       "cursor_border = \"#{}\"\ncursor_fg = \"#{}\"\nselection_bg = \"#{}\"\nselection_fg = \"#{}\"\n"
                       "ansi = [{}]\nbrights = [{}]\n\n[metadata]\nname = \"Kusanagi\"\n",
                       s.foreground, s.background, s.cursor, s.cursor, s.cursorText, s.selectionBackground,
                       s.selectionForeground, list(0), list(8));
  }

  std::string liveSequence(const Scheme& s) {
    std::string out;
    for (std::size_t i = 0; i < s.ansi.size(); ++i) out += std::format("\033]4;{};#{}\007", i, s.ansi[i]);
    out += std::format("\033]10;#{}\007\033]11;#{}\007\033]12;#{}\007\033]17;#{}\007\033]19;#{}\007", s.foreground,
                       s.background, s.cursor, s.selectionBackground, s.selectionForeground);
    return out;
  }

  std::string resetSequence() { return "\033]104\007\033]110\007\033]111\007\033]112\007\033]117\007\033]119\007"; }

  std::string hook(Kind kind, std::string_view config, std::string_view themePath) {
    switch (kind) {
    case Kind::Foot:
      // include is a [main] option; a second [main] at the end makes it load after every colour set above it
      return appendBlock(config, std::format("[main]\ninclude={}\n", themePath));
    case Kind::Kitty:
      return appendBlock(config, std::format("include {}\n", themePath));
    case Kind::Ghostty:
      return appendBlock(config, std::format("config-file = {}\n", themePath));
    case Kind::Alacritty:
      break;
    }

    // Alacritty: general.import, which has to come before the first table.
    std::string text = unhook(kind, config, themePath);
    static const std::regex importList(R"((^|\n)(\s*import\s*=\s*\[))");
    std::smatch m;
    if (std::regex_search(text, m, importList)) {
      // Already importing something: add ours to the front of the list.
      const auto at = static_cast<std::size_t>(m.position(0) + m.length(0));
      return text.insert(at, quoted(themePath) + ", ");
    }
    static const std::regex general(R"((^|\n)\[general\][^\n]*\n)");
    if (std::regex_search(text, m, general)) {
      const auto at = static_cast<std::size_t>(m.position(0) + m.length(0));
      return text.insert(at, block(std::format("import = [{}]\n", quoted(themePath))));
    }
    // No [general] table: a dotted key at the very top, before any table.
    return block(std::format("general.import = [{}]\n", quoted(themePath))) + (text.empty() ? "" : "\n") + text;
  }

  std::string unhook(Kind kind, std::string_view config, std::string_view themePath) {
    std::string text = withoutBlock(config);
    if (kind == Kind::Alacritty) {
      // An entry hook() put into an import list that was already there.
      for (const std::string& entry : {quoted(themePath) + ", ", ", " + quoted(themePath), quoted(themePath)}) {
        for (std::size_t at = text.find(entry); at != std::string::npos; at = text.find(entry)) {
          text.erase(at, entry.size());
        }
      }
      // withoutBlock() keeps the blank line that followed a block at the very top
      if (config.starts_with(kBegin) && text.starts_with('\n')) text.erase(0, 1);
    }
    return text;
  }

} // namespace kusanagi::terminal
