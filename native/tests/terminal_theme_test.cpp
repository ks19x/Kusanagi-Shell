#include "shell/kusanagi/terminal_theme.h"
#include "tests/test_check.h"

#include <cmath>
#include <string>

using namespace kusanagi::terminal;

namespace {

  bool isHex(const std::string& s) {
    return s.size() == 6 && s.find_first_not_of("0123456789abcdef") == std::string::npos;
  }

  double luminance(const std::string& h) {
    const auto ch = [&h](int i) {
      const double c = std::stoi(h.substr(static_cast<std::size_t>(i), 2), nullptr, 16) / 255.0;
      return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * ch(0) + 0.7152 * ch(2) + 0.0722 * ch(4);
  }

  double contrast(const std::string& a, const std::string& b) {
    const double x = luminance(a) + 0.05;
    const double y = luminance(b) + 0.05;
    return x > y ? x / y : y / x;
  }

  bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

  void checkScheme(const Scheme& s) {
    TEST_CHECK(isHex(s.foreground) && isHex(s.background) && isHex(s.cursor));
    for (const auto& c : s.ansi) TEST_CHECK(isHex(c));
    // the six hues and bright black must be readable on the background
    for (std::size_t i = 1; i <= 6; ++i) {
      TEST_CHECK(contrast(s.ansi[i], s.background) > 3.0);
      TEST_CHECK(contrast(s.ansi[i + 8], s.background) > 3.0);
    }
    TEST_CHECK(contrast(s.ansi[8], s.background) > 1.8);
  }

  void testSchemes() {
    // Kusanagi's dark wallpaper palette: bgPanel, text, accent
    const Scheme dark = makeScheme({0x15 / 255.0, 0x0a / 255.0, 0x08 / 255.0}, {0xf3 / 255.0, 0xe8 / 255.0, 0xe7 / 255.0},
                                   {0xfd / 255.0, 0xa2 / 255.0, 0x95 / 255.0});
    TEST_CHECK(!dark.light);
    TEST_CHECK(dark.background == "150a08");
    TEST_CHECK(dark.foreground == "f3e8e7");
    TEST_CHECK(dark.cursor == "fda295");
    TEST_CHECK(dark.ansi[15] == dark.foreground);
    checkScheme(dark);

    // a light palette (Latte-like)
    const Scheme light = makeScheme({0.94, 0.94, 0.96}, {0.30, 0.31, 0.41}, {0.53, 0.22, 0.94});
    TEST_CHECK(light.light);
    checkScheme(light);

    // a grey accent: no hue to lean toward, still a full palette
    const Scheme grey = makeScheme({0.08, 0.08, 0.08}, {0.9, 0.9, 0.9}, {0.6, 0.6, 0.6});
    checkScheme(grey);
    TEST_CHECK(grey.ansi[1] != grey.ansi[2]);
  }

  void testFormats() {
    const Scheme s = makeScheme({0.1, 0.1, 0.12}, {0.9, 0.9, 0.9}, {0.4, 0.6, 1.0});
    const std::string foot = footTheme(s);
    TEST_CHECK(contains(foot, "[colors-dark]") && contains(foot, "[colors-light]"));
    TEST_CHECK(contains(foot, "regular0=" + s.ansi[0]) && contains(foot, "bright7=" + s.ansi[15]));
    TEST_CHECK(!contains(foot, "alpha")); // transparency stays the user's
    TEST_CHECK(contains(kittyTheme(s), "color15 #" + s.ansi[15]));
    TEST_CHECK(contains(alacrittyTheme(s), "[colors.bright]\nblack = \"#" + s.ansi[8] + "\""));
    TEST_CHECK(contains(ghosttyTheme(s), "palette = 9=#" + s.ansi[9]));
    TEST_CHECK(contains(weztermTheme(s), "name = \"Kusanagi\""));
    const std::string live = liveSequence(s);
    TEST_CHECK(contains(live, "\033]4;0;#" + s.ansi[0] + "\007"));
    TEST_CHECK(contains(live, "\033]11;#" + s.background + "\007"));
    TEST_CHECK(contains(resetSequence(), "\033]104\007"));
  }

  void roundTrip(Kind kind, const std::string& config, const std::string& path, const std::string& expect) {
    const std::string hooked = hook(kind, config, path);
    TEST_CHECK(contains(hooked, expect));
    TEST_CHECK(hook(kind, hooked, path) == hooked); // hooking twice changes nothing
    TEST_CHECK(unhook(kind, hooked, path) == config || unhook(kind, hooked, path) == config + "\n");
    TEST_CHECK(unhook(kind, config, path) == config); // nothing to take out
  }

  void testHooks() {
    const std::string foot = "[main]\nfont=Mono:size=12\n\n[colors-dark]\nalpha=0.82\nbackground=000000\n";
    roundTrip(Kind::Foot, foot, "/t/foot.ini", "[main]\ninclude=/t/foot.ini\n");
    // the include comes after the user's own colours, so it wins
    const std::string hooked = hook(Kind::Foot, foot, "/t/foot.ini");
    TEST_CHECK(hooked.find("include=/t/foot.ini") > hooked.find("background=000000"));
    roundTrip(Kind::Foot, "", "/t/foot.ini", "include=/t/foot.ini");
    roundTrip(Kind::Foot, "[main]\nshell=zsh", "/t/foot.ini", "include=/t/foot.ini"); // no trailing newline

    roundTrip(Kind::Kitty, "font_size 12.0\nbackground_opacity 0.82\n", "/t/kitty.conf", "include /t/kitty.conf\n");
    roundTrip(Kind::Ghostty, "font-size = 12\n", "/t/ghostty", "config-file = /t/ghostty\n");

    // Alacritty: no [general] -> a dotted key before every table
    const std::string plain = "[window]\nopacity = 0.9\n";
    const std::string a1 = hook(Kind::Alacritty, plain, "/t/a.toml");
    TEST_CHECK(a1.find("general.import = [\"/t/a.toml\"]") < a1.find("[window]"));
    roundTrip(Kind::Alacritty, plain, "/t/a.toml", "general.import");
    roundTrip(Kind::Alacritty, "", "/t/a.toml", "general.import = [\"/t/a.toml\"]");
    // a [general] table -> import goes into it
    const std::string withGeneral = "[general]\nlive_config_reload = true\n\n[window]\nopacity = 0.9\n";
    const std::string a2 = hook(Kind::Alacritty, withGeneral, "/t/a.toml");
    TEST_CHECK(contains(a2, "[general]\n# >>>"));
    TEST_CHECK(!contains(a2, "general.import"));
    roundTrip(Kind::Alacritty, withGeneral, "/t/a.toml", "import = [\"/t/a.toml\"]");
    // an import list already there -> ours joins it at the front
    const std::string withImport = "[general]\nimport = [\n  \"~/themes/x.toml\",\n]\n";
    const std::string a3 = hook(Kind::Alacritty, withImport, "/t/a.toml");
    TEST_CHECK(contains(a3, "import = [\"/t/a.toml\", \n  \"~/themes/x.toml\""));
    roundTrip(Kind::Alacritty, withImport, "/t/a.toml", "\"/t/a.toml\", ");
  }

} // namespace

int main() {
  testSchemes();
  testFormats();
  testHooks();
  return 0;
}
