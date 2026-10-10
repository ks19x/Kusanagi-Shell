#include "shell/launcher/launcher_panel.h"

#include "config/config_service.h"
#include "core/deferred_call.h"
#include "core/input/key_modifiers.h"
#include "core/input/key_symbols.h"
#include "core/input/keybind_matcher.h"
#include "core/ui_phase.h"
#include "cursor-shape-v1-client-protocol.h"
#include "i18n/i18n.h"
#include "launcher/kusanagi_provider.h"
#include "render/core/async_texture_cache.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "render/scene/node.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/panel/panel_manager.h"
#include "system/desktop_entry.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/glyph.h"
#include "ui/controls/image.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/scroll_view.h"
#include "ui/controls/virtual_grid_view.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "util/fuzzy_match.h"
#include "util/string_utils.h"

#include <algorithm>
#include <format>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace {

  constexpr std::size_t kRowOverscan = 3;
  constexpr double kUsageScorePerCount = 0.1;
  constexpr double kTypedUsageScoreCap = 0.5;
  constexpr std::string_view kProviderOverviewProviderId = "__launcher_provider_overview__";
  constexpr std::string_view kProviderOverviewResultPrefix = "provider:";
  constexpr std::string_view kKusanagiProviderId = "Kusanagi";
  constexpr const char* kIconFont = "JetBrainsMono Nerd Font";
  constexpr float kFieldH = 64.0F;

  // Easing curves. The animations run linear and these shape the progress.
  float outBack(float t, float overshoot) {
    const float u = t - 1.0F;
    return 1.0F + (overshoot + 1.0F) * u * u * u + overshoot * u * u;
  }
  float outQuint(float t) { return 1.0F - std::pow(1.0F - t, 5.0F); }
  float inCubic(float t) { return t * t * t; }

  double usageBoostForScore(double score, int usageCount, bool typedQuery) {
    if (usageCount <= 0) {
      return 0.0;
    }
    const double rawBoost = static_cast<double>(usageCount) * kUsageScorePerCount;
    if (!typedQuery) {
      return rawBoost;
    }
    if (!FuzzyMatch::isMatch(score)) {
      return 0.0;
    }
    return std::min(rawBoost, kTypedUsageScoreCap);
  }

  [[nodiscard]] bool isDescendantOf(const Node* node, const Node* ancestor) {
    for (const Node* current = node; current != nullptr; current = current->parent()) {
      if (current == ancestor) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] std::string singleLinePreview(std::string_view text) {
    std::string preview;
    preview.reserve(text.size());
    bool lastWasSpace = false;
    for (const char c : text) {
      const bool whitespace = c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\v';
      if (whitespace) {
        if (!lastWasSpace) {
          preview.push_back(' ');
          lastWasSpace = true;
        }
        continue;
      }
      preview.push_back(c);
      lastWasSpace = c == ' ';
    }
    return preview;
  }

  void sortResultsByScore(std::vector<LauncherResult>& results) {
    std::ranges::stable_sort(results, std::ranges::greater{}, &LauncherResult::score);
  }

  // What a result row or grid tile needs from the panel's look.
  struct TileStyle {
    bool grid = false;
    bool actionsMode = false; // listing an app's actions, so no actions buttons
    float iconPx = 32.0F;
    float radius = 16.0F;
  };

  // One result: a list row or a grid tile.
  class KusanagiResultTile final : public Node {
  public:
    explicit KusanagiResultTile(AsyncTextureCache* asyncTextures) : m_asyncTextures(asyncTextures) {
      addChild(ui::image({.out = &m_image, .visible = false}));
      addChild(ui::label({.out = &m_glyph, .fontFamily = std::string(kIconFont), .visible = false}));
      addChild(ui::glyph({.out = &m_engineGlyph, .visible = false}));
      addChild(ui::label({.out = &m_badge, .visible = false}));
      addChild(ui::label({
          .out = &m_title,
          .fontWeight = FontWeight::Bold,
          .maxLines = 1,
          .ellipsize = TextEllipsize::End,
      }));
      addChild(ui::label({
          .out = &m_desc,
          .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
          .maxLines = 1,
          .ellipsize = TextEllipsize::End,
      }));
      addChild(ui::label({.out = &m_hint, .color = colorSpecFromRole(ColorRole::Primary)}));
      addChild(ui::box({.out = &m_actionsBg, .visible = false}));
      addChild(ui::label({
          .out = &m_actionsGlyph,
          .fontFamily = std::string(kIconFont),
          .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
          .visible = false,
      }));
      m_image->setAsyncReadyCallback([this]() {
        if (!m_iconPath.empty() && m_image->hasImage()) {
          m_image->setVisible(true);
          m_glyph->setVisible(false);
        }
      });
    }

    // The actions button's box in tile coordinates.
    static void actionsButtonRect(
        const TileStyle& style, bool selected, float w, float h, float& x, float& y, float& size
    ) {
      if (style.grid) {
        size = 22.0F;
        x = w - 4.0F - size;
        y = 4.0F;
      } else {
        size = 26.0F;
        x = w - (selected ? 104.0F : 8.0F) - size;
        y = std::round((h - size) * 0.5F);
      }
    }

    void bind(
        Renderer& renderer, const LauncherResult& r, const TileStyle& style, std::size_t index, bool selected,
        bool hovered, bool showActions
    ) {
      const float w = width();
      const float h = height();
      // The same result changing state animates; a recycled tile just shows its new state.
      const bool sameItem = m_boundIndex == index && m_boundGrid == style.grid;
      m_boundIndex = index;
      m_boundGrid = style.grid;
      m_iconPath = r.iconPath;
      m_iconPx = style.iconPx;

      // The selection highlight belongs to the view (see LauncherPanel::syncHighlight) so it can slide.

      // Icon: the app icon, else a Nerd Font glyph in the accent colour, else the badge text.
      const float iconPx = style.iconPx;
      float iconX = 12.0F;
      float iconY = std::round((h - iconPx) * 0.5F);
      float titleSize = style.grid ? 11.0F : (r.kind == "calc" ? 17.0F : 13.0F);
      std::optional<float> gridLabelH;
      if (style.grid) {
        m_title->setFontSize(titleSize);
        m_title->setFontWeight(FontWeight::Normal);
        m_title->setMaxLines(2);
        m_title->setTextAlign(TextAlign::Center);
        m_title->setMaxWidth(std::max(0.0F, w - 12.0F));
        m_title->setText(singleLinePreview(r.title));
        m_title->measure(renderer);
        gridLabelH = m_title->height();
        const float colH = iconPx + 8.0F + *gridLabelH;
        iconX = std::round((w - iconPx) * 0.5F);
        iconY = std::round((h - colH) * 0.5F);
      }

      const bool hasBadge = !r.badge.empty();
      const bool hasPath = !m_iconPath.empty() && !hasBadge;
      bool imageReady = false;
      if (hasPath) {
        m_image->setPosition(iconX, iconY);
        m_image->setSize(iconPx, iconPx);
        imageReady = refreshAsyncIcon(renderer);
      } else {
        m_image->clear(renderer);
        m_image->setVisible(false);
      }
      // In the grid the icon grows on the selected or hovered tile.
      setIconScale(style.grid && (selected || hovered) ? 1.08F : 1.0F, sameItem);
      m_image->setScale(m_iconScale);

      m_badge->setVisible(hasBadge);
      if (hasBadge) {
        // Matches the classic look: emoji are 0.75 (list) or 0.8 (grid) of the icon, drawn at 0.8 of that.
        m_badge->setFontSize(std::round(iconPx * (style.grid ? 0.8F : 0.75F) * 0.8F));
        m_badge->setColor(colorSpecFromRole(ColorRole::OnSurface));
        m_badge->setText(r.badge);
        m_badge->measure(renderer);
        m_badge->setPosition(
            iconX + std::round((iconPx - m_badge->width()) * 0.5F), iconY + std::round((iconPx - m_badge->height()) * 0.5F)
        );
      }

      const bool nerd = !hasBadge && !imageReady && !r.iconGlyph.empty();
      m_glyph->setVisible(nerd);
      if (!hasBadge && !r.iconGlyph.empty()) {
        m_glyph->setFontSize(style.grid ? std::round(iconPx * 0.7F) : 22.0F);
        m_glyph->setColor(colorSpecFromRole(ColorRole::Primary));
        m_glyph->setText(r.iconGlyph);
        m_glyph->measure(renderer);
        m_glyph->setPosition(
            iconX + std::round((iconPx - m_glyph->width()) * 0.5F), iconY + std::round((iconPx - m_glyph->height()) * 0.5F)
        );
        m_glyph->setScale(m_iconScale);
      }

      // The // providers (//win, //emo, ...) use a Tabler glyph.
      const bool engineGlyph = !hasBadge && !hasPath && r.iconGlyph.empty() && !r.glyphName.empty();
      m_engineGlyph->setVisible(engineGlyph);
      if (engineGlyph) {
        m_engineGlyph->setGlyph(r.glyphName);
        m_engineGlyph->setGlyphSize(22.0F);
        m_engineGlyph->setColor(colorSpecFromRole(ColorRole::Primary));
        m_engineGlyph->measure(renderer);
        m_engineGlyph->setPosition(
            iconX + std::round((iconPx - m_engineGlyph->width()) * 0.5F),
            iconY + std::round((iconPx - m_engineGlyph->height()) * 0.5F)
        );
      }

      // The actions button, for apps with desktop actions.
      const bool actionsButton = showActions && !style.actionsMode && KusanagiProvider::hasActions(r);
      float bx = 0.0F;
      float by = 0.0F;
      float bs = 0.0F;
      actionsButtonRect(style, selected, w, h, bx, by, bs);
      m_actionsBg->setVisible(actionsButton && m_actionsHovered);
      m_actionsGlyph->setVisible(actionsButton);
      if (actionsButton) {
        m_actionsBg->setPosition(bx, by);
        m_actionsBg->setSize(bs, bs);
        m_actionsBg->setRadius(bs * 0.5F);
        m_actionsBg->setFill(colorSpecFromRole(ColorRole::OnSurface, 0.1F));
        m_actionsGlyph->setFontSize(style.grid ? 14.0F : 16.0F);
        m_actionsGlyph->setColor(colorSpecFromRole(m_actionsHovered ? ColorRole::OnSurface : ColorRole::OnSurfaceVariant));
        m_actionsGlyph->setText("\U000F0142");
        m_actionsGlyph->measure(renderer);
        m_actionsGlyph->setPosition(
            bx + std::round((bs - m_actionsGlyph->width()) * 0.5F), by + std::round((bs - m_actionsGlyph->height()) * 0.5F)
        );
      }

      if (style.grid) {
        m_desc->setVisible(false);
        m_hint->setVisible(false);
        m_title->setColor(colorSpecFromRole(ColorRole::OnSurface));
        m_title->setPosition(std::round((w - m_title->width()) * 0.5F), iconY + iconPx + 8.0F);
        return;
      }

      // The hint fades in on the selected row only, but always reserves its room so the title doesn't reflow.
      m_hint->setVisible(true);
      setHintOpacity(selected ? 1.0F : 0.0F, sameItem);
      m_hint->setFontSize(11.0F);
      m_hint->setText(KusanagiProvider::hintFor(r, actionsButton));
      m_hint->measure(renderer);
      const float hintX = w - 14.0F - m_hint->width();
      m_hint->setPosition(hintX, std::round((h - m_hint->height()) * 0.5F));

      const float textX = iconX + iconPx + 14.0F;
      const float textW = std::max(0.0F, hintX - 10.0F - textX);
      m_title->setFontSize(titleSize);
      m_title->setFontWeight(FontWeight::Bold);
      m_title->setMaxLines(1);
      m_title->setTextAlign(TextAlign::Start);
      m_title->setColor(colorSpecFromRole(ColorRole::OnSurface));
      m_title->setMaxWidth(textW);
      m_title->setText(singleLinePreview(r.title));
      m_title->measure(renderer);

      const bool showDesc = kusanagi::opt<bool>("launcher", "descriptions", true) && !r.subtitle.empty();
      m_desc->setVisible(showDesc);
      float blockH = m_title->height();
      if (showDesc) {
        m_desc->setFontSize(11.0F);
        m_desc->setMaxWidth(textW);
        m_desc->setText(singleLinePreview(r.subtitle));
        m_desc->measure(renderer);
        blockH += 1.0F + m_desc->height();
      }
      const float top = std::round((h - blockH) * 0.5F);
      m_title->setPosition(textX, top);
      if (showDesc) {
        m_desc->setPosition(textX, top + m_title->height() + 1.0F);
      }
    }

    void setActionsHovered(bool hovered) {
      m_actionsHovered = hovered;
      if (m_actionsGlyph->visible()) {
        m_actionsBg->setVisible(hovered);
        m_actionsGlyph->setColor(colorSpecFromRole(hovered ? ColorRole::OnSurface : ColorRole::OnSurfaceVariant));
      }
    }

  protected:
    void doLayout(Renderer& renderer) override {
      if (!m_iconPath.empty() && m_image->visible() == false && m_glyph->visible() == false) {
        (void)refreshAsyncIcon(renderer);
      }
      Node::doLayout(renderer);
    }

  private:
    void setHintOpacity(float target, bool animate) {
      if (target == m_hintTarget && animate) return;
      m_hintTarget = target;
      AnimationManager* anims = animationManager();
      if (anims != nullptr) anims->cancel(m_hintAnim);
      m_hintAnim = 0;
      if (!animate || anims == nullptr) {
        m_hint->setOpacity(target);
        return;
      }
      m_hintAnim = anims->animate(
          m_hint->opacity(), target, 120.0F, Easing::Linear, [this](float v) { m_hint->setOpacity(v); },
          [this]() { m_hintAnim = 0; }, m_hint
      );
    }

    void setIconScale(float target, bool animate) {
      if (target == m_iconTarget && animate) return;
      m_iconTarget = target;
      AnimationManager* anims = animationManager();
      if (anims != nullptr) anims->cancel(m_scaleAnim);
      m_scaleAnim = 0;
      if (!animate || anims == nullptr) {
        m_iconScale = target;
        return;
      }
      const float from = m_iconScale;
      const float overshoot = kusanagi::bounce(2.0F);
      m_scaleAnim = anims->animate(
          0.0F, 1.0F, 180.0F, Easing::Linear,
          [this, from, target, overshoot](float t) {
            m_iconScale = from + (target - from) * outBack(t, overshoot);
            m_image->setScale(m_iconScale);
            m_glyph->setScale(m_iconScale);
          },
          [this]() { m_scaleAnim = 0; }, m_image
      );
    }

    bool refreshAsyncIcon(Renderer& renderer) {
      const int target = static_cast<int>(std::round(m_iconPx));
      bool ready = false;
      if (m_asyncTextures != nullptr) {
        ready = m_image->setSourceFileAsync(renderer, *m_asyncTextures, m_iconPath, target, true);
      } else {
        ready = m_image->setSourceFile(renderer, m_iconPath, target, true);
      }
      m_image->setSize(m_iconPx, m_iconPx);
      m_image->setVisible(ready);
      if (ready) {
        m_glyph->setVisible(false);
      }
      return ready;
    }

    AsyncTextureCache* m_asyncTextures = nullptr;
    Image* m_image = nullptr;
    Label* m_glyph = nullptr;
    Glyph* m_engineGlyph = nullptr;
    Label* m_badge = nullptr;
    Label* m_title = nullptr;
    Label* m_desc = nullptr;
    Label* m_hint = nullptr;
    Box* m_actionsBg = nullptr;
    Label* m_actionsGlyph = nullptr;
    std::string m_iconPath;
    float m_iconPx = 32.0F;
    bool m_actionsHovered = false;
    std::optional<std::size_t> m_boundIndex;
    bool m_boundGrid = false;
    float m_hintTarget = -1.0F;
    AnimationManager::Id m_hintAnim = 0;
    float m_iconScale = 1.0F;
    float m_iconTarget = -1.0F;
    AnimationManager::Id m_scaleAnim = 0;
  };

} // namespace

