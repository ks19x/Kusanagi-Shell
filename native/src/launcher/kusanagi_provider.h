#pragma once

// Kusanagi's launcher search: one provider for everything the launcher finds, routed by the query's first
// character. Plain text finds apps and Kusanagi commands; "=" is the calculator, ">" runs a shell command,
// ":" finds emoji and symbols, "/" finds files in your home and "?" searches the web.
//
// App results carry providerId "Applications" so they launch through AppProvider; everything else comes
// back here. Usage counts live in ~/.config/kusanagi/launcher-usage.json, in the format the classic shell
// also reads: {"counts": {"<desktop id>": n, "sym:<glyph>": n}}.

#include "core/timer_manager.h"
#include "launcher/launcher_provider.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

class ClipboardService;
class IpcService;
struct DesktopEntry;

class KusanagiProvider final : public LauncherProvider {
public:
  enum class Mode : std::uint8_t { Apps, Calc, Run, Emoji, Files, Web };

  KusanagiProvider(ClipboardService* clipboard, IpcService* ipc);
  ~KusanagiProvider() override;

  [[nodiscard]] std::string_view defaultPrefix() const override { return ""; }
  [[nodiscard]] bool allowCustomPrefix() const override { return false; }
  [[nodiscard]] std::string_view id() const override { return "Kusanagi"; }
  [[nodiscard]] std::string displayName() const override { return "Kusanagi"; }
  [[nodiscard]] std::string_view defaultGlyphName() const override { return "search"; }

  void setResultsChangedCallback(std::function<void()> callback) override { m_resultsChanged = std::move(callback); }
  void setQueryRequestedCallback(std::function<void(std::string)> callback) override {
    m_queryRequested = std::move(callback);
  }
  void reset() override;

  // The panel routes Kusanagi queries through search(); query() is the same with no app open.
  [[nodiscard]] std::vector<LauncherResult> query(std::string_view text) const override { return search(text, {}); }
  // actionsOf: desktop file path of the app whose actions are listed, or empty.
  [[nodiscard]] std::vector<LauncherResult> search(std::string_view text, std::string_view actionsOf) const;

  bool activate(const LauncherResult& result) override { return activate(result, false); }
  // alternate = Shift+Enter: run in a terminal, type the symbol, open the containing folder.
  bool activate(const LauncherResult& result, bool alternate);

  // Count a launch (apps, app actions, symbols) for "most used first".
  void recordUse(const LauncherResult& result);

  [[nodiscard]] static Mode modeOf(std::string_view text);
  // Nerd Font glyph for the search field: the mode's, or a back arrow while app actions are listed.
  [[nodiscard]] static std::string fieldGlyph(Mode mode, bool actions);
  // The accent hint at the right of the selected row, e.g. "copy ↵".
  [[nodiscard]] static std::string hintFor(const LauncherResult& result, bool hasActions);
  [[nodiscard]] static bool hasActions(const LauncherResult& result);

private:
  struct Symbol {
    std::string ch;
    std::string name;
    std::string keys;
  };

  [[nodiscard]] std::vector<LauncherResult> appResults(const std::string& q) const;
  [[nodiscard]] std::vector<LauncherResult> actionResults(std::string_view actionsOf, const std::string& q) const;
  [[nodiscard]] std::vector<LauncherResult> symbolResults(const std::string& q) const;
  [[nodiscard]] std::vector<LauncherResult> fileResults(const std::string& q) const;
  [[nodiscard]] LauncherResult webItem(const std::string& q) const;
  void loadSymbols() const;
  void loadUsage() const;
  void saveUsage() const;
  [[nodiscard]] int usage(const std::string& key) const;
  void startFileSearch(const std::string& q) const;
  void runCommand(const std::string& id);

  ClipboardService* m_clipboard = nullptr;
  IpcService* m_ipc = nullptr;
  std::function<void()> m_resultsChanged;
  std::function<void(std::string)> m_queryRequested;

  // Per open session (dropped by reset()): usage counts, the symbol table, the last file search.
  mutable bool m_usageLoaded = false;
  mutable std::map<std::string, int> m_usage;
  mutable std::vector<Symbol> m_symbols;
  mutable std::string m_fileQuery;     // query the file results belong to
  mutable std::string m_filePending;   // query waiting for (or running) the search
  mutable std::vector<LauncherResult> m_files;
  mutable Timer m_fileDelay;
  mutable std::uint64_t m_fileGeneration = 0;
};
