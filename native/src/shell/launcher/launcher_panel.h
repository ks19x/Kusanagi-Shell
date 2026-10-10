#pragma once

// The Kusanagi launcher: a surface covering the output with a dimmed backdrop and a card (launcher.style
// card, spotlight, fullscreen or side) holding the search field and the results as a list or grid.
// Kusanagi's own search comes from KusanagiProvider; the other providers stay reachable as //word.

#include "launcher/launcher_provider.h"
#include "launcher/usage_tracker.h"
#include "render/animation/animation_manager.h"
#include "shell/kusanagi/slide_highlight.h"
#include "shell/panel/panel.h"
#include "system/icon_resolver.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Box;
class InputArea;
class Input;
class KusanagiProvider;
class Label;
class LauncherResultAdapter;
class Renderer;
class ScrollView;
class VirtualGridView;
class ConfigService;
class AsyncTextureCache;

class LauncherPanel : public Panel {
public:
  LauncherPanel(ConfigService* config, AsyncTextureCache* asyncTextures);
  ~LauncherPanel() override;

  void addProvider(std::unique_ptr<LauncherProvider> provider);
  // Drop every dynamically-registered (plugin-backed) provider, so the enabled
  // plugin set can be re-applied without disturbing the built-in providers.
  void clearDynamicProviders();
  // Drop providers whose stable id starts with `prefix` (e.g. config-driven "dmenu.").
  void clearProvidersWithIdPrefix(std::string_view prefix);
  // Restrict the next open to a single provider (stdin/dmenu session). When set,
  // onInputChanged queries only that provider and skips prefix routing/overview.
  // Cleared on close.
  void setScopedProvider(std::string_view providerId, std::string_view placeholder = {});

  void create() override;
  void onOpen(std::string_view context) override;
  void onClose() override;
  void onIconThemeChanged() override;

  // IPC `launcher-actions`: lists the selected app's desktop actions, when it has any.
  bool actionsOfSelected();
  void clearUsage();
  void syncUsageTrackingState();

  // Invoked after a terminal close when the activation copied text and the provider
  // supports auto-paste. The host schedules virtual-keyboard paste (clipboard path).
  void setCopiedActivationCallback(std::function<void()> callback) { m_onCopiedActivation = std::move(callback); }

  [[nodiscard]] float preferredWidth() const override { return scaled(640.0F); }
  [[nodiscard]] float preferredHeight() const override { return scaled(500.0F); }
  [[nodiscard]] bool coversOutput() const noexcept override { return true; }
  [[nodiscard]] bool hasDecoration() const override { return false; }
  [[nodiscard]] bool dismissOnOutsideClick() const override { return false; }
  [[nodiscard]] PanelPlacement panelPlacement() const noexcept override { return PanelPlacement::Floating; }
  bool beginCloseAnimation(std::function<void()> done) override;
  [[nodiscard]] LayerShellKeyboard keyboardMode() const override { return LayerShellKeyboard::Exclusive; }
  [[nodiscard]] InputArea* initialFocusArea() const override;
  [[nodiscard]] bool handleGlobalKey(std::uint32_t sym, std::uint32_t modifiers, bool pressed, bool preedit) override;

private:
  void doLayout(Renderer& renderer, float width, float height) override;
  void onInputChanged(const std::string& text);
  void setQuery(std::string query);
  // Re-gather the current query, preserving the selected result by identity.
  void reapplyCurrentQuery();
  // A plugin provider delivered fresh async results — re-gather if the panel is open.
  void onProviderResultsChanged();
  void refreshResults();
  void select(std::size_t index);
  void activateAt(std::size_t index, bool alternate = false);
  void activateSelected(bool alternate = false);
  bool handleKeyEvent(std::uint32_t sym, std::uint32_t modifiers);
  void bindDetailResult();
  [[nodiscard]] bool shouldUseDetailPresentation() const;
  [[nodiscard]] bool startsWithLauncherPrefix(std::string_view text) const;
  void applyProviderConfig(LauncherProvider& provider) const;
  void finishActivation(LauncherProvider& provider, const LauncherResult& result, bool copied);
  [[nodiscard]] std::vector<LauncherResult> providerOverviewResults(std::string_view text) const;
  [[nodiscard]] std::vector<LauncherResult> engineResults(const std::string& text, bool& loading);
  [[nodiscard]] bool openActions(std::size_t index);
  void closeActions();
  void syncField();
  void requestClose();
  [[nodiscard]] bool shouldTrackUsage() const;
  [[nodiscard]] bool gridLayout() const;

  // Geometry for the current style, settings and surface size.
  struct Look {
    bool full = false;
    bool side = false;
    bool spot = false;
    bool grid = false;
    bool centered = false;
    float iconPx = 32.0F;
    float rowH = 54.0F;
    float cellW = 108.0F;
    float cellH = 90.0F;
    int rows = 7;
    int gridRows = 2;
    std::size_t cols = 1;
    float cardW = 640.0F;
    float radius = 16.0F;
    float panelOpacity = 0.95F;
  };
  [[nodiscard]] Look computeLook(float width, float height) const;
  [[nodiscard]] float cardTargetHeight(const Look& look, float height) const;
  void placeCard();
  void startOpenAnimation();
  // Moves the highlight onto the selected result, sliding when `animate` is set.
  void syncHighlight(bool animate);

  std::vector<std::unique_ptr<LauncherProvider>> m_providers;
  KusanagiProvider* m_kusanagi = nullptr;
  std::vector<LauncherResult> m_results;
  UsageTracker m_usageTracker;
  IconResolver m_iconResolver;

  // Scene nodes, owned by the panel root while open.
  Node* m_rootNode = nullptr;
  Box* m_backdrop = nullptr;
  Node* m_cardGroup = nullptr; // shadow and card, moved together by the open animation
  Box* m_shadow = nullptr;
  Box* m_card = nullptr;
  InputArea* m_cardArea = nullptr;
  Node* m_field = nullptr;
  Box* m_fieldPill = nullptr;
  Label* m_fieldIcon = nullptr;
  Input* m_input = nullptr;
  Label* m_placeholder = nullptr;
  Box* m_separator = nullptr;
  VirtualGridView* m_grid = nullptr;
  ScrollView* m_detailScroll = nullptr;
  Label* m_detailSubtitle = nullptr;
  Label* m_detailBody = nullptr;
  std::unique_ptr<LauncherResultAdapter> m_adapter;

  std::string m_query;
  std::string m_scopedProviderId;
  std::string m_scopedPlaceholder;
  // While set, the app's desktop actions are listed instead of the search results.
  std::string m_actionsOf;
  std::string m_actionsOfName;
  std::size_t m_selectedIndex = 0;
  std::uint64_t m_desktopEntriesVersion = 0;

  // Geometry and motion.
  Look m_look;
  float m_surfaceW = 0.0F;
  float m_surfaceH = 0.0F;
  float m_cardH = 0.0F;       // animated
  float m_cardTargetH = -1.0F;
  float m_openProgress = 0.0F; // runs to 1 on open and back to 0 on close
  bool m_closing = false;
  AnimationManager::Id m_heightAnim = 0;
  kusanagi::SlideHighlight m_highlight;
  float m_gridW = 0.0F;       // the results view's width as last laid out
  std::string m_highlightGeometry; // what the highlight was last placed for (cells, width, style)

  ConfigService* m_config = nullptr;
  AsyncTextureCache* m_asyncTextures = nullptr;
  std::function<void()> m_onCopiedActivation;
};
