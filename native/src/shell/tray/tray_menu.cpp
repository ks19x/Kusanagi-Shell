#include "shell/tray/tray_menu.h"

#include "config/config_service.h"
#include "core/deferred_call.h"
#include "core/input/keybind_matcher.h"
#include "core/log.h"
#include "core/ui_phase.h"
#include "dbus/tray/tray_service.h"
#include "i18n/i18n.h"
#include "render/render_context.h"
#include "shell/panel/panel_manager.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/tray/tray_identifier.h"
#include "shell/tray/tray_settings.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "ui/controls/context_menu.h"
#include "ui/controls/scroll_view.h"
#include "ui/popup_chrome.h"
#include "ui/style.h"
#include "wayland/layer_surface.h"
#include "wayland/wayland_connection.h"
#include "wayland/wayland_seat.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "cursor-shape-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <linux/input-event-codes.h>
#include <cmath>
#include <optional>
#include <string>

namespace {

  constexpr Logger kLog("tray");

  constexpr float kCardW = 240.0F;
  constexpr float kRowH = 32.0F;
  constexpr float kSeparatorH = 9.0F;
  constexpr float kSubIndent = 12.0F;
  constexpr float kHiddenScale = 0.92F;
  // Room around the card on the popup surface for the spring's overshoot and the shadow.
  constexpr float kPadX = 8.0F;
  constexpr float kPadTop = 8.0F;
  constexpr float kPadBottom = 28.0F;
  constexpr const char* kIconFont = "JetBrainsMono Nerd Font";

  std::string utf8(char32_t cp) {
    std::string out;
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
    return out;
  }

  // The primary Kusanagi bar's inner margin. The bar's thickness includes it, so the menu keeps clear of it.
  // The margin may be a number, [edge, inner, sides], [edge, sides] or {edge, inner, sides}.
  float barInnerMargin() {
    const auto& s = kusanagi::settings();
    const auto bars = s.find("bars");
    if (bars == s.end() || !bars->is_array() || bars->empty() || !bars->front().is_object()) {
      return 0.0F;
    }
    const auto m = bars->front().find("margin");
    if (m == bars->front().end()) {
      return 0.0F;
    }
    if (m->is_array() && m->size() >= 3 && (*m)[1].is_number()) {
      return (*m)[1].get<float>();
    }
    if (m->is_object() && m->contains("inner") && (*m)["inner"].is_number()) {
      return (*m)["inner"].get<float>();
    }
    return 0.0F;
  }

  struct IconRef {
    std::string itemId;
    InputArea* area = nullptr;
    const void* owner = nullptr;
  };
  std::vector<IconRef>& iconRegistry() {
    static std::vector<IconRef> refs;
    return refs;
  }
  InputArea*& anchorHint() {
    static InputArea* hint = nullptr;
    return hint;
  }
  constexpr std::size_t kTrayMenuVisibleItems = 20;
  constexpr std::int32_t kPinToggleEntryId = -2147000000;

  constexpr std::uint32_t kPopupConstraintAdjust = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X
      | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y
      | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X
      | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y;

  bool containsTrayWidget(const std::vector<std::string>& widgets) {
    return std::ranges::contains(widgets, tray::kCanonicalTrayWidgetName);
  }

  void closeTrayDrawerPanelIfOpen() {
    auto& panelManager = PanelManager::instance();
    if (panelManager.isOpenPanel("tray-drawer")) {
      panelManager.close();
    }
  }

  bool trayDrawerEnabled(ConfigService* config) {
    return config != nullptr && tray::resolvedTrayOptions(*config).options.drawerMode;
  }

  std::size_t visibleEntryLimit(std::size_t entryCount) {
    return std::max<std::size_t>(1, std::min<std::size_t>(entryCount, kTrayMenuVisibleItems));
  }

  // Convert an icon name like "audio-input-microphone-symbolic" to a readable label like "Audio Input Microphone".
  std::string iconNameToLabel(std::string_view iconName) {
    // Strip trailing "-symbolic"
    constexpr std::string_view kSymbolicSuffix = "-symbolic";
    if (iconName.ends_with(kSymbolicSuffix)) {
      iconName.remove_suffix(kSymbolicSuffix.size());
    }
    std::string out;
    out.reserve(iconName.size());
    bool capitaliseNext = true;
    for (char c : iconName) {
      if (c == '-') {
        out.push_back(' ');
        capitaliseNext = true;
      } else if (capitaliseNext) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        capitaliseNext = false;
      } else {
        out.push_back(c);
      }
    }
    return out;
  }

  std::optional<BarConfig> resolveTrayBarConfig(ConfigService* config, WaylandConnection* wayland, wl_output* output) {
    if (config == nullptr) {
      return std::nullopt;
    }

    const WaylandOutput* wlOutput = nullptr;
    if (wayland != nullptr && output != nullptr) {
      wlOutput = wayland->findOutputByWl(output);
    }

    std::optional<BarConfig> fallback;
    for (const auto& base : config->config().bars) {
      BarConfig resolved = base;
      if (wlOutput != nullptr) {
        resolved = ConfigService::resolveForOutput(base, *wlOutput);
      }
      if (!resolved.enabled) {
        continue;
      }
      if (!fallback.has_value()) {
        fallback = resolved;
      }
      if (containsTrayWidget(resolved.startWidgets)
          || containsTrayWidget(resolved.centerWidgets)
          || containsTrayWidget(resolved.endWidgets)) {
        return resolved;
      }
    }
    return fallback;
  }

  struct PopupPlacement {
    std::int32_t anchorX = 0;
    std::int32_t anchorY = 0;
    std::int32_t anchorWidth = 1;
    std::int32_t anchorHeight = 1;
    std::uint32_t anchor = XDG_POSITIONER_ANCHOR_NONE;
    std::uint32_t gravity = XDG_POSITIONER_GRAVITY_TOP;
    std::int32_t offsetX = 0;
    std::int32_t offsetY = 2;
    popup_chrome::Attachment chromeAttachment{
        .horizontal = popup_chrome::HorizontalAttachment::Center, .vertical = popup_chrome::VerticalAttachment::Top
    };
    ContextSubmenuDirection submenuDirection = ContextSubmenuDirection::Right;
  };

  PopupPlacement
  popupPlacementForBar(const BarConfig& bar, std::int32_t anchorX, std::int32_t anchorY, float contentScale) {
    const std::int32_t kGap =
        std::max(2, static_cast<std::int32_t>(std::lround(Style::spaceMd * std::max(0.1F, contentScale))));
    const std::int32_t iconSize = std::clamp(bar.thickness - 10, 16, 40);
    const std::int32_t halfIcon = iconSize / 2;
    PopupPlacement placement{
        .anchorX = anchorX - halfIcon,
        .anchorY = anchorY - halfIcon,
        .anchorWidth = iconSize,
        .anchorHeight = iconSize,
    };

    if (bar.position == "bottom") {
      placement.anchor = XDG_POSITIONER_ANCHOR_TOP;
      placement.gravity = XDG_POSITIONER_GRAVITY_TOP;
      placement.offsetX = 0;
      placement.offsetY = -kGap;
      placement.chromeAttachment = popup_chrome::Attachment{
          .horizontal = popup_chrome::HorizontalAttachment::Center,
          .vertical = popup_chrome::VerticalAttachment::Bottom,
      };
      placement.submenuDirection = ContextSubmenuDirection::Right;
      return placement;
    }

    if (bar.position == "left") {
      placement.anchor = XDG_POSITIONER_ANCHOR_RIGHT;
      placement.gravity = XDG_POSITIONER_GRAVITY_RIGHT;
      placement.offsetX = kGap;
      placement.offsetY = 0;
      placement.chromeAttachment = popup_chrome::Attachment{
          .horizontal = popup_chrome::HorizontalAttachment::Left,
          .vertical = popup_chrome::VerticalAttachment::Center,
      };
      placement.submenuDirection = ContextSubmenuDirection::Right;
      return placement;
    }

    if (bar.position == "right") {
      placement.anchor = XDG_POSITIONER_ANCHOR_LEFT;
      placement.gravity = XDG_POSITIONER_GRAVITY_LEFT;
      placement.offsetX = -kGap;
      placement.offsetY = 0;
      placement.chromeAttachment = popup_chrome::Attachment{
          .horizontal = popup_chrome::HorizontalAttachment::Right,
          .vertical = popup_chrome::VerticalAttachment::Center,
      };
      placement.submenuDirection = ContextSubmenuDirection::Left;
      return placement;
    }

    placement.anchor = XDG_POSITIONER_ANCHOR_BOTTOM;
    placement.gravity = XDG_POSITIONER_GRAVITY_BOTTOM;
    placement.offsetX = 0;
    placement.offsetY = kGap;
    placement.chromeAttachment = popup_chrome::Attachment{
        .horizontal = popup_chrome::HorizontalAttachment::Center,
        .vertical = popup_chrome::VerticalAttachment::Top,
    };
    placement.submenuDirection = ContextSubmenuDirection::Right;
    return placement;
  }

  ShellConfig::ShadowConfig popupShadowConfig(ConfigService* config) {
    return config != nullptr ? config->config().shell.shadow : ShellConfig::ShadowConfig{};
  }

} // namespace
void TrayMenu::initialize(
    WaylandConnection& wayland, ConfigService* config, TrayService* tray, RenderContext* renderContext
) {
  m_wayland = &wayland;
  m_config = config;
  m_tray = tray;
  m_renderContext = renderContext;
}