class LauncherResultAdapter final : public VirtualGridAdapter {
public:
  explicit LauncherResultAdapter(AsyncTextureCache* cache) : m_cache(cache) {}

  void setResults(const std::vector<LauncherResult>* results) { m_results = results; }
  void setRenderer(Renderer* renderer) { m_renderer = renderer; }
  void setStyle(TileStyle style) { m_style = style; }
  [[nodiscard]] const TileStyle& style() const { return m_style; }
  void setSelected(std::size_t index) { m_selected = index; }
  void setOnActivate(std::function<void(std::size_t)> cb) { m_onActivate = std::move(cb); }
  void setOnHover(std::function<void(std::size_t)> cb) { m_onHover = std::move(cb); }
  void setOnActions(std::function<void(std::size_t)> cb) { m_onActions = std::move(cb); }
  void resetHover() { m_lastHover.reset(); }

  [[nodiscard]] std::size_t itemCount() const override { return m_results == nullptr ? 0U : m_results->size(); }

  [[nodiscard]] std::unique_ptr<Node> createTile() override { return std::make_unique<KusanagiResultTile>(m_cache); }

  void bindTile(Node& tile, std::size_t index, bool selected, bool hovered) override {
    if (m_renderer == nullptr || m_results == nullptr || index >= m_results->size()) {
      return;
    }
    // Hovering a result selects it. Only a change of the hovered item counts, so
    // results rebinding under a resting pointer (typing) keep the keyboard's selection.
    if (hovered && m_lastHover != index) {
      m_lastHover = index;
      if (!selected && m_onHover) {
        DeferredCall::callLater([cb = m_onHover, index]() { cb(index); });
      }
    } else if (!hovered && m_lastHover == index) {
      m_lastHover.reset();
    }
    auto& row = static_cast<KusanagiResultTile&>(tile);
    row.bind(*m_renderer, (*m_results)[index], m_style, index, selected, hovered, true);
  }

