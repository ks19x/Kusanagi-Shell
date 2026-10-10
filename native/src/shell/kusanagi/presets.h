#pragma once

// Presets: whole looks in one click. A preset sets the look-related settings (bar, workspaces, motion,
// surfaces, OSD/notification/launcher style, sometimes palette and font) and leaves everything else alone.
// Saved and kept imported looks live in ~/.config/kusanagi/presets.json as { "saved": [...] }.
// settings.json is read from and written to disk each time, so this works without a running shell.
//
// A preset is { id, name, note, look, bar, workspaces, panel, osd, notifications, launcher, lock, power,
// bars | barsTemplate }, where barsTemplate names a bar_templates id.

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace kusanagi::presets {

  [[nodiscard]] const nlohmann::json& builtin();
  [[nodiscard]] nlohmann::json saved();
  // Built-in presets first, then saved ones.
  [[nodiscard]] nlohmann::json all();
  // By id or name, case-insensitive. Null when there's none.
  [[nodiscard]] nlohmann::json find(std::string_view idOrName);
  // Which keys of which sections make up a look.
  [[nodiscard]] const nlohmann::ordered_json& lookKeys();
  // What applying an older look resets, for sections it doesn't mention.
  [[nodiscard]] const nlohmann::json& styleDefaults();

  [[nodiscard]] nlohmann::json barsOf(const nlohmann::json& preset);
  // Writes the preset's sections and its bar layout to settings.json in one write. Every preset decides the
  // bar layout, so a classic one brings the classic bar back.
  bool apply(const nlohmann::json& preset);
  bool applyNamed(std::string_view name);
  // Applies the preset after the last one applied (wrapping) and returns its id or name.
  std::string next();
  // Id or name of the last preset applied or saved in this process.
  [[nodiscard]] const std::string& lastApplied();

  // The current look as a preset.
  [[nodiscard]] nlohmann::json snapshot(const std::string& name);
  bool save(std::string name);
  bool remove(const std::string& name);
  // Adds an imported look to presets.json, replacing one with the same name.
  bool keep(const nlohmann::json& look);

  // Looks are shared as ~/kusanagi-looks/<slug>.kusanagi files or as pasted text.
  [[nodiscard]] std::string looksDir();
  [[nodiscard]] std::string slug(std::string_view name);
  [[nodiscard]] nlohmann::ordered_json lookDoc(const std::string& name);
  // Writes the look to a file, or to the clipboard. Returns the path, "clipboard", or "" on failure.
  std::string exportLook(std::string name, bool toClipboard);

  struct ReadResult {
    nlohmann::json look;               // null on error
    std::vector<std::string> commands; // what it would run
    std::string error;                 // "" when it's a look
  };
  [[nodiscard]] ReadResult readLook(std::string_view text);
  [[nodiscard]] ReadResult readLookFile(std::string_view path);
  // What a look would run on this machine: custom modules' commands and actions that aren't built in.
  // A look with commands is never applied without review.
  [[nodiscard]] std::vector<std::string> commandsIn(const nlohmann::json& look);
  [[nodiscard]] nlohmann::json withoutCommands(const nlohmann::json& look);

  // The `preset` IPC target: apply <name> | next | list | export <name> | import <file>.
  std::string command(std::string_view args);
  // `kusanagi-shell preset <args>`, no running shell needed. Prints command()'s answer.
  int runCli(int argc, char* argv[]);

} // namespace kusanagi::presets