void TrayMenu::onTrayChanged() {
  if (!m_visible) {
    return;
  }

  auto previousEntries = std::move(m_entries);
  refreshEntries();
  if (m_entries.empty()) {
    close();
    return;
  }
  bool needsRebuild = false;
  // Inline submenus: some servers only fill them after "opened".
  const auto previousSubEntries = m_subEntries;
  refreshExpandedEntries();
  if (m_entries != previousEntries || m_subEntries != previousSubEntries) {
    resizeMainSurfaceToEntries();
    needsRebuild = true;
  }

  // Some dbusmenu providers rebuild submenu children only after the submenu is
  // opened. Refresh visible submenu rows in place so activation uses fresh ids.
  for (std::size_t levelIndex = 0; levelIndex < m_submenuLevels.size(); ++levelIndex) {
    auto& level = m_submenuLevels[levelIndex];
    if (level.instance == nullptr || level.parentEntryId == 0 || m_tray == nullptr) {
      continue;
    }
    const auto refreshedEntries = m_tray->menuEntriesForParent(m_activeItemId, level.parentEntryId);
    if (refreshedEntries.empty() || refreshedEntries == level.entries) {
      continue;
    }
    level.entries = refreshedEntries;
    closeSubmenusFrom(levelIndex + 1);
    needsRebuild = true;
  }

  for (std::size_t levelIndex = 0; levelIndex < m_submenuLevels.size(); ++levelIndex) {
    auto& level = m_submenuLevels[levelIndex];
    if (level.pendingParentEntryId == 0 || level.instance != nullptr) {
      continue;
    }
    const auto parentId = level.pendingParentEntryId;
    const auto rowCenterY = level.pendingRowCenterY;
    level.pendingParentEntryId = 0;
    level.pendingRowCenterY = 0.0F;
    openSubmenuAtLevel(levelIndex, parentId, rowCenterY);
    needsRebuild = true;
    break;
  }

  if (needsRebuild) {
    rebuildScenes();
  }
}

void TrayMenu::toggleForItem(const std::string& itemId, float contentScale) {
  if (itemId.empty()) {
    close();
    return;
  }

  if (m_visible && itemId == m_activeItemId) {
    close();
    return;
  }

  // popup_done fires on press; setOnClick fires on release. By the time we get here
  // the menu is already closed (m_visible = false) even though the user is closing it.
  // Suppress reopening if the same item was dismissed within the last 300 ms.
  if (!m_visible && itemId == m_lastClosedItemId) {
    const auto elapsed = std::chrono::steady_clock::now() - m_lastCloseTime;
    if (elapsed < std::chrono::milliseconds(300)) {
      m_lastClosedItemId.clear();
      return;
    }
  }

  m_activeItemId = itemId;
  m_contentScale = std::max(0.1F, contentScale);
  m_expanded.clear();
  m_subEntries.clear();

  // Some dbusmenu servers only materialize menu rows after receiving "opened".
  // Emit this before the first fetch so we don't render a persistent empty menu.
  if (m_tray != nullptr) {
    m_tray->notifyMenuOpened(m_activeItemId);
  }

  refreshEntries();

  m_visible = true;
  ensureSurface();
  if (m_instance == nullptr || m_instance->surface == nullptr) {
    close();
    return;
  }

  rebuildScenes();
}

void TrayMenu::close() {
  if (!m_visible) {
    return;
  }
  m_lastClosedItemId = m_activeItemId;
  m_lastCloseTime = std::chrono::steady_clock::now();
  m_visible = false;
  // Stop any in-flight retry: continuing to hit GetLayout while the user is
  // spam-clicking the tray is what wedges Electron's dbusmenu handler.
  m_retryTimer.stop();
  closeSubmenu();
  if (m_tray != nullptr && !m_activeItemId.empty()) {
    for (const auto id : m_expanded) {
      m_tray->notifyMenuClosed(m_activeItemId, id);
    }
  }
  m_expanded.clear();
  m_subEntries.clear();
  // Send the "closed" event before tearing down the surface so the server
  // has a chance to reset its internal open-state before the next open.
  if (m_tray != nullptr && !m_activeItemId.empty()) {
    m_tray->notifyMenuClosed(m_activeItemId);
  }
  destroySurface();
  if (m_closedCallback) {
    m_closedCallback();
  }
}

void TrayMenu::setClosedCallback(std::function<void()> callback) { m_closedCallback = std::move(callback); }

void TrayMenu::onThemeChanged() {
  if (!m_visible) {
    return;
  }
  rebuildScenes();
}

void TrayMenu::requestLayout() {
  if (!m_visible) {
    return;
  }
  if (m_instance != nullptr && m_instance->surface != nullptr) {
    m_instance->surface->requestLayout();
  }
  for (auto& level : m_submenuLevels) {
    if (level.instance != nullptr && level.instance->surface != nullptr) {
      level.instance->surface->requestLayout();
    }
  }
}

bool TrayMenu::onKeyboardEvent(const KeyboardEvent& event) {
  if (!m_visible) {
    return false;
  }
  if (event.pressed && !event.preedit && KeybindMatcher::matches(KeybindAction::Cancel, event.sym, event.modifiers)) {
    DeferredCall::callLater([this]() { close(); });
  }
  // The menu holds a modal grab while open — swallow keys so they don't leak.
  return true;
}