  // The actions button is an overlay: a press there opens the actions instead of launching.
  [[nodiscard]] bool
  overlayHitTest(std::size_t index, float x, float y, float cellWidth, float cellHeight) const override {
    if (m_style.actionsMode || m_results == nullptr || index >= m_results->size()
        || !KusanagiProvider::hasActions((*m_results)[index])) {
      return false;
    }
    float bx = 0.0F;
    float by = 0.0F;
    float bs = 0.0F;
    KusanagiResultTile::actionsButtonRect(m_style, index == m_selected, cellWidth, cellHeight, bx, by, bs);
    return x >= bx && x < bx + bs && y >= by && y < by + bs;
  }

  void applyOverlayHover(Node& tile, bool hovered) override {
    static_cast<KusanagiResultTile&>(tile).setActionsHovered(hovered);
  }

  bool onPointerPress(std::size_t index, float x, float y, float cellWidth, float cellHeight) override {
    if (!overlayHitTest(index, x, y, cellWidth, cellHeight)) {
      return false;
    }
    m_pressed = index;
    return true;
  }

  PointerReleaseResult onPointerRelease(std::optional<std::size_t> index) override {
    if (m_pressed.has_value() && index == m_pressed && m_onActions) {
      DeferredCall::callLater([cb = m_onActions, i = *m_pressed]() { cb(i); });
    }
    m_pressed.reset();
    return {.rebind = true};
  }

  void onPointerCancel() override { m_pressed.reset(); }

  void onActivate(std::size_t index) override {
    if (m_onActivate) {
      m_onActivate(index);
    }
  }

  void onSecondaryActivate(std::size_t index, float /*anchorX*/, float /*anchorY*/) override {
    if (m_onActions) {
      m_onActions(index);
    }
  }

private:
  AsyncTextureCache* m_cache = nullptr;
  Renderer* m_renderer = nullptr;
  const std::vector<LauncherResult>* m_results = nullptr;
  TileStyle m_style;
  std::size_t m_selected = 0;
  std::optional<std::size_t> m_lastHover;
  std::optional<std::size_t> m_pressed;
  std::function<void(std::size_t)> m_onActivate;
  std::function<void(std::size_t)> m_onHover;
  std::function<void(std::size_t)> m_onActions;
};

LauncherPanel::LauncherPanel(ConfigService* config, AsyncTextureCache* asyncTextures)
    : m_iconResolver(true), m_config(config), m_asyncTextures(asyncTextures) {}

LauncherPanel::~LauncherPanel() = default;

void LauncherPanel::applyProviderConfig(LauncherProvider& provider) const {
  std::string triggerWord = std::string(provider.defaultPrefix());
  std::string prefix = "/";
  std::optional<bool> global;
  if (m_config != nullptr) {
    const auto& launcherCfg = m_config->config().shell.launcher;
    prefix = launcherCfg.providerPrefix;
    if (provider.allowCustomPrefix()) {
      const std::string key = StringUtils::toLower(std::string(provider.id()));
      auto it = std::ranges::find(launcherCfg.providers, key, &LauncherProviderConfig::name);
      if (it != launcherCfg.providers.end()) {
        if (!it->prefix.empty()) {
          triggerWord = it->prefix;
        }
        global = it->global;
      }
    }
  }

  if (provider.allowCustomPrefix()) {
    provider.setCustomPrefix(triggerWord.empty() ? std::string() : prefix + triggerWord);
    provider.setCustomIncludeInGlobalSearch(global);
  }
}

void LauncherPanel::finishActivation(LauncherProvider& provider, const LauncherResult& result, bool copied) {
  if (!result.kind.empty() && m_kusanagi != nullptr) {
    m_kusanagi->recordUse(result);
  } else if (shouldTrackUsage() && provider.trackUsage()) {
    m_usageTracker.record(provider.id(), result.id);
  }
  PanelManager::instance().closePanel(true);
  if (copied && provider.supportsAutoPaste() && m_onCopiedActivation) {
    m_onCopiedActivation();
  }
}

void LauncherPanel::addProvider(std::unique_ptr<LauncherProvider> provider) {
  applyProviderConfig(*provider);
  provider->initialize();
  provider->setResultsChangedCallback([this]() { onProviderResultsChanged(); });
  provider->setQueryRequestedCallback([this](std::string query) { setQuery(std::move(query)); });
  LauncherProvider* providerPtr = provider.get();
  provider->setActivationDoneCallback([this, providerPtr](const std::string& resultId, bool copied) {
    LauncherResult r;
    r.id = resultId;
    finishActivation(*providerPtr, r, copied);
  });
  if (provider->id() == kKusanagiProviderId) {
    m_kusanagi = static_cast<KusanagiProvider*>(provider.get());
  }
  m_providers.push_back(std::move(provider));
}

