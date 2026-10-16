#pragma once

// Terminal colours that match the shell: a 16-colour palette built from the shell's background, text and accent,
// written out in each terminal's own config format, plus the edits that hook a theme file into a terminal's
// config and take it out again. Pure text in, text out; terminal_colors.cpp does the files and the live update.

#include <array>
#include <string>
#include <string_view>

namespace kusanagi::terminal {

  // An sRGB colour, each channel 0..1.
  struct Rgb {
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
  };

  // Every colour as "rrggbb" (no '#').
  struct Scheme {
    std::string foreground;
    std::string background;
    std::string cursor;
    std::string cursorText;
    std::string selectionForeground;
    std::string selectionBackground;
    std::array<std::string, 16> ansi; // 0-7 regular, 8-15 bright
    bool light = false;
  };

  // The usual terminal hues (red, green, yellow, blue, magenta, cyan), each nudged 15% toward the accent so the
  // terminal sits in the same family as the shell, at lightnesses that read well on `background`.
  [[nodiscard]] Scheme makeScheme(const Rgb& background, const Rgb& foreground, const Rgb& accent);

  [[nodiscard]] std::string footTheme(const Scheme& s);
  [[nodiscard]] std::string kittyTheme(const Scheme& s);
  [[nodiscard]] std::string alacrittyTheme(const Scheme& s);
  [[nodiscard]] std::string ghosttyTheme(const Scheme& s);
  [[nodiscard]] std::string weztermTheme(const Scheme& s);

  // OSC 4/10/11/12/17/19: recolours a terminal window that is already open. Every terminal here understands it.
  [[nodiscard]] std::string liveSequence(const Scheme& s);
  // OSC 104/110/111/112/117/119: back to the terminal's own colours.
  [[nodiscard]] std::string resetSequence();

  enum class Kind { Foot, Kitty, Alacritty, Ghostty };

  // `config` with the theme at `themePath` hooked in. Foot, kitty and Ghostty get a marked block at the end, so it
  // wins over colours set earlier in the file. Alacritty gets an import, which must sit before any table.
  // Unchanged when it is already hooked.
  [[nodiscard]] std::string hook(Kind kind, std::string_view config, std::string_view themePath);
  // `config` without anything hook() added.
  [[nodiscard]] std::string unhook(Kind kind, std::string_view config, std::string_view themePath);

} // namespace kusanagi::terminal