bool TrayMenu::onPointerEvent(const PointerEvent& event) {
  if (!m_visible || m_instance == nullptr) {
    return false;
  }

  // Route to top-most submenu first — it holds the active grab when open.
  for (std::size_t idx = m_submenuLevels.size(); idx > 0; --idx) {
    auto& level = m_submenuLevels[idx - 1];
    if (level.instance == nullptr) {
      continue;
    }
    auto* sub = level.instance.get();
    const bool onSub = (event.surface != nullptr && event.surface == sub->wlSurface);
    bool subConsumed = false;

    switch (event.type) {
    case PointerEvent::Type::Enter:
      if (onSub) {
        sub->pointerInside = true;
        sub->inputDispatcher.pointerEnter(static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial);
      }
      break;
    case PointerEvent::Type::Leave:
      if (onSub) {
        sub->pointerInside = false;
        sub->inputDispatcher.pointerLeave();
      }
      break;
    case PointerEvent::Type::Motion:
      if (onSub || sub->pointerInside) {
        if (onSub)
          sub->pointerInside = true;
        sub->inputDispatcher.pointerMotion(static_cast<float>(event.sx), static_cast<float>(event.sy), 0);
        subConsumed = true;
      }
      break;
    case PointerEvent::Type::Button:
      if (onSub || sub->pointerInside) {
        if (onSub)
          sub->pointerInside = true;
        const bool pressed = event.pressed;
        sub->inputDispatcher.pointerButton(
            static_cast<float>(event.sx), static_cast<float>(event.sy), event.button, pressed, event.serial, event.time,
            event.touch
        );
        subConsumed = true;
      }
      break;
    case PointerEvent::Type::Axis:
      if (onSub || sub->pointerInside) {
        if (onSub)
          sub->pointerInside = true;
        subConsumed = sub->inputDispatcher.pointerAxis(
            static_cast<float>(event.sx), static_cast<float>(event.sy), event.axis, event.axisSource, event.axisValue,
            event.axisDiscrete, event.axisValue120, event.axisLines
        );
      }
      break;
    }

    if (sub->surface != nullptr
        && sub->sceneRoot != nullptr
        && (sub->sceneRoot->paintDirty() || sub->sceneRoot->layoutDirty())) {
      if (sub->sceneRoot->layoutDirty()) {
        sub->surface->requestLayout();
      } else {
        sub->surface->requestRedraw();
      }
    }

    if (subConsumed) {
      return true;
    }
  }

  auto* inst = m_instance.get();
  const bool onThisSurface = (event.surface != nullptr && event.surface == inst->wlSurface);
  bool consumed = false;

  switch (event.type) {
  case PointerEvent::Type::Enter:
    if (onThisSurface) {
      inst->pointerInside = true;
      inst->inputDispatcher.pointerEnter(static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial);
    }
    break;
  case PointerEvent::Type::Leave:
    if (onThisSurface) {
      inst->pointerInside = false;
      inst->inputDispatcher.pointerLeave();
    }
    break;
  case PointerEvent::Type::Motion:
    if (onThisSurface || inst->pointerInside) {
      if (onThisSurface) {
        inst->pointerInside = true;
      }
      inst->inputDispatcher.pointerMotion(static_cast<float>(event.sx), static_cast<float>(event.sy), 0);
      consumed = true;
    }
    break;
  case PointerEvent::Type::Button:
    if (onThisSurface || inst->pointerInside) {
      if (onThisSurface) {
        inst->pointerInside = true;
      }
      const bool pressed = event.pressed;
      inst->inputDispatcher.pointerButton(
          static_cast<float>(event.sx), static_cast<float>(event.sy), event.button, pressed, event.serial, event.time,
          event.touch
      );
      consumed = true;
      if (!m_visible || m_instance == nullptr) {
        return consumed;
      }
    }
    break;
  case PointerEvent::Type::Axis:
    if (onThisSurface || inst->pointerInside) {
      if (onThisSurface) {
        inst->pointerInside = true;
      }
      consumed = inst->inputDispatcher.pointerAxis(
          static_cast<float>(event.sx), static_cast<float>(event.sy), event.axis, event.axisSource, event.axisValue,
          event.axisDiscrete, event.axisValue120, event.axisLines
      );
    }
    break;
  }

  if (inst->surface != nullptr
      && inst->sceneRoot != nullptr
      && (inst->sceneRoot->paintDirty() || inst->sceneRoot->layoutDirty())) {
    if (inst->sceneRoot->layoutDirty()) {
      inst->surface->requestLayout();
    } else {
      inst->surface->requestRedraw();
    }
  }

  if (event.type == PointerEvent::Type::Button && event.pressed && !consumed) {
    close();
  }
  return consumed;
}

void TrayMenu::onFontChanged() {
  if (m_instance != nullptr && m_instance->surface != nullptr) {
    m_instance->surface->requestLayout();
  }
  for (auto& level : m_submenuLevels) {
    if (level.instance != nullptr && level.instance->surface != nullptr) {
      level.instance->surface->requestLayout();
    }
  }
}

void TrayMenu::refreshEntries() {
  m_retryTimer.stop();
  m_entries.clear();
  if (m_tray == nullptr || m_activeItemId.empty()) {
    return;
  }
  m_entries = m_tray->menuEntries(m_activeItemId);
  if (!m_entries.empty() && trayDrawerEnabled(m_config)) {
    const bool pinned = activeItemPinned();
    m_entries.insert(
        m_entries.begin(),
        TrayMenuEntry{
            .id = kPinToggleEntryId,
            .label = i18n::tr(pinned ? "tray.menu.unpin" : "tray.menu.pin"),
            .iconName = {},
            .iconData = {},
            .enabled = true,
            .visible = true,
            .separator = false,
            .hasSubmenu = false,
        }
    );
  }
  if (m_entries.empty()) {
    m_entries.push_back(
        TrayMenuEntry{
            .id = -1,
            .label = i18n::tr("tray.menu.empty"),
            .iconName = {},
            .iconData = {},
            .enabled = false,
            .visible = true,
            .separator = false,
            .hasSubmenu = false,
        }
    );
    // Short retry window for apps that need a moment to populate after registration.
    // LayoutUpdated from TrayService will also trigger a refresh via onTrayChanged,
    // so this is just a fallback for servers that don't emit it reliably.
    scheduleEntryRetry(0);
  }
}

void TrayMenu::scheduleEntryRetry(int attempt) {
  // Delays: 300ms, 900ms, 2000ms — total window ~3s. Kept small on purpose:
  // longer retry loops hammer the server while the user is clicking and that is
  // what wedges Electron's dbusmenu handler.
  constexpr int kDelays[] = {300, 900, 2000};
  constexpr int kMaxAttempts = static_cast<int>(sizeof(kDelays) / sizeof(kDelays[0]));
  if (attempt >= kMaxAttempts || m_tray == nullptr) {
    return;
  }
  const auto delay = std::chrono::milliseconds(kDelays[attempt]);
  const std::string capturedItemId = m_activeItemId;
  m_retryTimer.start(delay, [this, attempt, capturedItemId]() {
    // Abort if the menu closed or the user switched tray items — we only retry
    // while the placeholder menu is still visible to the user.
    if (!m_visible || m_tray == nullptr || m_activeItemId != capturedItemId) {
      return;
    }
    auto fresh = m_tray->menuEntries(capturedItemId);
    if (fresh.empty()) {
      scheduleEntryRetry(attempt + 1);
      return;
    }
    kLog.debug("tray menu recovered (attempt {}) for id={}", attempt + 1, capturedItemId);
    m_entries = std::move(fresh);
    if (!m_entries.empty() && trayDrawerEnabled(m_config)) {
      const bool pinned = activeItemPinned();
      m_entries.insert(
          m_entries.begin(),
          TrayMenuEntry{
              .id = kPinToggleEntryId,
              .label = i18n::tr(pinned ? "tray.menu.unpin" : "tray.menu.pin"),
              .iconName = {},
              .iconData = {},
              .enabled = true,
              .visible = true,
              .separator = false,
              .hasSubmenu = false,
          }
      );
    }
    resizeMainSurfaceToEntries();
    rebuildScenes();
  });
}

uint32_t TrayMenu::submenuHeightPx(const std::vector<TrayMenuEntry>& submenuEntries) const {
  std::vector<ContextMenuControlEntry> entries;
  entries.reserve(submenuEntries.size());
  for (const auto& entry : submenuEntries) {
    entries.push_back(
        ContextMenuControlEntry{
            .id = entry.id,
            .label = entry.label,
            .enabled = entry.enabled,
            .separator = entry.separator,
            .hasSubmenu = entry.hasSubmenu,
            .checkmark = entry.checkmark,
            .radio = entry.radio,
            .toggleState = entry.toggleState,
        }
    );
  }
  return static_cast<uint32_t>(
      std::ceil(ContextMenuControl::preferredHeight(entries, visibleEntryLimit(entries.size()), contentScale()))
  );
}

uint32_t TrayMenu::surfaceHeightPx() const {
  // The surface adds room for the shadow around the card.
  const float cardH = std::min(listHeight() + 12.0F, m_maxCardHeight);
  return static_cast<uint32_t>(std::ceil(cardH + kPadTop + kPadBottom));
}