void LauncherPanel::clearDynamicProviders() {
  std::erase_if(m_providers, [](const std::unique_ptr<LauncherProvider>& provider) { return provider->isDynamic(); });
}

void LauncherPanel::clearProvidersWithIdPrefix(std::string_view prefix) {
  std::erase_if(m_providers, [&](const std::unique_ptr<LauncherProvider>& provider) {
    return provider->id().starts_with(prefix);
  });
}

void LauncherPanel::setScopedProvider(std::string_view providerId, std::string_view placeholder) {
  m_scopedProviderId = providerId;
  m_scopedPlaceholder = placeholder;
  syncField();
}

// Geometry

LauncherPanel::Look LauncherPanel::computeLook(float width, float height) const {
  Look l;
  const std::string style = kusanagi::opt<std::string>("launcher", "style", "card");
  l.full = style == "fullscreen";
  l.side = style == "side";
  l.spot = style == "spotlight";
  l.centered = kusanagi::opt<std::string>("launcher", "position", "upper") == "center";
  const float configW = static_cast<float>(kusanagi::opt<double>("launcher", "width", 640.0));
  l.cardW = l.full ? width : l.side ? std::min(configW, 460.0F) : l.spot ? std::min(configW, 600.0F) : configW;
  l.cardW = std::min(l.cardW, width);
  const float iconSize = static_cast<float>(kusanagi::opt<double>("launcher", "iconSize", 32.0));
  l.iconPx = l.full ? std::max(56.0F, iconSize) : iconSize;
  l.grid = l.full || kusanagi::opt<std::string>("launcher", "layout", "list") == "grid";
  l.rowH = std::max(54.0F, l.iconPx + 20.0F);
  l.cellW = std::max(108.0F, l.iconPx + 64.0F);
  l.cellH = l.iconPx + 58.0F;
  l.rows = std::max(1, kusanagi::opt<int>("launcher", "rows", 7));
  const float gridW = (l.full ? std::min(width - 120.0F, 1400.0F) : l.cardW) - 16.0F;
  l.cols = static_cast<std::size_t>(std::max(1.0F, std::floor(gridW / l.cellW)));
  l.gridRows = l.full ? std::max(2, static_cast<int>(std::floor((height - 200.0F) / l.cellH)))
                      : std::max(2, static_cast<int>(std::ceil(static_cast<float>(l.rows) / 2.0F)));
  l.radius = l.full ? 0.0F : l.spot ? 30.0F : kusanagi::radius();
  const float opacity = static_cast<float>(kusanagi::opt<double>("panel", "opacity", 0.95));
  l.panelOpacity = l.full ? std::min(0.88F, opacity) : opacity;
  return l;
}

bool LauncherPanel::gridLayout() const {
  // The // providers and dmenu sessions always use the list.
  return m_look.grid && m_scopedProviderId.empty() && !startsWithLauncherPrefix(m_query);
}

float LauncherPanel::cardTargetHeight(const Look& look, float height) const {
  if (look.full || look.side) {
    return height;
  }
  const bool listShown = !(look.spot && m_query.empty() && m_actionsOf.empty());
  const std::size_t n = m_results.size();
  if (n == 0 || !listShown) {
    return kFieldH;
  }
  if (shouldUseDetailPresentation()) {
    return kFieldH + static_cast<float>(look.rows) * look.rowH + 16.0F;
  }
  if (gridLayout()) {
    const auto rows = static_cast<float>(
        std::min<std::size_t>((n + look.cols - 1) / look.cols, static_cast<std::size_t>(look.gridRows))
    );
    return kFieldH + rows * look.cellH + 16.0F;
  }
  return kFieldH + static_cast<float>(std::min<std::size_t>(n, static_cast<std::size_t>(look.rows))) * look.rowH
       + 16.0F;
}

// Position the card group (shadow + card) from the animated values.
void LauncherPanel::placeCard() {
  if (m_cardGroup == nullptr || m_card == nullptr) {
    return;
  }
  const Look& l = m_look;
  const float p = m_openProgress; // 0 hidden, 1 shown
  const float h = m_cardH;
  float x = l.side ? 0.0F : std::round((m_surfaceW - l.cardW) * 0.5F);
  float y = 0.0F;
  if (!l.full && !l.side) {
    y = l.centered ? std::round(m_surfaceH * 0.5F - h * 0.5F) : std::round(m_surfaceH * 0.28F);
  }
  // The side card slides in from the left edge; the others drop in.
  float opacity = 1.0F;
  float scale = 1.0F;
  if (m_closing) {
    // Closing: one progress drives a slightly quicker fade, the scale and the move.
    const float t = 1.0F - p;
    opacity = 1.0F - std::clamp(t * 160.0F / 140.0F, 0.0F, 1.0F);
    const float s = inCubic(t);
    scale = l.side ? 1.0F : 1.0F + ((l.full ? 1.04F : 0.95F) - 1.0F) * s;
    const float m = outQuint(t);
    if (l.side) {
      x -= std::round(l.cardW * 0.35F * m);
    } else if (!l.full) {
      y -= 14.0F * m;
    }
  } else {
    // Opening: one progress drives a quicker fade, the overshooting scale and the move.
    opacity = std::clamp(p * 380.0F / 220.0F, 0.0F, 1.0F);
    const float s = outBack(p, kusanagi::bounce(1.3F));
    scale = l.side ? 1.0F : (l.full ? 1.04F : 0.95F) + (1.0F - (l.full ? 1.04F : 0.95F)) * s;
    if (l.side) {
      const float q = outQuint(std::clamp(p * 380.0F / 340.0F, 0.0F, 1.0F));
      x -= std::round(l.cardW * 0.35F * (1.0F - q));
    } else if (!l.full) {
      y -= 14.0F * (1.0F - outQuint(p));
    }
  }

  m_cardGroup->setPosition(x, y);
  m_cardGroup->setSize(l.cardW, h);
  m_cardGroup->setTransformOrigin(l.cardW * 0.5F, h * 0.5F);
  m_cardGroup->setScale(scale);
  m_cardGroup->setOpacity(opacity);
  m_card->setSize(l.cardW, h);
  if (m_cardArea != nullptr) {
    m_cardArea->setPosition(0.0F, 0.0F); // the click catcher spans the card
    m_cardArea->setSize(l.cardW, h);
  }
  if (m_shadow != nullptr) {
    m_shadow->setVisible(kusanagi::shadows());
    m_shadow->setPosition(0.0F, 14.0F);
    m_shadow->setSize(l.cardW, h);
  }
  if (m_backdrop != nullptr) {
    const float dim = static_cast<float>(kusanagi::opt<double>("look", "backdrop", 0.25));
    m_backdrop->setFill(fixedColorSpec(rgba(0.0F, 0.0F, 0.0F, std::clamp(dim * opacity, 0.0F, 1.0F))));
  }
}

void LauncherPanel::startOpenAnimation() {
  if (m_animations == nullptr || m_cardGroup == nullptr) {
    m_openProgress = 1.0F;
    placeCard();
    return;
  }
  m_openProgress = 0.0F;
  m_closing = false;
  placeCard();
  m_animations->animate(
      0.0F, 1.0F, 380.0F, Easing::Linear,
      [this](float v) {
        m_openProgress = v;
        placeCard();
      },
      {}, m_cardGroup
  );
}

bool LauncherPanel::beginCloseAnimation(std::function<void()> done) {
  if (m_animations == nullptr || m_cardGroup == nullptr) {
    return false;
  }
  m_animations->cancelForOwner(m_cardGroup);
  m_closing = true;
  const float from = m_openProgress;
  m_animations->animate(
      from, 0.0F, 160.0F * from, Easing::Linear,
      [this](float v) {
        m_openProgress = v;
        placeCard();
      },
      [done = std::move(done)]() { done(); }, m_cardGroup
  );
  return true;
}