std::vector<TrayMenu::Row> TrayMenu::visibleRows() const {
  std::vector<Row> rows;
  std::function<void(const std::vector<TrayMenuEntry>&, float, int)> add =
      [&](const std::vector<TrayMenuEntry>& entries, float indent, int depth) {
        for (const auto& e : entries) {
          if (!e.visible) {
            continue;
          }
          rows.push_back(Row{.entry = &e, .indent = indent});
          if (e.hasSubmenu && m_expanded.contains(e.id) && depth < 8) {
            if (const auto it = m_subEntries.find(e.id); it != m_subEntries.end()) {
              add(it->second, indent + kSubIndent, depth + 1);
            }
          }
        }
      };
  add(m_entries, 0.0F, 0);
  return rows;
}

float TrayMenu::listHeight() const {
  float h = 0.0F;
  for (const auto& row : visibleRows()) {
    h += row.entry->separator ? kSeparatorH : kRowH;
  }
  return h;
}

void TrayMenu::toggleExpanded(std::int32_t entryId) {
  if (!m_visible || m_tray == nullptr) {
    return;
  }
  if (m_expanded.erase(entryId) > 0) {
    m_tray->notifyMenuClosed(m_activeItemId, entryId);
  } else {
    m_expanded.insert(entryId);
    // Some dbusmenu servers fill a submenu only once it's "opened"; onTrayChanged() picks it up then.
    m_tray->notifyMenuOpened(m_activeItemId, entryId);
    m_subEntries[entryId] = m_tray->menuEntriesForParent(m_activeItemId, entryId);
  }
  resizeMainSurfaceToEntries();
  rebuildScenes();
}

void TrayMenu::refreshExpandedEntries() {
  if (m_tray == nullptr) {
    return;
  }
  for (const auto id : m_expanded) {
    auto fresh = m_tray->menuEntriesForParent(m_activeItemId, id);
    if (!fresh.empty()) {
      m_subEntries[id] = std::move(fresh);
    }
  }
}

bool TrayMenu::ownsSurface(wl_surface* surface) const {
  return m_instance != nullptr && surface != nullptr && m_instance->wlSurface == surface;
}

float TrayMenu::contentScale() const noexcept { return std::max(0.1F, m_contentScale); }

float TrayMenu::menuWidth() const noexcept { return kCardW; }

void TrayMenu::rememberIcon(const std::string& itemId, InputArea* area, const void* owner) {
  if (area == nullptr) {
    return;
  }
  iconRegistry().push_back(IconRef{.itemId = itemId, .area = area, .owner = owner});
}

void TrayMenu::forgetIcons(const void* owner) {
  std::erase_if(iconRegistry(), [owner](const IconRef& r) { return r.owner == owner; });
  anchorHint() = nullptr;
}

void TrayMenu::setAnchorHint(InputArea* area) { anchorHint() = area; }

void TrayMenu::toggleForIndex(std::size_t index) {
  if (m_tray == nullptr) {
    return;
  }
  const auto items = m_tray->items();
  if (index >= items.size()) {
    return;
  }
  // No pointer here: the menu opens under the remembered icon (see ensureSurface).
  m_lastClosedItemId.clear();
  toggleForItem(items[index].id, m_contentScale);
}

PopupSurfaceConfig TrayMenu::popupConfigFor(float cardH) const {
  // The card hangs from m_cardRefX/Y (parent surface coords): its top centre for a top bar, bottom centre
  // for a bottom bar, left middle for a left bar and right middle for a right bar.
  float cardX = m_cardRefX - kCardW / 2.0F;
  float cardY = m_cardRefY;
  if (m_edge == "bottom") {
    cardY = m_cardRefY - cardH;
  } else if (m_edge == "left") {
    cardX = m_cardRefX;
    cardY = m_cardRefY - cardH / 2.0F;
  } else if (m_edge == "right") {
    cardX = m_cardRefX - kCardW;
    cardY = m_cardRefY - cardH / 2.0F;
  }
  return PopupSurfaceConfig{
      .anchorX = static_cast<std::int32_t>(std::round(cardX - kPadX)),
      .anchorY = static_cast<std::int32_t>(std::round(cardY - kPadTop)),
      .anchorWidth = 1,
      .anchorHeight = 1,
      .width = static_cast<std::uint32_t>(kCardW + 2.0F * kPadX),
      .height = static_cast<std::uint32_t>(std::ceil(cardH + kPadTop + kPadBottom)),
      .anchor = XDG_POSITIONER_ANCHOR_TOP_LEFT,
      .gravity = XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT,
      // Keeps the card on screen; the surface's side padding keeps it 8 px from the edges.
      .constraintAdjustment = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y,
  };
}