void LauncherPanel::requestClose() { PanelManager::instance().closePanel(true); }

// Scene

void LauncherPanel::create() {
  m_look = Look{};
  m_cardTargetH = -1.0F;
  m_cardH = kFieldH;
  m_closing = false;

  auto root = ui::node({.out = &m_rootNode});

  // The screen dims behind the card, and a click outside the card closes the launcher.
  m_rootNode->addChild(ui::box({.out = &m_backdrop}));
  m_rootNode->addChild(ui::inputArea({
      .onClick = [this](const InputArea::PointerData& /*data*/) { requestClose(); },
  }));

  m_rootNode->addChild(ui::node({.out = &m_cardGroup}));
  m_cardGroup->addChild(ui::box({.out = &m_shadow}));
  m_cardGroup->addChild(ui::box({.out = &m_card}));
  m_card->setClipChildren(true);
  // Swallow clicks on the card itself.
  m_card->addChild(ui::inputArea({.out = &m_cardArea}));

  m_card->addChild(ui::node({.out = &m_field}));
  m_field->addChild(ui::box({.out = &m_fieldPill, .visible = false}));
  m_field->addChild(ui::label({
      .out = &m_fieldIcon,
      .fontSize = 20.0F,
      .fontFamily = std::string(kIconFont),
      .color = colorSpecFromRole(ColorRole::Primary),
  }));
  m_field->addChild(ui::label({
      .out = &m_placeholder,
      .fontSize = 15.0F,
      .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      .maxLines = 1,
  }));
  m_field->addChild(ui::input({
      .out = &m_input,
      .fontSize = 18.0F,
      .controlHeight = 40.0F,
      .horizontalPadding = 0.0F,
      .clearButtonEnabled = false,
      .frameVisible = false,
      .onChange =
          [this](const std::string& text) {
            if (m_input == nullptr) {
              return;
            }
            const std::string preview = singleLinePreview(text);
            if (preview != text) {
              m_input->setValue(preview);
            }
            onInputChanged(preview);
          },
      .onSubmit = [this](const std::string& /*text*/) { activateSelected(); },
      .onKeyEvent = [this](std::uint32_t sym, std::uint32_t modifiers) { return handleKeyEvent(sym, modifiers); },
  }));

  m_card->addChild(ui::box({.out = &m_separator, .fill = colorSpecFromRole(ColorRole::OnSurface, 0.07F)}));

  m_adapter = std::make_unique<LauncherResultAdapter>(m_asyncTextures);
  m_adapter->setResults(&m_results);
  m_adapter->setOnActivate([this](std::size_t index) { activateAt(index); });
  m_adapter->setOnHover([this](std::size_t index) {
    if (m_grid != nullptr && index < m_results.size() && index != m_selectedIndex) {
      select(index);
    }
  });
  m_adapter->setOnActions([this](std::size_t index) {
    if (m_grid != nullptr) {
      (void)openActions(index);
    }
  });

  m_card->addChild(ui::virtualGridView({
      .out = &m_grid,
      .columns = 1,
      .cellHeight = 54.0F,
      .squareCells = false,
      .columnGap = 0.0F,
      .rowGap = 0.0F,
      .overscanRows = kRowOverscan,
      .itemCursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
      .scrollbarVisible = false,
      .adapter = m_adapter.get(),
      .onSelectionChanged =
          [this](std::optional<std::size_t> idx) {
            if (idx.has_value() && *idx < m_results.size()) {
              m_selectedIndex = *idx;
              if (m_adapter != nullptr) {
                m_adapter->setSelected(*idx);
              }
            }
          },
      .configure =
          [](VirtualGridView& grid) {
            grid.scrollView().setViewportPaddingH(0.0F);
            grid.scrollView().setViewportPaddingV(0.0F);
          },
  }));

  auto detailScroll = ui::scrollView({
      .out = &m_detailScroll,
      .scrollbarVisible = true,
      .viewportPaddingH = 12.0F,
      .viewportPaddingV = 8.0F,
      .visible = false,
  });
  auto* detailContent = detailScroll->content();
  detailContent->setDirection(FlexDirection::Vertical);
  detailContent->setAlign(FlexAlign::Stretch);
  detailContent->setGap(Style::spaceSm);
  detailContent->addChild(ui::label({
      .out = &m_detailSubtitle,
      .fontSize = 11.0F,
      .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
      .maxLines = 1,
      .ellipsize = TextEllipsize::End,
      .visible = false,
      .participatesInLayout = false,
  }));
  detailContent->addChild(ui::label({
      .out = &m_detailBody,
      .fontSize = 13.0F,
      .color = colorSpecFromRole(ColorRole::OnSurface),
      .maxLines = 0,
      .flexGrow = 1.0F,
  }));
  m_card->addChild(std::move(detailScroll));

  setRoot(std::move(root));
  if (m_animations != nullptr) {
    m_rootNode->setAnimationManager(m_animations);
  }
  // The highlight scrolls the view along as it slides, so the grid itself doesn't.
  m_grid->setScrollsToSelection(false);
  m_highlight.attach(m_grid->scrollView(), m_animations);
  m_highlightGeometry.clear();
}

void LauncherPanel::syncHighlight(bool animate) {
  if (!m_highlight.attached() || m_grid == nullptr) {
    return;
  }
  const bool grid = gridLayout();
  const bool shown = m_grid->visible() && m_selectedIndex < m_results.size();
  m_highlight.setVisible(shown);
  if (!shown) {
    m_highlightGeometry.clear();
    return;
  }
  Box* box = m_highlight.box();
  box->setRadius(std::max(grid ? 8.0F : 6.0F, kusanagi::radius() - 6.0F));
  box->setFill(colorSpecFromRole(ColorRole::Primary, 0.16F));
  box->setBorder(colorSpecFromRole(ColorRole::Primary, 0.35F), 1.0F);
  // Matches the classic look: the list highlight moves in a fixed time, the grid one at a capped speed.
  const float move = static_cast<float>(kusanagi::ms(160));
  m_highlight.setMotion({.durationMs = move, .velocity = grid ? 200.0F : -1.0F}, {});
  kusanagi::SlideHighlight::Rect rect;
  if (grid) {
    const std::size_t cols = std::max<std::size_t>(1, m_look.cols);
    rect = {
        static_cast<float>(m_selectedIndex % cols) * m_look.cellW,
        static_cast<float>(m_selectedIndex / cols) * m_look.cellH, m_look.cellW, m_look.cellH,
    };
  } else {
    rect = {0.0F, static_cast<float>(m_selectedIndex) * m_look.rowH, m_gridW, m_look.rowH};
  }
  // A new layout (style, size, columns) or a fresh list puts it straight there.
  const std::string geometry = std::format(
      "{}:{}:{}:{}:{}:{}", grid, m_look.cols, m_look.cellW, m_look.cellH, m_look.rowH, m_gridW
  );
  const bool sameGeometry = geometry == m_highlightGeometry;
  m_highlightGeometry = geometry;
  const auto& at = m_highlight.target();
  if (sameGeometry && at.x == rect.x && at.y == rect.y && at.w == rect.w && at.h == rect.h) {
    return; // already there or on its way
  }
  m_highlight.moveTo(rect, animate && sameGeometry);
}