void TrayMenu::ensureSurface() {
  if (m_instance != nullptr) {
    return;
  }
  if (m_wayland == nullptr || m_renderContext == nullptr) {
    return;
  }

  // The icon just clicked, else the one the tray widget remembered for this item (IPC or keybind).
  InputArea* icon = std::exchange(anchorHint(), nullptr);
  if (icon == nullptr) {
    for (const auto& ref : iconRegistry()) {
      if (ref.itemId == m_activeItemId) {
        icon = ref.area;
        break;
      }
    }
  }
  std::optional<std::pair<LayerPopupParentContext, std::string>> parent;
  if (icon != nullptr && m_parentResolver) {
    parent = m_parentResolver(icon);
  }
  float iconCx = 0.0F;
  float iconCy = 0.0F;
  if (parent.has_value()) {
    float ax = 0.0F;
    float ay = 0.0F;
    Node::absolutePosition(icon, ax, ay);
    iconCx = ax + icon->width() / 2.0F;
    iconCy = ay + icon->height() / 2.0F;
  } else {
    // Fall back to the surface under the pointer (the tray drawer panel).
    wl_surface* pointerSurface = m_wayland->lastPointerSurface();
    auto* layer = m_wayland->layerSurfaceFor(pointerSurface);
    wl_output* output = m_wayland->lastPointerOutput();
    if (layer == nullptr || output == nullptr || !m_wayland->hasPointerPosition()) {
      kLog.debug("tray menu: no anchor (icon={}, pointer surface={})", icon != nullptr, layer != nullptr);
      return;
    }
    const auto bar = resolveTrayBarConfig(m_config, m_wayland, output);
    parent = std::make_pair(
        LayerPopupParentContext{.surface = pointerSurface, .layerSurface = layer, .output = output},
        bar.has_value() ? bar->position : std::string("top")
    );
    iconCx = static_cast<float>(m_wayland->lastPointerX());
    iconCy = static_cast<float>(m_wayland->lastPointerY());
  }
  const LayerPopupParentContext ctx = parent->first;
  m_edge = parent->second;
  wl_output* output = ctx.output;

  // The card sits 6 px past the bar's full thickness (size plus margins). Measured from the icon's centre,
  // that is half the bar, its inner margin, then 6.
  float barSize = 28.0F;
  if (const auto bar = resolveTrayBarConfig(m_config, m_wayland, output); bar.has_value()) {
    barSize = static_cast<float>(bar->thickness);
  }
  const float reach = barSize / 2.0F + barInnerMargin() + 6.0F;
  m_cardRefX = iconCx;
  m_cardRefY = iconCy + reach;
  if (m_edge == "bottom") {
    m_cardRefY = iconCy - reach;
  } else if (m_edge == "left") {
    m_cardRefX = iconCx + reach;
    m_cardRefY = iconCy;
  } else if (m_edge == "right") {
    m_cardRefX = iconCx - reach;
    m_cardRefY = iconCy;
  }
  const WaylandOutput* wlOutput = m_wayland->findOutputByWl(output);
  const float outputH = wlOutput != nullptr && wlOutput->effectiveLogicalHeight() > 0
      ? static_cast<float>(wlOutput->effectiveLogicalHeight())
      : 1080.0F;
  m_maxCardHeight = std::max(60.0F, outputH - 80.0F);

  auto inst = std::make_unique<MenuInstance>();
  inst->output = output;
  inst->surface = std::make_unique<PopupSurface>(*m_wayland);
  inst->surface->setRenderContext(m_renderContext);
  inst->surface->setAnimationManager(&inst->animations);
  auto* instPtr = inst.get();

  inst->surface->setConfigureCallback([instPtr](uint32_t /*width*/, uint32_t /*height*/) {
    instPtr->surface->requestLayout();
  });
  inst->surface->setPrepareFrameCallback([this, instPtr](bool needsUpdate, bool needsLayout) {
    prepareMainMenuFrame(*instPtr, needsUpdate, needsLayout);
  });
  inst->surface->setDismissedCallback([this]() { close(); });

  // On Hyprland, hyprland_focus_grab_v1 conflicts with xdg_popup_grab — the
  // compositor sends popup_done shortly after we commit our focus grab,
  // tearing the popup down. Skip xdg_popup_grab when we'll be using
  // focus_grab; outside-click dismissal is handled by the focus grab's
  // `cleared` event instead.
  auto* grabService = m_wayland->focusGrabService();
  const bool useFocusGrab = grabService != nullptr && grabService->available();
  const std::uint32_t serial = m_wayland->lastInputSerial();
  const float cardH = static_cast<float>(surfaceHeightPx()) - kPadTop - kPadBottom;
  auto popupConfig = popupConfigFor(cardH);
  popupConfig.serial = serial;
  popupConfig.grab = !useFocusGrab && serial != 0;

  // Layer-shell popups inherit their parent's keyboard interactivity. A bar is
  // None, so without this the grabbing popup would get no keyboard focus and ESC
  // could not reach it. Flip a None parent to OnDemand before the popup maps; a
  // parent that already takes keyboard (the tray drawer panel) is left alone. The
  // focus-grab path carries keyboard itself, so only the plain grab path needs it.
  const LayerSurface* parentOwner = m_wayland->layerSurfaceOwnerFor(ctx.surface);
  if (popupConfig.grab && parentOwner != nullptr && parentOwner->keyboardInteractivity() == LayerShellKeyboard::None) {
    m_keyboardBarLayerSurface = ctx.layerSurface;
    m_keyboardBarWlSurface = ctx.surface;
    zwlr_layer_surface_v1_set_keyboard_interactivity(
        ctx.layerSurface, static_cast<std::uint32_t>(LayerShellKeyboard::OnDemand)
    );
    wl_surface_commit(ctx.surface);
  }

  if (!inst->surface->initialize(ctx.layerSurface, output, popupConfig)) {
    kLog.debug("tray menu: failed to create popup surface");
    restoreBarKeyboardInteractivity();
    return;
  }

  // Only the card takes the pointer.
  inst->surface->setInputRegion({InputRect{
      static_cast<int>(kPadX), static_cast<int>(kPadTop), static_cast<int>(kCardW), static_cast<int>(std::ceil(cardH))
  }});
  inst->wlSurface = inst->surface->wlSurface();
  m_instance = std::move(inst);

  // Hyprland: without an active focus grab covering the popup, the compositor
  // only delivers pointer events after a click transfers focus from the bar's
  // OnDemand layer surface, so hover never reaches the popup. Whitelisting the
  // popup (and the parent bar so re-clicking other tray icons keeps working)
  // eagerly routes motion to the popup. Defer to the next tick — Hyprland's
  // focus_grab needs the whitelisted surfaces to be mapped, which only happens
  // after the configure round-trip completes; committing the grab synchronously
  // here makes the compositor fire `cleared` immediately and the menu never
  // appears.
  DeferredCall::callLater([this]() {
    if (!m_visible || m_instance == nullptr || m_wayland == nullptr) {
      return;
    }
    auto* svc = m_wayland->focusGrabService();
    if (svc == nullptr || !svc->available()) {
      return;
    }

    // When the menu is opened from an attached panel (e.g. the tray drawer),
    // this popup is already enrolled into the panel's existing focus grab.
    // Creating another grab here would clear the panel grab, dismissing the
    // parent panel.
    if (svc->popupGrabHost() != nullptr) {
      return;
    }

    m_focusGrab = svc->createGrab();
    if (m_focusGrab == nullptr) {
      return;
    }
    m_focusGrab->setOnCleared([this]() {
      if (m_visible) {
        close();
      }
    });
    m_focusGrab->addSurface(m_instance->wlSurface);
    m_focusGrab->commit();
  });
}

void TrayMenu::resizeMainSurfaceToEntries() {
  if (m_instance == nullptr || m_instance->surface == nullptr) {
    return;
  }
  const auto desiredHeight = surfaceHeightPx();
  if (m_instance->surface->height() == desiredHeight) {
    return;
  }
  const float cardH = static_cast<float>(desiredHeight) - kPadTop - kPadBottom;
  m_instance->surface->setInputRegion({InputRect{
      static_cast<int>(kPadX), static_cast<int>(kPadTop), static_cast<int>(kCardW), static_cast<int>(std::ceil(cardH))
  }});
  // On a bottom or side bar the card's far edge (or middle) stays put, so move the popup as it grows.
  auto config = popupConfigFor(cardH);
  config.width = m_instance->surface->width();
  config.height = desiredHeight;
  m_instance->surface->resize(m_instance->surface->width(), desiredHeight, false);
  if (!m_instance->surface->repositionAnchor(config)) {
    m_instance->surface->requestLayout();
  }
}

void TrayMenu::destroySurface() {
  if (m_instance != nullptr) {
    m_instance->inputDispatcher.setSceneRoot(nullptr);
  }
  m_instance.reset();
  m_focusGrab.reset();
  restoreBarKeyboardInteractivity();
}

void TrayMenu::restoreBarKeyboardInteractivity() {
  zwlr_layer_surface_v1* layerSurface = m_keyboardBarLayerSurface;
  wl_surface* wlSurface = m_keyboardBarWlSurface;
  m_keyboardBarLayerSurface = nullptr;
  m_keyboardBarWlSurface = nullptr;
  // The parent (a bar or the tray drawer panel) can be destroyed while the menu
  // is open. Its proxies are then freed, so only touch it while still registered.
  if (layerSurface == nullptr || m_wayland == nullptr || m_wayland->layerSurfaceFor(wlSurface) != layerSurface) {
    return;
  }
  zwlr_layer_surface_v1_set_keyboard_interactivity(layerSurface, static_cast<std::uint32_t>(LayerShellKeyboard::None));
  wl_surface_commit(wlSurface);
}

void TrayMenu::rebuildScenes() {
  uiAssertNotRendering("TrayMenu::rebuildScenes");
  if (!m_visible) {
    return;
  }
  if (!m_entries.empty() && m_instance != nullptr && m_instance->surface != nullptr) {
    m_instance->surface->requestLayout();
  }
  for (auto& level : m_submenuLevels) {
    if (!level.entries.empty() && level.instance != nullptr && level.instance->surface != nullptr) {
      level.instance->surface->requestLayout();
    }
  }
}

void TrayMenu::prepareMainMenuFrame(MenuInstance& inst, bool /*needsUpdate*/, bool needsLayout) {
  if (m_renderContext == nullptr || inst.surface == nullptr) {
    return;
  }

  const auto width = inst.surface->width();
  const auto height = inst.surface->height();
  if (width == 0 || height == 0) {
    return;
  }

  m_renderContext->makeCurrent(inst.surface->renderTarget());

  const bool needsSceneBuild = inst.sceneRoot == nullptr
      || static_cast<uint32_t>(std::round(inst.sceneRoot->width())) != width
      || static_cast<uint32_t>(std::round(inst.sceneRoot->height())) != height;
  if (needsSceneBuild || needsLayout) {
    UiPhaseScope layoutPhase(UiPhase::Layout);
    buildScene(inst, width, height);
  }

  if (!inst.animated) {
    // Springs out of the bar: a quick fade with a slightly overshooting scale.
    inst.animated = true;
    MenuInstance* instPtr = &inst;
    inst.animations.animate(0.0F, 1.0F, 160.0F, Easing::Linear, [this, instPtr](float v) {
      instPtr->open = v;
      applyCardMotion(*instPtr);
    });
    const float overshoot = kusanagi::bounce(1.6F);
    inst.animations.animate(0.0F, 1.0F, 300.0F, Easing::Linear, [this, instPtr, overshoot](float t) {
      const float u = t - 1.0F;
      instPtr->scaleT = u * u * ((overshoot + 1.0F) * u + overshoot) + 1.0F;
      applyCardMotion(*instPtr);
    });
  }
}