void LauncherPanel::syncField() {
  if (m_input == nullptr || m_placeholder == nullptr || m_fieldIcon == nullptr) {
    return;
  }
  const bool actions = !m_actionsOf.empty();
  const auto mode = startsWithLauncherPrefix(m_query) ? KusanagiProvider::Mode::Apps : KusanagiProvider::modeOf(m_query);
  m_fieldIcon->setText(KusanagiProvider::fieldGlyph(mode, actions));
  std::string hint;
  if (!m_scopedProviderId.empty()) {
    hint = m_scopedPlaceholder.empty() ? i18n::tr("launcher.search-placeholder") : m_scopedPlaceholder;
  } else if (actions) {
    hint = m_actionsOfName + " actions    ← back";
  } else {
    hint = "Search apps    = calc    > run    : emoji    / files    ? web";
  }
  m_placeholder->setText(hint);
  m_placeholder->setVisible(m_input->value().empty());
}

void LauncherPanel::doLayout(Renderer& renderer, float width, float height) {
  if (m_rootNode == nullptr || m_input == nullptr || m_grid == nullptr) {
    return;
  }
  m_surfaceW = width;
  m_surfaceH = height;
  m_look = computeLook(width, height);
  const Look& l = m_look;

  m_rootNode->setSize(width, height);
  for (auto& child : m_rootNode->children()) {
    if (child.get() != m_cardGroup) {
      child->setPosition(0.0F, 0.0F);
      child->setSize(width, height);
    }
  }

  m_card->setPosition(0.0F, 0.0F);
  m_card->setFill(colorSpecFromRole(ColorRole::Surface, l.panelOpacity));
  if (kusanagi::surfaceBorderWidth() > 0.0F) {
    m_card->setBorder(kusanagi::surfaceBorder(), kusanagi::surfaceBorderWidth());
  } else {
    m_card->clearBorder();
  }
  // The side card's left corners sit on the screen edge.
  const float r = l.radius;
  m_card->setRadii(l.side ? Radii{0.0F, r, r, 0.0F} : Radii{r, r, r, r});
  m_shadow->setStyle(RoundedRectStyle{
      .fill = rgba(0.0F, 0.0F, 0.0F, 0.5F),
      .border = Color{},
      .fillMode = FillMode::Solid,
      .radius = l.side ? Radii{0.0F, r, r, 0.0F} : Radii{r, r, r, r},
      .softness = 28.0F,
      .borderWidth = 0.0F,
      .outerShadow = true,
      .shadowCutoutOffsetX = 0.0F,
      .shadowCutoutOffsetY = 14.0F,
  });
  m_shadow->setVisible(!l.full);

  // The search field is a pill near the top for fullscreen and side, else the card's first row.
  const float fieldW = l.full ? std::min(560.0F, l.cardW - 80.0F) : l.cardW;
  const float fieldY = l.full ? 56.0F : l.side ? 18.0F : 0.0F;
  m_field->setPosition(std::round((l.cardW - fieldW) * 0.5F), fieldY);
  m_field->setSize(fieldW, kFieldH);
  m_fieldPill->setVisible(l.full || l.side);
  m_fieldPill->setPosition(6.0F, 6.0F);
  m_fieldPill->setSize(fieldW - 12.0F, kFieldH - 12.0F);
  m_fieldPill->setRadius((kFieldH - 12.0F) * 0.5F);
  m_fieldPill->setFill(colorSpecFromRole(ColorRole::OnSurface, 0.07F));
  m_fieldPill->setBorder(colorSpecFromRole(ColorRole::OnSurface, 0.08F), 1.0F);

  syncField();
  m_fieldIcon->measure(renderer);
  m_fieldIcon->setPosition(22.0F, std::round((kFieldH - m_fieldIcon->height()) * 0.5F));
  m_input->setFontSize(l.spot ? 20.0F : 18.0F);
  const float inputW = std::max(0.0F, fieldW - 56.0F - 22.0F);
  m_input->setSize(inputW, 40.0F);
  m_input->setPosition(56.0F, std::round((kFieldH - 40.0F) * 0.5F));
  m_placeholder->measure(renderer); // may run past the card edge, where it is clipped
  m_placeholder->setPosition(56.0F, std::round((kFieldH - m_placeholder->height()) * 0.5F));

  m_separator->setVisible(!m_results.empty());
  m_separator->setPosition(16.0F, fieldY + kFieldH);
  m_separator->setSize(std::max(0.0F, l.cardW - 32.0F), 1.0F);

  // Results
  const bool grid = gridLayout();
  m_adapter->setRenderer(&renderer);
  const TileStyle style{.grid = grid, .actionsMode = !m_actionsOf.empty(), .iconPx = l.iconPx, .radius = kusanagi::radius()};
  const TileStyle& prev = m_adapter->style();
  const bool styleChanged = prev.grid != style.grid || prev.actionsMode != style.actionsMode
                         || prev.iconPx != style.iconPx || prev.radius != style.radius;
  m_adapter->setStyle(style);
  const float listTop = fieldY + kFieldH + 8.0F;
  const float listH = std::max(
      0.0F, (l.full || l.side ? height : cardTargetHeight(l, height)) - listTop - 8.0F
  );
  if (grid) {
    const float gridW = static_cast<float>(l.cols) * l.cellW;
    m_grid->setColumns(l.cols);
    m_grid->setCellHeight(l.cellH);
    m_grid->setPosition(std::max(8.0F, std::floor((l.cardW - gridW) * 0.5F)), listTop);
    m_grid->setSize(gridW, listH);
  } else {
    m_grid->setColumns(1);
    m_grid->setCellHeight(l.rowH);
    m_grid->setPosition(8.0F, listTop);
    m_grid->setSize(std::max(0.0F, l.cardW - 16.0F), listH);
  }
  if (styleChanged) {
    m_grid->notifyDataChanged();
  }
  m_gridW = m_grid->width();
  syncHighlight(false);
  m_detailScroll->setPosition(8.0F, listTop);
  m_detailScroll->setSize(std::max(0.0F, l.cardW - 16.0F), listH);

  // The card height follows the results.
  const float target = cardTargetHeight(l, height);
  if (m_cardTargetH < 0.0F || m_animations == nullptr) {
    m_cardH = target;
    m_cardTargetH = target;
  } else if (std::abs(target - m_cardTargetH) > 0.5F) {
    m_cardTargetH = target;
    if (m_heightAnim != 0) {
      m_animations->cancel(m_heightAnim);
    }
    const float from = m_cardH;
    m_heightAnim = m_animations->animate(
        0.0F, 1.0F, 220.0F, Easing::Linear,
        [this, from, target](float t) {
          m_cardH = from + (target - from) * outQuint(t);
          placeCard();
        },
        [this]() { m_heightAnim = 0; }, m_card
    );
  }
  placeCard();

  m_rootNode->layout(renderer);
}

void LauncherPanel::onOpen(std::string_view context) {
  for (auto& provider : m_providers) {
    applyProviderConfig(*provider);
  }
  // Pick up apps installed since the last scan. Cheap stat-only check; only rescans on real change.
  refreshDesktopEntriesIfSourcesChanged();

  m_actionsOf.clear();
  m_actionsOfName.clear();
  m_selectedIndex = 0;
  const std::string initialValue = singleLinePreview(context);
  if (m_input != nullptr) {
    m_input->setValue(initialValue);
  }
  if (m_grid != nullptr) {
    m_grid->scrollView().setScrollOffset(0.0F);
  }
  onInputChanged(initialValue);
  startOpenAnimation();
}