void TrayMenu::applyCardMotion(MenuInstance& inst) {
  if (inst.card == nullptr) {
    return;
  }
  inst.card->setOpacity(std::clamp(inst.open, 0.0F, 1.0F));
  inst.card->setScale(kHiddenScale + (1.0F - kHiddenScale) * inst.scaleT);
  if (inst.surface != nullptr) {
    inst.surface->requestRedraw();
  }
}

void TrayMenu::buildScene(MenuInstance& inst, uint32_t width, uint32_t height) {
  uiAssertNotRendering("TrayMenu::buildScene");
  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);
  Renderer& renderer = inst.surface->renderTarget().renderer();

  const float cardH = std::max(1.0F, h - kPadTop - kPadBottom);
  const float radius = std::max(8.0F, kusanagi::radius() - 4.0F);
  const float panelOpacity = static_cast<float>(kusanagi::opt<double>("panel", "opacity", 0.94));
  const std::string font = kusanagi::font();

  inst.sceneRoot = ui::node({});
  inst.sceneRoot->setSize(w, h);

  auto card = ui::node({.out = &inst.card});
  card->setPosition(kPadX, kPadTop);
  card->setSize(kCardW, cardH);
  // Scale from the side facing the bar.
  if (m_edge == "bottom") {
    card->setTransformOrigin(kCardW / 2.0F, cardH);
  } else if (m_edge == "left") {
    card->setTransformOrigin(0.0F, cardH / 2.0F);
  } else if (m_edge == "right") {
    card->setTransformOrigin(kCardW, cardH / 2.0F);
  } else {
    card->setTransformOrigin(kCardW / 2.0F, 0.0F);
  }

  if (kusanagi::shadows()) {
    auto shadow = ui::box({});
    shadow->setStyle(RoundedRectStyle{
        .fill = rgba(0.0F, 0.0F, 0.0F, 0.4F),
        .softness = 15.0F,
        .outerShadow = true,
        .shadowCutoutOffsetY = 6.0F,
    });
    shadow->setRadius(radius);
    shadow->setSize(kCardW, cardH);
    shadow->setPosition(0.0F, 6.0F);
    card->addChild(std::move(shadow));
  }
  card->addChild(ui::box({
      .fill = colorSpecFromRole(ColorRole::Surface, std::max(0.9F, panelOpacity)),
      .border = kusanagi::surfaceBorder(),
      .borderWidth = kusanagi::surfaceBorderWidth(),
      .radius = radius,
      .width = kCardW,
      .height = cardH,
  }));

  const float listW = kCardW - 12.0F;
  const float viewH = std::max(1.0F, cardH - 12.0F);
  auto scrollView = ui::scrollView({
      .state = &inst.scrollState,
      .scrollbarVisible = true,
      .viewportPaddingH = 0.0F,
      .viewportPaddingV = 0.0F,
      .radius = 0.0F,
      .width = listW,
      .height = viewH,
      .configure = [](ScrollView& view) {
        view.setPosition(6.0F, 6.0F);
        view.clearFill();
        view.clearBorder();
      },
  });

  // Rows
  const auto rows = visibleRows();
  auto list = ui::node({});
  float y = 0.0F;
  for (const auto& row : rows) {
    const TrayMenuEntry& e = *row.entry;
    const float rowW = listW - row.indent;
    if (e.separator) {
      auto line = ui::box({.fill = colorSpecFromRole(ColorRole::OnSurface, 0.08F), .width = rowW - 16.0F, .height = 1.0F});
      line->setPosition(row.indent + 8.0F, y + 4.0F);
      list->addChild(std::move(line));
      y += kSeparatorH;
      continue;
    }
    const bool enabled = e.enabled;
    const bool expanded = e.hasSubmenu && m_expanded.contains(e.id);
    Box* hover = nullptr;
    auto area = ui::inputArea({
        .acceptedButtons = InputArea::buttonMask({BTN_LEFT, BTN_RIGHT}),
        .cursorShape = enabled ? std::optional<std::uint32_t>(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER) : std::nullopt,
        .width = rowW,
        .height = kRowH,
    });
    area->setPosition(row.indent, y);
    area->setOpacity(enabled ? 1.0F : 0.4F);
    area->addChild(ui::box({.out = &hover, .fill = clearColorSpec(), .radius = 8.0F, .width = rowW, .height = kRowH}));

    // Check box or radio dot, else the entry's icon, or a gap to keep the text aligned.
    const bool toggle = e.checkmark || e.radio;
    if (toggle) {
      const bool checked = e.toggleState == 1;
      auto box = ui::box({
          .fill = checked ? colorSpecFromRole(ColorRole::Primary) : clearColorSpec(),
          .border = colorSpecFromRole(ColorRole::OnSurface, 0.4F),
          .borderWidth = checked ? 0.0F : 1.5F,
          .radius = e.radio ? 8.0F : 4.0F,
          .width = 16.0F,
          .height = 16.0F,
      });
      box->setPosition(10.0F, (kRowH - 16.0F) / 2.0F);
      area->addChild(std::move(box));
      if (checked) {
        Label* mark = nullptr;
        area->addChild(ui::label({
            .out = &mark,
            .text = utf8(e.radio ? 0xf0765 : 0xf012c),
            .fontSize = e.radio ? 7.0F : 12.0F,
            .fontFamily = std::string(kIconFont),
            .color = colorSpecFromRole(ColorRole::Surface),
            .maxLines = 1,
            .baselineMode = LabelBaselineMode::FontLine,
        }));
        mark->measure(renderer);
        mark->setPosition(
            10.0F + std::round((16.0F - mark->width()) / 2.0F),
            std::round((kRowH - 16.0F) / 2.0F) + std::floor((16.0F - mark->height()) / 2.0F)
        );
      }
    } else if (!e.iconData.empty() || !e.iconName.empty()) {
      Image* img = nullptr;
      area->addChild(ui::image({.out = &img, .fit = ImageFit::Contain, .width = 16.0F, .height = 16.0F}));
      img->setPosition(10.0F, (kRowH - 16.0F) / 2.0F);
      bool ok = false;
      if (!e.iconData.empty()) {
        ok = img->setSourceBytes(renderer, e.iconData.data(), e.iconData.size());
      }
      if (!ok && !e.iconName.empty()) {
        const std::string& path = m_iconResolver.resolve(e.iconName, 16);
        ok = !path.empty() && img->setSourceFile(renderer, path, 16);
      }
      img->setVisible(ok);
    }

    Label* text = nullptr;
    area->addChild(ui::label({
        .out = &text,
        .text = e.label.empty() ? iconNameToLabel(e.iconName) : e.label,
        .fontSize = 12.0F,
        .fontFamily = font,
        .color = colorSpecFromRole(ColorRole::OnSurface),
        .maxWidth = 228.0F - 10.0F - 16.0F - 10.0F - 30.0F,
        .maxLines = 1,
        .ellipsize = TextEllipsize::End,
        .baselineMode = LabelBaselineMode::FontLine,
    }));
    text->measure(renderer);
    text->setPosition(36.0F, std::floor((kRowH - text->height()) / 2.0F));

    if (e.hasSubmenu) {
      Label* chevron = nullptr;
      area->addChild(ui::label({
          .out = &chevron,
          .text = utf8(0xf0140),
          .fontSize = 14.0F,
          .fontFamily = std::string(kIconFont),
          .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
          .maxLines = 1,
          .baselineMode = LabelBaselineMode::FontLine,
      }));
      chevron->measure(renderer);
      chevron->setPosition(rowW - 10.0F - chevron->width(), std::floor((kRowH - chevron->height()) / 2.0F));
      chevron->setRotation(expanded ? 0.0F : -1.5707964F);
    }

    if (enabled) {
      area->setOnEnter([hover](const InputArea::PointerData&) {
        hover->setFill(colorSpecFromRole(ColorRole::Primary, 0.16F));
      });
      area->setOnLeave([hover]() { hover->setFill(clearColorSpec()); });
      const std::int32_t id = e.id;
      const bool submenu = e.hasSubmenu;
      area->setOnClick([this, id, submenu](const InputArea::PointerData&) {
        if (submenu) {
          DeferredCall::callLater([this, id]() { toggleExpanded(id); });
          return;
        }
        if (id == kPinToggleEntryId) {
          DeferredCall::callLater([this]() {
            (void)toggleActiveItemPinned();
            close();
            closeTrayDrawerPanelIfOpen();
          });
          return;
        }
        if (m_tray == nullptr || m_activeItemId.empty()) {
          return;
        }
        DeferredCall::callLater([this, id]() {
          if (m_tray != nullptr) {
            (void)m_tray->activateMenuEntry(m_activeItemId, id);
          }
          close();
          closeTrayDrawerPanelIfOpen();
        });
      });
    }
    list->addChild(std::move(area));
    y += kRowH;
  }
  list->setSize(listW, std::max(1.0F, y));
  scrollView->content()->addChild(std::move(list));
  scrollView->layout(renderer);
  card->addChild(std::move(scrollView));
  inst.sceneRoot->addChild(std::move(card));
  applyCardMotion(inst);

  inst.inputDispatcher.setSceneRoot(inst.sceneRoot.get());
  inst.inputDispatcher.setCursorShapeCallback([this](uint32_t serial, uint32_t shape) {
    m_wayland->setCursorShape(serial, shape);
  });
  inst.surface->setSceneRoot(inst.sceneRoot.get());
}