void LauncherPanel::onClose() {
  if (m_asyncTextures != nullptr) {
    DeferredCall::callLater([asyncTextures = m_asyncTextures]() { asyncTextures->trimUnused(0); });
  }
  for (auto& provider : m_providers) {
    provider->reset();
  }

  m_query.clear();
  m_results.clear();
  m_results.shrink_to_fit();
  m_scopedProviderId.clear();
  m_scopedPlaceholder.clear();
  m_actionsOf.clear();
  m_actionsOfName.clear();
  m_selectedIndex = 0;
  m_heightAnim = 0;
  m_closing = false;

  if (m_grid != nullptr) {
    m_grid->setAdapter(nullptr);
  }
  m_adapter.reset();

  // The scene tree (and all nodes) is destroyed by PanelManager after onClose().
  m_rootNode = nullptr;
  m_backdrop = nullptr;
  m_cardGroup = nullptr;
  m_shadow = nullptr;
  m_card = nullptr;
  m_cardArea = nullptr;
  m_field = nullptr;
  m_fieldPill = nullptr;
  m_fieldIcon = nullptr;
  m_input = nullptr;
  m_placeholder = nullptr;
  m_separator = nullptr;
  m_grid = nullptr;
  m_highlight.detach();
  m_detailScroll = nullptr;
  m_detailSubtitle = nullptr;
  m_detailBody = nullptr;
  clearReleasedRoot();
}

void LauncherPanel::onIconThemeChanged() { reapplyCurrentQuery(); }

void LauncherPanel::clearUsage() {
  m_usageTracker.clear();
  if (m_input != nullptr) {
    reapplyCurrentQuery();
  }
}

bool LauncherPanel::shouldTrackUsage() const {
  return m_config != nullptr && m_config->config().shell.launcher.sortByUsage;
}

void LauncherPanel::syncUsageTrackingState() {
  if (m_input != nullptr) {
    reapplyCurrentQuery();
  }
}

void LauncherPanel::reapplyCurrentQuery() {
  std::string selectedProvider;
  std::string selectedId;
  if (m_selectedIndex < m_results.size()) {
    selectedProvider = m_results[m_selectedIndex].providerId;
    selectedId = m_results[m_selectedIndex].id;
  }

  onInputChanged(m_query);

  if (!selectedId.empty()) {
    for (std::size_t i = 0; i < m_results.size(); ++i) {
      if (m_results[i].providerId == selectedProvider && m_results[i].id == selectedId) {
        m_selectedIndex = i;
        break;
      }
    }
  }
  refreshResults();
}

void LauncherPanel::setQuery(std::string query) {
  if (m_input == nullptr) {
    return;
  }
  m_actionsOf.clear();
  m_actionsOfName.clear();
  m_input->setValue(singleLinePreview(query));
  if (m_grid != nullptr) {
    m_grid->scrollView().setScrollOffset(0.0F);
  }
  onInputChanged(query);
}

void LauncherPanel::onProviderResultsChanged() {
  // Only re-gather while the panel is open and built.
  if (m_input == nullptr) {
    return;
  }
  reapplyCurrentQuery();
}

InputArea* LauncherPanel::initialFocusArea() const { return m_input != nullptr ? m_input->inputArea() : nullptr; }

bool LauncherPanel::handleGlobalKey(std::uint32_t sym, std::uint32_t modifiers, bool pressed, bool preedit) {
  if (!pressed || preedit || m_closing) {
    return false;
  }

  auto& dispatcher = PanelManager::instance().inputDispatcher();
  InputArea* const focused = dispatcher.focusedArea();
  if (focused != nullptr) {
    const bool onInput = (m_input != nullptr && focused == m_input->inputArea());
    const bool inResults = (m_grid != nullptr && isDescendantOf(focused, m_grid));
    if (!onInput && !inResults) {
      return false;
    }
  }
  return handleKeyEvent(sym, modifiers);
}

// Results

std::vector<LauncherResult> LauncherPanel::engineResults(const std::string& text, bool& loading) {
  std::vector<LauncherResult> results;
  LauncherProvider* activeProvider = nullptr;
  std::string_view queryText = text;
  for (auto& provider : m_providers) {
    const auto prefix = provider->prefix();
    if (prefix.empty() || provider->id() == kKusanagiProviderId) {
      continue;
    }
    if (std::string_view(text).starts_with(prefix)
        && (activeProvider == nullptr || prefix.size() > activeProvider->prefix().size())) {
      activeProvider = provider.get();
      queryText = std::string_view(text).substr(prefix.size());
    }
  }
  if (activeProvider == nullptr) {
    return providerOverviewResults(text);
  }
  if (!queryText.empty() && queryText.front() == ' ') {
    queryText = queryText.substr(1);
  }
  results = activeProvider->queryPrefixed(queryText);
  loading = activeProvider->isLoading();
  const bool typedQuery = !queryText.empty();
  if (activeProvider->trackUsage() && shouldTrackUsage()) {
    for (auto& result : results) {
      const int usageCount = m_usageTracker.getCount(activeProvider->id(), result.id);
      result.score += usageBoostForScore(result.score, usageCount, typedQuery);
    }
  }
  for (auto& result : results) {
    result.providerId = activeProvider->id();
  }
  sortResultsByScore(results);
  return results;
}

void LauncherPanel::onInputChanged(const std::string& text) {
  const auto desktopVersion = desktopEntriesVersion();
  if (desktopVersion != m_desktopEntriesVersion) {
    m_iconResolver.invalidateMissingCache();
    m_desktopEntriesVersion = desktopVersion;
  }

  m_query = text;
  m_results.clear();
  bool loading = false;

  if (!m_scopedProviderId.empty()) {
    for (auto& provider : m_providers) {
      if (provider->id() != m_scopedProviderId) {
        continue;
      }
      m_results = provider->query(text);
      for (auto& result : m_results) {
        result.providerId = provider->id();
      }
      sortResultsByScore(m_results);
      break;
    }
  } else if (startsWithLauncherPrefix(text)) {
    m_results = engineResults(text, loading);
  } else if (m_kusanagi != nullptr) {
    m_results = m_kusanagi->search(text, m_actionsOf);
    for (auto& result : m_results) {
      if (result.providerId.empty()) {
        result.providerId = std::string(kKusanagiProviderId);
      }
    }
  }
  (void)loading;

  const int iconTargetSize = static_cast<int>(std::round(m_look.iconPx));
  for (auto& result : m_results) {
    if (result.iconPath.empty() && !result.iconName.empty()) {
      const std::string& resolved = m_iconResolver.resolve(result.iconName, iconTargetSize);
      if (!resolved.empty()) {
        result.iconPath = resolved;
      }
      result.iconName.clear();
    }
  }

  m_selectedIndex = 0;
  syncField();
  refreshResults();
}

void LauncherPanel::select(std::size_t index) {
  if (index >= m_results.size()) {
    return;
  }
  m_selectedIndex = index;
  if (m_adapter != nullptr) {
    m_adapter->setSelected(index);
  }
  if (m_grid != nullptr) {
    m_grid->setSelectedIndex(index);
  }
  syncHighlight(true);
}

void LauncherPanel::refreshResults() {
  uiAssertNotRendering("LauncherPanel::refreshResults");
  if (m_grid == nullptr) {
    return;
  }
  if (m_adapter != nullptr) {
    m_adapter->setSelected(m_selectedIndex);
    m_adapter->resetHover();
  }
  m_grid->notifyDataChanged();
  if (m_results.empty()) {
    m_grid->setSelectedIndex(std::nullopt);
    m_grid->scrollView().setScrollOffset(0.0F);
  } else {
    m_grid->setSelectedIndex(m_selectedIndex);
  }
  const bool detail = shouldUseDetailPresentation();
  const bool listShown = !(m_look.spot && m_query.empty() && m_actionsOf.empty());
  m_grid->setVisible(!m_results.empty() && !detail && listShown);
  if (m_detailScroll != nullptr) {
    m_detailScroll->setVisible(detail);
  }
  bindDetailResult();
  // New results: the highlight jumps to the current row without sliding.
  m_highlightGeometry.clear();
  syncHighlight(false);
  if (m_rootNode != nullptr) {
    m_rootNode->markLayoutDirty();
  }
}

bool LauncherPanel::shouldUseDetailPresentation() const {
  return m_results.size() == 1 && m_results.front().presentation == "detail";
}

void LauncherPanel::bindDetailResult() {
  if (!shouldUseDetailPresentation()
      || m_detailScroll == nullptr
      || m_detailSubtitle == nullptr
      || m_detailBody == nullptr) {
    return;
  }
  const LauncherResult& result = m_results.front();
  const bool hasSubtitle = !result.subtitle.empty();
  m_detailSubtitle->setVisible(hasSubtitle);
  m_detailSubtitle->setParticipatesInLayout(hasSubtitle);
  m_detailSubtitle->setText(singleLinePreview(result.subtitle));
  m_detailBody->setText(result.title.empty() ? result.id : result.title);
  m_detailScroll->setScrollOffset(0.0F);
}

bool LauncherPanel::startsWithLauncherPrefix(std::string_view text) const {
  const std::string& prefix = m_config != nullptr ? m_config->config().shell.launcher.providerPrefix : "//";
  return !prefix.empty() && text.starts_with(prefix);
}

std::vector<LauncherResult> LauncherPanel::providerOverviewResults(std::string_view text) const {
  std::string filter;
  if (startsWithLauncherPrefix(text)) {
    const std::string& prefix = m_config != nullptr ? m_config->config().shell.launcher.providerPrefix : "//";
    filter = StringUtils::toLower(StringUtils::trim(text.substr(prefix.size())));
  }

  std::vector<LauncherResult> results;
  results.reserve(m_providers.size());
  for (const auto& provider : m_providers) {
    const std::string_view prefix = provider->prefix();
    if (prefix.empty()) {
      continue;
    }
    const std::string title(provider->displayName());
    const std::string prefixText(prefix);
    const std::string searchable = StringUtils::toLower(title + " " + prefixText);
    const double score = filter.empty() ? 0.0 : FuzzyMatch::score(filter, searchable);
    if (!filter.empty() && !FuzzyMatch::isMatch(score)) {
      continue;
    }
    LauncherResult result;
    result.id = std::string(kProviderOverviewResultPrefix) + prefixText;
    result.providerId = std::string(kProviderOverviewProviderId);
    result.title = title;
    result.subtitle = prefixText;
    result.glyphName = std::string(provider->defaultGlyphName());
    result.score = score;
    results.push_back(std::move(result));
  }
  if (!filter.empty()) {
    sortResultsByScore(results);
  }
  return results;
}

// App actions

bool LauncherPanel::actionsOfSelected() { return m_grid != nullptr && openActions(m_selectedIndex); }

bool LauncherPanel::openActions(std::size_t index) {
  if (!m_actionsOf.empty() || index >= m_results.size() || !KusanagiProvider::hasActions(m_results[index])) {
    return false;
  }
  m_actionsOf = m_results[index].desktopEntryPath;
  m_actionsOfName = m_results[index].title;
  if (m_input != nullptr) {
    m_input->setValue("");
  }
  if (m_grid != nullptr) {
    m_grid->scrollView().setScrollOffset(0.0F);
  }
  onInputChanged("");
  return true;
}

void LauncherPanel::closeActions() {
  m_actionsOf.clear();
  m_actionsOfName.clear();
  if (m_input != nullptr) {
    m_input->setValue("");
  }
  if (m_grid != nullptr) {
    m_grid->scrollView().setScrollOffset(0.0F);
  }
  onInputChanged("");
}

// Activation

void LauncherPanel::activateAt(std::size_t index, bool alternate) {
  if (index >= m_results.size()) {
    return;
  }
  m_selectedIndex = index;
  activateSelected(alternate);
}

void LauncherPanel::activateSelected(bool alternate) {
  if (m_selectedIndex >= m_results.size() || m_closing) {
    return;
  }
  const LauncherResult result = m_results[m_selectedIndex];
  if (result.providerId == kProviderOverviewProviderId && result.id.starts_with(kProviderOverviewResultPrefix)) {
    std::string prefix = result.id.substr(kProviderOverviewResultPrefix.size());
    if (!prefix.empty()) {
      prefix += ' ';
    }
    setQuery(prefix);
    return;
  }

  // Dispatch only to the provider that produced this result.
  for (auto& provider : m_providers) {
    if (provider->id() != std::string_view(result.providerId)) {
      continue;
    }
    const bool done = provider.get() == m_kusanagi ? m_kusanagi->activate(result, alternate) : provider->activate(result);
    if (!done) {
      return;
    }
    finishActivation(*provider, result, provider->supportsAutoPaste());
    return;
  }
}

bool LauncherPanel::handleKeyEvent(std::uint32_t sym, std::uint32_t modifiers) {
  const std::size_t n = m_results.size();
  const bool grid = gridLayout() && m_grid != nullptr;
  const std::size_t cols = grid ? std::max<std::size_t>(1, m_look.cols) : 1;
  const bool ctrl = (modifiers & KeyMod::Ctrl) != 0;
  const bool shift = (modifiers & KeyMod::Shift) != 0;
  const bool alt = (modifiers & KeyMod::Alt) != 0;
  const bool actions = !m_actionsOf.empty();
  const bool enter = sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter;

  if (KeybindMatcher::matches(KeybindAction::Cancel, sym, modifiers)) {
    if (actions) {
      closeActions();
      return true;
    }
    return false; // the manager closes the launcher
  }
  if (!grid && sym == XKB_KEY_Right && !actions && n > 0 && openActions(m_selectedIndex)) {
    return true;
  }
  if (enter && alt && !actions) {
    (void)openActions(m_selectedIndex);
    return true;
  }
  if (actions
      && ((sym == XKB_KEY_Left && !grid) || (sym == XKB_KEY_BackSpace && m_input != nullptr && m_input->value().empty()))) {
    closeActions();
    return true;
  }
  if (n == 0) {
    return enter;
  }
  const bool down = sym == XKB_KEY_Down || sym == XKB_KEY_Tab || (ctrl && (sym == XKB_KEY_j || sym == XKB_KEY_J));
  const bool up = sym == XKB_KEY_Up || sym == XKB_KEY_ISO_Left_Tab || (ctrl && (sym == XKB_KEY_k || sym == XKB_KEY_K));
  // Grid: Left/Right step and Up/Down jump a row. List: Up/Down step and wrap.
  if (grid && sym == XKB_KEY_Right) {
    select(std::min(n - 1, m_selectedIndex + 1));
    return true;
  }
  if (grid && sym == XKB_KEY_Left) {
    select(m_selectedIndex > 0 ? m_selectedIndex - 1 : 0);
    return true;
  }
  if (down) {
    select(grid && (sym == XKB_KEY_Down) ? std::min(n - 1, m_selectedIndex + cols) : (m_selectedIndex + 1) % n);
    return true;
  }
  if (up) {
    if (grid && sym == XKB_KEY_Up) {
      select(m_selectedIndex >= cols ? m_selectedIndex - cols : 0);
    } else {
      select((m_selectedIndex + n - 1) % n);
    }
    return true;
  }
  const auto page = static_cast<std::size_t>(std::max(1, m_look.rows));
  if (KeySymbol::isPageDown(sym)) {
    select(std::min(n - 1, m_selectedIndex + page));
    return true;
  }
  if (KeySymbol::isPageUp(sym)) {
    select(m_selectedIndex >= page ? m_selectedIndex - page : 0);
    return true;
  }
  if (enter) {
    activateSelected(shift);
    return true;
  }
  return false;
}