std::optional<TrayItemInfo> TrayMenu::activeTrayItem() const {
  if (m_tray == nullptr || m_activeItemId.empty()) {
    return std::nullopt;
  }
  const auto allItems = m_tray->items();
  const auto it = std::ranges::find(allItems, m_activeItemId, &TrayItemInfo::id);
  if (it == allItems.end()) {
    return std::nullopt;
  }
  return *it;
}

bool TrayMenu::activeItemPinned() const {
  if (m_config == nullptr) {
    return false;
  }
  const auto item = activeTrayItem();
  if (!item.has_value()) {
    return false;
  }
  const auto resolved = tray::resolvedTrayOptions(*m_config);
  for (const auto& token : resolved.options.pinnedItems) {
    if (tray::tokenMatchesItem(token, *item)) {
      return true;
    }
  }
  return false;
}

bool TrayMenu::toggleActiveItemPinned() {
  if (m_config == nullptr) {
    return false;
  }
  const auto item = activeTrayItem();
  if (!item.has_value()) {
    return false;
  }

  std::vector<std::string> pinned = tray::resolvedTrayOptions(*m_config).options.pinnedItems;
  std::erase_if(pinned, [](const std::string& token) {
    return tray::looksGenericStatusItemName(token) || tray::isTransientUniqueIdentifier(token);
  });
  const bool currentlyPinned =
      std::ranges::any_of(pinned, [&](const std::string& token) { return tray::tokenMatchesItem(token, *item); });

  if (currentlyPinned) {
    std::erase_if(pinned, [&](const std::string& token) { return tray::tokenMatchesItem(token, *item); });
    kLog.info(
        "tray pin removed token for id={} itemName='{}' title='{}' sniTitle='{}' icon='{}' process='{}' "
        "bus='{}'",
        item->id, item->itemName, item->title, item->statusNotifierTitle, item->iconName, item->processName,
        item->busName
    );
  } else {
    std::string token = tray::preferredPinToken(*item);
    if (token.empty()) {
      kLog.info(
          "tray pin skipped: no stable token for id={} itemName='{}' title='{}' sniTitle='{}' icon='{}' "
          "process='{}' bus='{}' objectPath='{}'",
          item->id, item->itemName, item->title, item->statusNotifierTitle, item->iconName, item->processName,
          item->busName, item->objectPath
      );
      return false;
    }
    kLog.info(
        "tray pin added token='{}' for id={} itemName='{}' title='{}' sniTitle='{}' icon='{}' process='{}' "
        "bus='{}'",
        token, item->id, item->itemName, item->title, item->statusNotifierTitle, item->iconName, item->processName,
        item->busName
    );
    pinned.push_back(token);
  }

  return m_config->setOverride({"widget", std::string(tray::kCanonicalTrayWidgetName), "pinned"}, pinned);
}

void TrayMenu::closeSubmenu() { closeSubmenusFrom(0); }

void TrayMenu::openSubmenu(std::int32_t parentEntryId, float rowCenterY) {
  openSubmenuAtLevel(0, parentEntryId, rowCenterY);
}

void TrayMenu::closeSubmenusFrom(std::size_t levelIndex) {
  if (levelIndex >= m_submenuLevels.size()) {
    return;
  }

  for (std::size_t idx = m_submenuLevels.size(); idx > levelIndex; --idx) {
    auto& level = m_submenuLevels[idx - 1];
    if (level.instance != nullptr) {
      level.instance->inputDispatcher.setSceneRoot(nullptr);
      if (m_focusGrab != nullptr && level.instance->wlSurface != nullptr) {
        m_focusGrab->removeSurface(level.instance->wlSurface);
        m_focusGrab->commit();
      }
    }
    if (m_tray != nullptr && !m_activeItemId.empty() && level.parentEntryId != 0) {
      m_tray->notifyMenuClosed(m_activeItemId, level.parentEntryId);
    }
    level.instance.reset();
    level.entries.clear();
    level.parentEntryId = 0;
    level.pendingParentEntryId = 0;
    level.pendingRowCenterY = 0.0F;
  }
}

void TrayMenu::openSubmenuAtLevel(std::size_t levelIndex, std::int32_t parentEntryId, float rowCenterY) {
  closeSubmenusFrom(levelIndex);

  if (m_tray == nullptr) {
    return;
  }
  MenuInstance* parentMenu = nullptr;
  if (levelIndex == 0) {
    if (m_instance == nullptr || m_instance->surface == nullptr) {
      return;
    }
    parentMenu = m_instance.get();
  } else {
    if (levelIndex > m_submenuLevels.size()) {
      return;
    }
    auto& parentLevel = m_submenuLevels[levelIndex - 1];
    if (parentLevel.instance == nullptr || parentLevel.instance->surface == nullptr) {
      return;
    }
    parentMenu = parentLevel.instance.get();
  }

  if (m_submenuLevels.size() <= levelIndex) {
    m_submenuLevels.resize(levelIndex + 1);
  }
  auto& level = m_submenuLevels[levelIndex];

  level.entries = m_tray->menuEntriesForParent(m_activeItemId, parentEntryId);
  if (level.entries.empty()) {
    level.pendingParentEntryId = parentEntryId;
    level.pendingRowCenterY = rowCenterY;
    return;
  }
  level.pendingParentEntryId = 0;
  level.pendingRowCenterY = 0.0F;
  level.parentEntryId = parentEntryId;
  m_tray->notifyMenuOpened(m_activeItemId, parentEntryId);

  const auto parentContentX = static_cast<std::int32_t>(std::lround(parentMenu->chrome.contentX()));
  const auto parentWidth = static_cast<std::int32_t>(std::lround(parentMenu->chrome.contentWidth));
  const auto parentX = parentMenu->surface->configuredX() + parentContentX;
  const float scale = contentScale();
  const auto rowTop = static_cast<std::int32_t>(std::lround(rowCenterY - Style::controlHeightSm * scale * 0.5F));
  const auto rowH = std::max(1, static_cast<std::int32_t>(std::lround(Style::controlHeightSm * scale)));
  const auto subGap = std::max(1, static_cast<std::int32_t>(std::lround(4.0F * scale)));

  const auto chrome = popup_chrome::computeGeometry(
      menuWidth(), static_cast<float>(submenuHeightPx(level.entries)), popupShadowConfig(m_config),
      Style::popupShadowsEnabled()
  );

  const auto* wlOutput = m_wayland->findOutputByWl(parentMenu->output);
  const std::int32_t outputWidth = (wlOutput != nullptr && wlOutput->effectiveLogicalWidth() > 0)
      ? wlOutput->effectiveLogicalWidth()
      : static_cast<std::int32_t>(chrome.surfaceWidth);

  bool isRight = (parentMenu->submenuDirection == ContextSubmenuDirection::Right);
  const std::int32_t submenuExtent = static_cast<std::int32_t>(chrome.surfaceWidth) + subGap;
  if (isRight) {
    if (parentX + parentWidth + submenuExtent > outputWidth) {
      isRight = false;
    }
  } else {
    if (parentX - submenuExtent < 0) {
      isRight = true;
    }
  }

  const std::int32_t anchorX = isRight ? parentContentX + parentWidth : parentContentX;
  const std::int32_t anchorY = static_cast<std::int32_t>(std::lround(parentMenu->chrome.contentY())) + rowTop;
  const std::uint32_t anchor = isRight ? XDG_POSITIONER_ANCHOR_TOP_RIGHT : XDG_POSITIONER_ANCHOR_TOP_LEFT;
  const std::uint32_t gravity = isRight ? XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT : XDG_POSITIONER_GRAVITY_BOTTOM_LEFT;
  const std::int32_t offsetX = isRight ? subGap : -subGap;
  const auto subDir = isRight ? ContextSubmenuDirection::Right : ContextSubmenuDirection::Left;
  const auto chromeAttachment = isRight
                                    ? popup_chrome::Attachment{
                                          .horizontal = popup_chrome::HorizontalAttachment::Left,
                                          .vertical = popup_chrome::VerticalAttachment::Top,
                                      }
                                    : popup_chrome::Attachment{
                                          .horizontal = popup_chrome::HorizontalAttachment::Right,
                                          .vertical = popup_chrome::VerticalAttachment::Top,
                                      };

  auto inst = std::make_unique<MenuInstance>();
  inst->output = parentMenu->output;
  inst->surface = std::make_unique<PopupSurface>(*m_wayland);
  inst->surface->setRenderContext(m_renderContext);
  inst->submenuDirection = subDir;
  inst->chrome = chrome;
  auto* instPtr = inst.get();

  inst->surface->setConfigureCallback([instPtr](uint32_t /*w*/, uint32_t /*h*/) { instPtr->surface->requestLayout(); });
  inst->surface->setPrepareFrameCallback([this, levelIndex, instPtr](bool needsUpdate, bool needsLayout) {
    prepareSubmenuFrame(levelIndex, *instPtr, needsUpdate, needsLayout);
  });
  inst->surface->setDismissedCallback([this, levelIndex]() { closeSubmenusFrom(levelIndex); });

  auto popupConfig = PopupSurfaceConfig{
      .anchorX = anchorX,
      .anchorY = anchorY,
      .anchorWidth = 1,
      .anchorHeight = rowH,
      .width = chrome.surfaceWidth,
      .height = chrome.surfaceHeight,
      .anchor = anchor,
      .gravity = gravity,
      .constraintAdjustment = kPopupConstraintAdjust,
      .offsetX = offsetX,
      .offsetY = 0,
      .serial = m_wayland->lastInputSerial(),
      .grab = (m_focusGrab == nullptr),
  };
  popup_chrome::applyToConfig(popupConfig, chrome, chromeAttachment);

  xdg_surface* parentXdg = parentMenu->surface->xdgSurface();
  if (!inst->surface->initializeAsChild(parentXdg, parentMenu->output, popupConfig)) {
    kLog.debug("tray submenu: failed to create child popup surface");
    level.entries.clear();
    level.parentEntryId = 0;
    return;
  }

  popup_chrome::setContentInputRegion(*inst->surface, inst->chrome);
  inst->wlSurface = inst->surface->wlSurface();
  level.instance = std::move(inst);

  if (m_focusGrab != nullptr && level.instance->wlSurface != nullptr) {
    m_focusGrab->addSurface(level.instance->wlSurface);
    m_focusGrab->commit();
  }
}

void TrayMenu::prepareSubmenuFrame(std::size_t levelIndex, MenuInstance& inst, bool /*needsUpdate*/, bool needsLayout) {
  if (m_renderContext == nullptr || inst.surface == nullptr) {
    return;
  }

  const auto width = inst.surface->width();
  const auto height = inst.surface->height();
  if (width == 0 || height == 0) {
    return;
  }

  m_renderContext->makeCurrent(inst.surface->renderTarget());

  const bool needsSceneBuild = inst.sceneRoot == nullptr
      || static_cast<uint32_t>(std::round(inst.sceneRoot->width())) != width
      || static_cast<uint32_t>(std::round(inst.sceneRoot->height())) != height;
  if (needsSceneBuild || needsLayout) {
    UiPhaseScope layoutPhase(UiPhase::Layout);
    buildSubmenuScene(levelIndex, inst, width, height);
  }
}

void TrayMenu::buildSubmenuScene(std::size_t levelIndex, MenuInstance& inst, uint32_t width, uint32_t height) {
  uiAssertNotRendering("TrayMenu::buildSubmenuScene");
  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);

  inst.sceneRoot = ui::node({});
  inst.sceneRoot->setSize(w, h);
  if (Style::popupShadowsEnabled()) {
    (void)popup_chrome::addShadow(
        *inst.sceneRoot, inst.chrome, popupShadowConfig(m_config), Style::scaledRadiusLg(contentScale())
    );
  }
  (void)popup_chrome::addCardBackground(*inst.sceneRoot, inst.chrome, contentScale());

  if (levelIndex >= m_submenuLevels.size()) {
    return;
  }
  std::vector<ContextMenuControlEntry> entries;
  const auto& submenuEntries = m_submenuLevels[levelIndex].entries;
  entries.reserve(submenuEntries.size());
  for (const auto& entry : submenuEntries) {
    entries.push_back(
        ContextMenuControlEntry{
            .id = entry.id,
            .label = entry.label,
            .enabled = entry.enabled,
            .separator = entry.separator,
            .hasSubmenu = entry.hasSubmenu,
            .checkmark = entry.checkmark,
            .radio = entry.radio,
            .toggleState = entry.toggleState,
        }
    );
  }

  const float menuWidth = std::max(1.0F, inst.chrome.contentWidth);

  auto scrollView = ui::scrollView({
      .state = &inst.scrollState,
      .contentScale = contentScale(),
      .scrollbarVisible = true,
      .viewportPaddingH = 0.0F,
      .viewportPaddingV = 0.0F,
      .radius = 0.0F,
      .width = inst.chrome.contentWidth,
      .height = inst.chrome.contentHeight,
      .configure = [this, &inst](ScrollView& view) {
        view.setPosition(inst.chrome.contentX(), inst.chrome.contentY());
        view.clearFill();
        view.clearBorder();
        view.setScrollbarInsetV(Style::scaledRadiusLg(contentScale()));
      },
  });

  auto menu = std::make_unique<ContextMenuControl>();
  menu->setReserveToggleSpace(true);
  menu->setContentScale(contentScale());
  menu->setMenuWidth(menuWidth);
  menu->setMaxVisible(entries.size()); // Always lay out all entries for scrolling
  menu->setSubmenuDirection(inst.submenuDirection);
  menu->setEntries(std::move(entries));
  menu->setRedrawCallback([&inst]() {
    if (inst.surface != nullptr) {
      inst.surface->requestRedraw();
    }
  });
  menu->setOnActivate([this](const ContextMenuControlEntry& entry) {
    if (m_tray == nullptr || m_activeItemId.empty()) {
      return;
    }
    DeferredCall::callLater([this, entry]() {
      if (m_tray != nullptr) {
        (void)m_tray->activateMenuEntry(m_activeItemId, entry.id);
      }
      close();
      closeTrayDrawerPanelIfOpen();
    });
  });
  menu->setOnSubmenuOpen([this, levelIndex](const ContextMenuControlEntry& entry, float rowCenterY) {
    openSubmenuAtLevel(levelIndex + 1, entry.id, rowCenterY);
  });
  scrollView->content()->addChild(std::move(menu));
  scrollView->layout(inst.surface->renderTarget().renderer());
  inst.sceneRoot->addChild(std::move(scrollView));

  inst.inputDispatcher.setSceneRoot(inst.sceneRoot.get());
  inst.inputDispatcher.setCursorShapeCallback([this](uint32_t serial, uint32_t shape) {
    m_wayland->setCursorShape(serial, shape);
  });
  inst.surface->setSceneRoot(inst.sceneRoot.get());
}
