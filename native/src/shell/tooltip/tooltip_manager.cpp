#include "shell/tooltip/tooltip_manager.h"

#include "config/config_service.h"
#include "core/deferred_call.h"
#include "core/log.h"
#include "core/ui_phase.h"
#include "render/render_context.h"
#include "render/render_target.h"
#include "render/scene/input_area.h"
#include "render/scene/node.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "ui/builders.h"
#include "ui/palette.h"
#include "ui/style.h"
#include "wayland/popup_surface.h"
#include "wayland/wayland_connection.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace {

  constexpr Logger kLog("tooltip");

  // Tooltips use the Kusanagi look. A bar module's tooltip pops in and out without a fade, and
  // moving to another module hides it and restarts the delay. Rich text (anything with a tag) goes through
  // Pango markup.
  constexpr auto kShowDelay = std::chrono::milliseconds(500);
  constexpr float kMaxContentWidth = 640.0F;
  constexpr int kMaxTextLines = 0;
  constexpr float kTableMinPeerColumnWidth = 80.0F;
  constexpr float kPadH = 10.0F;
  constexpr float kPadV = 7.0F;
  constexpr float kTableColumnGap = Style::spaceMd;
  constexpr float kBorder = 1.0F;
  constexpr float kRadius = 10.0F;
  constexpr float kFontSize = 13.0F;
  constexpr float kGap = 6.0F;

  // Line metrics that match the classic look: lines are floor(ascent) + ceil(descent) apart and the first
  // baseline sits floor(ascent) below the top (rich text 1 px higher).
  struct QtLines {
    float pitch = 0.0F;
    float ascent = 0.0F;
  };
  QtLines qtLines(Renderer& renderer, float fontSize) {
    const auto fm = renderer.measureFont(fontSize, FontWeight::Bold, kusanagi::font());
    return {std::floor(-fm.top) + std::ceil(fm.bottom), std::floor(-fm.top)};
  }

  void replaceAll(std::string& s, std::string_view from, std::string_view to) {
    for (std::size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) {
      s.replace(at, from.size(), to);
    }
  }

  // Turns tooltip text into Pango markup with every line on the pitch above. Handles the few rich-text tags
  // tooltips use: <br> (newline), <pre> (a 12 px gap after its block) and <small> (0.8x, on its own pitch).
  // Plain text is escaped.
  std::string tooltipMarkup(std::string_view text, bool rich, Renderer& renderer, float fontSize) {
    std::string out;
    if (rich) {
      out = std::string(text);
    } else {
      for (const char c : text) {
        out += c == '&' ? std::string("&amp;") : c == '<' ? std::string("&lt;") : c == '>' ? std::string("&gt;") : std::string(1, c);
      }
    }
    const float scale = renderer.renderScale();
    auto units = [scale](float px) { return std::to_string(std::lround(px * 1024.0F * scale)); };
    if (rich) {
      for (const std::string_view br : {"<br/>", "<br />", "<br>"}) {
        replaceAll(out, br, "\n");
      }
      for (std::size_t at = out.find("<pre"); at != std::string::npos; at = out.find("<pre", at)) {
        const std::size_t end = out.find('>', at);
        if (end == std::string::npos) break;
        out.erase(at, end - at + 1);
      }
      for (std::size_t at = out.find("</pre>"); at != std::string::npos; at = out.find("</pre>", at)) {
        const bool last = out.find_first_not_of(" \n", at + 6) == std::string::npos;
        const std::string margin = last ? "" : "\n<span size=\"1024\" line_height=\"" + units(12.0F) + "\"> </span>\n";
        out.replace(at, 6, margin);
        at += margin.size();
      }
      replaceAll(out, "<small>", "<span size=\"80%\" line_height=\"" + units(qtLines(renderer, fontSize * 0.8F).pitch) + "\">");
      replaceAll(out, "</small>", "</span>");
    }
    return "<span line_height=\"" + units(qtLines(renderer, fontSize).pitch) + "\">" + out + "</span>";
  }
  // Monospace so grid-mode value columns don't reflow on tiny per-tick content changes
  // (e.g. rising bitrate). Resolved via fontconfig — respects the user's `monospace` alias.
  constexpr std::string_view kValueFontFamily = "monospace";

  std::unique_ptr<Label> makeTooltipTextLabel(std::string_view text, float fontSize, float maxWidth, Renderer& renderer) {
    const bool rich = text.find('<') != std::string_view::npos;
    return ui::label({
        .text = tooltipMarkup(text, rich, renderer, fontSize),
        .fontSize = fontSize,
        .fontWeight = FontWeight::Bold,
        .fontFamily = kusanagi::font(),
        .color = colorSpecFromRole(ColorRole::OnSurface),
        .maxWidth = maxWidth,
        .maxLines = kMaxTextLines,
        .baselineMode = LabelBaselineMode::FontLine,
        .configure = [](Label& l) { l.setUseMarkup(true); },
    });
  }

  struct TableColumnWidths {
    float key = 0.0F;
    float value = 0.0F;
  };

  TableColumnWidths fitTableColumns(float naturalKeyW, float naturalValueW) {
    const float availableW = std::max(0.0F, kMaxContentWidth - kTableColumnGap);
    if (availableW <= 0.0F) {
      return {};
    }

    const float halfW = availableW * 0.5F;
    const float peerReserveW = std::min(kTableMinPeerColumnWidth, halfW);
    const float columnMaxW = std::max(0.0F, availableW - peerReserveW);

    TableColumnWidths widths{
        .key = std::min(naturalKeyW, halfW),
        .value = std::min(naturalValueW, halfW),
    };

    float remainingW = std::max(0.0F, availableW - widths.key - widths.value);
    if (remainingW <= 0.0F) {
      return widths;
    }

    float keyNeed = std::max(0.0F, std::min(naturalKeyW, columnMaxW) - widths.key);
    float valueNeed = std::max(0.0F, std::min(naturalValueW, columnMaxW) - widths.value);
    const float totalNeed = keyNeed + valueNeed;
    if (totalNeed <= 0.0F) {
      return widths;
    }

    const float keyDelta = std::min(keyNeed, remainingW * (keyNeed / totalNeed));
    widths.key += keyDelta;
    remainingW -= keyDelta;
    keyNeed -= keyDelta;

    const float valueDelta = std::min(valueNeed, remainingW);
    widths.value += valueDelta;
    remainingW -= valueDelta;

    if (remainingW > 0.0F && keyNeed > 0.0F) {
      widths.key += std::min(keyNeed, remainingW);
    }

    return widths;
  }

  // The anchor is the item's whole rect, so the popup's edge sits exactly on the item's edge. Bar tooltips
  // sit flush against a top or left bar's module and 6 px off a bottom or right bar's. Other tooltips keep
  // a 6 px gap all round.
  PopupSurfaceConfig buildTooltipAnchorConfig(const InputArea* area, bool fromBar) {
    const Node* anchorNode = area->tooltipAnchorNode();
    const Node* boundsNode = anchorNode != nullptr ? anchorNode : area;
    float absX = 0.0F;
    float absY = 0.0F;
    Node::absolutePosition(boundsNode, absX, absY);

    TooltipAnchorInsets inset{};
    if (area->hasTooltipAnchorInsets()) {
      inset = area->tooltipAnchorInsets();
    }
    const float iconX = absX + inset.left;
    const float iconY = absY + inset.top;
    const float iconW = std::max(1.0F, boundsNode->width() - inset.left - inset.right);
    const float iconH = std::max(1.0F, boundsNode->height() - inset.top - inset.bottom);

    const auto gap = static_cast<std::int32_t>(std::lround(kGap));
    const std::int32_t nearGap = fromBar ? 0 : gap; // below / right of the item
    const std::int32_t farGap = gap;                // above / left of it

    float anchorX = iconX;
    float anchorY = iconY;
    float anchorW = iconW;
    float anchorH = iconH;
    std::uint32_t anchor = XDG_POSITIONER_ANCHOR_BOTTOM;
    std::uint32_t gravity = XDG_POSITIONER_GRAVITY_BOTTOM;
    std::int32_t offsetX = 0;
    std::int32_t offsetY = nearGap;
    std::uint32_t constraintAdjustment =
        XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y | XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X;

    switch (area->tooltipPlacement()) {
    case TooltipPlacement::Above:
      anchor = XDG_POSITIONER_ANCHOR_TOP;
      gravity = XDG_POSITIONER_GRAVITY_TOP;
      offsetY = -farGap;
      constraintAdjustment = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X;
      break;
    case TooltipPlacement::Below:
      constraintAdjustment = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X;
      break;
    case TooltipPlacement::Left:
      anchor = XDG_POSITIONER_ANCHOR_LEFT;
      gravity = XDG_POSITIONER_GRAVITY_LEFT;
      offsetX = -farGap;
      offsetY = 0;
      constraintAdjustment = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y;
      break;
    case TooltipPlacement::Right:
      anchor = XDG_POSITIONER_ANCHOR_RIGHT;
      gravity = XDG_POSITIONER_GRAVITY_RIGHT;
      offsetX = nearGap;
      offsetY = 0;
      constraintAdjustment = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y;
      break;
    case TooltipPlacement::Default:
      break;
    }

    PopupSurfaceConfig config{};
    config.anchorX = static_cast<std::int32_t>(std::round(anchorX));
    config.anchorY = static_cast<std::int32_t>(std::round(anchorY));
    config.anchorWidth = std::max(1, static_cast<std::int32_t>(std::round(anchorW)));
    config.anchorHeight = std::max(1, static_cast<std::int32_t>(std::round(anchorH)));
    config.anchor = anchor;
    config.gravity = gravity;
    config.constraintAdjustment = constraintAdjustment;
    config.offsetX = offsetX;
    config.offsetY = offsetY;
    return config;
  }

} // namespace

TooltipManager& TooltipManager::instance() {
  static TooltipManager inst;
  return inst;
}

void TooltipManager::initialize(WaylandConnection& wayland, ConfigService* config, RenderContext* renderContext) {
  m_wayland = &wayland;
  m_config = config;
  m_renderContext = renderContext;
}

void TooltipManager::forceDestroy() {
  m_showTimer.stop();
  m_refreshTimer.stop();
  m_pendingArea = nullptr;
  m_pendingContent = {};
  m_pendingLayerParent = nullptr;
  m_pendingXdgParent = nullptr;
  m_pendingOutput = nullptr;
  m_reshowQueued = false;
  m_retargetQueued = false;
  m_destroyScheduled = false;
  m_showAfterDestroy = false;
  if (m_surface != nullptr) {
    m_surface->setDismissedCallback(nullptr);
    m_surface->setSceneRoot(nullptr);
  }
  destroyPopup();
}

void TooltipManager::shutdown() {
  forceDestroy();
  m_suppressedBarTooltipPanels.clear();
  m_wayland = nullptr;
  m_config = nullptr;
  m_renderContext = nullptr;
}

void TooltipManager::onHoverChange(InputArea* area, zwlr_layer_surface_v1* parentLayerSurface, wl_output* output) {
  if (area != nullptr && area->hasTooltip() && parentLayerSurface != nullptr && output != nullptr) {
    m_pendingFromBar = m_nextFromBar;
    m_pendingContent = area->tooltipContent();
    m_pendingLayerParent = parentLayerSurface;
    m_pendingXdgParent = nullptr;
    m_pendingOutput = output;
    handleHoverChange(area);
    return;
  }

  dismissPopup();
}

void TooltipManager::onHoverChange(InputArea* area, xdg_surface* parentXdgSurface, wl_output* output) {
  if (area != nullptr && area->hasTooltip() && parentXdgSurface != nullptr && output != nullptr) {
    m_pendingFromBar = false;
    m_pendingContent = area->tooltipContent();
    m_pendingLayerParent = nullptr;
    m_pendingXdgParent = parentXdgSurface;
    m_pendingOutput = output;
    handleHoverChange(area);
    return;
  }

  dismissPopup();
}

void TooltipManager::onBarHoverChange(InputArea* area, zwlr_layer_surface_v1* parentLayerSurface, wl_output* output) {
  if (!m_suppressedBarTooltipPanels.empty()) {
    dismissPopup();
    return;
  }
  m_nextFromBar = true;
  onHoverChange(area, parentLayerSurface, output);
  m_nextFromBar = false;
}

void TooltipManager::suppressBarTooltipsForPanel(std::string_view panelId) {
  if (panelId.empty() || !m_suppressedBarTooltipPanels.emplace(panelId).second) {
    return;
  }
  dismissPopup();
}

void TooltipManager::restoreBarTooltipsForPanel(std::string_view panelId) {
  m_suppressedBarTooltipPanels.erase(std::string(panelId));
}

void TooltipManager::handleHoverChange(InputArea* area) {
  const bool sameArea = area == m_pendingArea;
  m_pendingArea = area;
  area->setTooltipChangedCallback([this](InputArea* changedArea) { refreshFromArea(changedArea); });

  // Hovering another bar module hides the tooltip at once and restarts the show delay.
  if (m_pendingFromBar && !sameArea && (m_state == State::Showing || m_state == State::FadingOut)) {
    m_showAfterDestroy = false;
    if (m_fadeAnimId != 0) {
      m_animations.cancel(m_fadeAnimId);
      m_fadeAnimId = 0;
    }
    m_state = State::FadingOut;
    scheduleDestroyPopup();
    m_showTimer.stop();
    m_showTimer.start(kShowDelay, [this] { showPopup(); });
    return;
  }

  switch (m_state) {
  case State::Idle:
    m_state = State::Pending;
    m_showTimer.start(kShowDelay, [this] { showPopup(); });
    break;
  case State::Pending:
    m_showTimer.stop();
    m_showTimer.start(kShowDelay, [this] { showPopup(); });
    break;
  case State::Showing:
    if (sameArea) {
      refreshFromArea(area);
      break;
    }
    if (canRetargetPopup()) {
      scheduleRetargetPopup();
    } else {
      scheduleReshow();
    }
    break;
  case State::FadingOut:
    if (canRetargetPopup()) {
      scheduleRetargetPopup();
    } else {
      scheduleReshow();
    }
    break;
  }
}

bool TooltipManager::canRetargetPopup() const {
  return m_surface != nullptr
      && m_activeLayerParent == m_pendingLayerParent
      && m_activeXdgParent == m_pendingXdgParent
      && m_activeOutput == m_pendingOutput;
}

void TooltipManager::scheduleRetargetPopup() {
  if (m_retargetQueued) {
    return;
  }
  m_retargetQueued = true;
  DeferredCall::callLater([this] {
    m_retargetQueued = false;
    if (m_pendingArea == nullptr) {
      return;
    }
    if (m_surface == nullptr) {
      showPopup();
      return;
    }
    if (m_fadeAnimId != 0) {
      m_animations.cancel(m_fadeAnimId);
      m_fadeAnimId = 0;
    }
    if (m_state == State::FadingOut) {
      m_state = State::Showing;
    }
    refreshPopupContent();
    scheduleProviderRefresh();
  });
}

void TooltipManager::scheduleReshow() {
  if (m_reshowQueued) {
    return;
  }
  m_reshowQueued = true;
  // Hover changes are delivered during pointer-event dispatch, so rebuilding now would
  // re-enter the Wayland event loop while a pointer event is still on the stack.
  // Defer to the next main-loop tick. m_pendingArea is cleared when the hover target
  // is lost or destroyed, so a non-null value here is a live area.
  DeferredCall::callLater([this] {
    m_reshowQueued = false;
    if (m_pendingArea == nullptr) {
      return;
    }
    if (m_surface == nullptr && m_state == State::Idle) {
      showPopup();
      return;
    }
    // Layer-shell allows only one popup per surface; let the compositor finish
    // tearing down the old popup before zwlr_layer_surface_v1_get_popup runs again.
    m_showAfterDestroy = true;
    scheduleDestroyPopup();
  });
}

void TooltipManager::syncAnchor(InputArea* area) {
  if (m_state != State::Showing || m_surface == nullptr || m_pendingArea != area) {
    return;
  }

  auto anchorConfig = buildTooltipAnchorConfig(area, m_pendingFromBar);
  anchorConfig.width = m_surface->width();
  anchorConfig.height = m_surface->height();
  m_surface->repositionAnchor(anchorConfig);
}

void TooltipManager::showPopup() {
  if (m_wayland == nullptr
      || m_renderContext == nullptr
      || (m_pendingLayerParent == nullptr && m_pendingXdgParent == nullptr)
      || m_pendingOutput == nullptr
      || m_pendingArea == nullptr) {
    m_state = State::Idle;
    return;
  }

  if (m_surface != nullptr) {
    m_showAfterDestroy = true;
    scheduleDestroyPopup();
    return;
  }

  m_pendingContent = m_pendingArea->tooltipContent();
  ScaledRenderer measureRenderer(*m_renderContext, pendingOutputScale());
  const auto [contentW, contentH] = measureContent(measureRenderer, m_pendingContent);
  if (contentW == 0 || contentH == 0) {
    m_state = State::Idle;
    return;
  }

  auto config = buildTooltipAnchorConfig(m_pendingArea, m_pendingFromBar);
  config.width = contentW;
  config.height = contentH;
  config.grab = false;

  m_surface = std::make_unique<PopupSurface>(*m_wayland);
  m_surface->setRenderContext(m_renderContext);
  m_surface->setDismissedCallback([this] { scheduleDestroyPopup(); });

  const bool initialized = m_pendingXdgParent != nullptr
      ? m_surface->initializeAsChild(m_pendingXdgParent, m_pendingOutput, config)
      : m_surface->initialize(m_pendingLayerParent, m_pendingOutput, config);
  if (!initialized) {
    kLog.warn("failed to create tooltip popup");
    m_surface.reset();
    m_state = State::Idle;
    return;
  }

  const auto [scaledContentW, scaledContentH] = measureContent(m_surface->renderTarget().renderer(), m_pendingContent);
  if (scaledContentW == 0 || scaledContentH == 0) {
    destroyPopup();
    return;
  }
  if (scaledContentW != contentW || scaledContentH != contentH) {
    m_surface->resize(scaledContentW, scaledContentH);
  }

  m_surface->setInputRegion({});
  m_surface->setAnimationManager(&m_animations);
  m_surface->setConfigureCallback([this](std::uint32_t, std::uint32_t) { m_surface->requestLayout(); });
  m_surface->setPrepareFrameCallback([this](bool u, bool l) { prepareFrame(u, l); });
  m_surface->setScaleChangedCallback([this](float) {
    // Genuine compositor scale change for this popup: remeasure/resize/rebuild.
    if (m_state == State::Showing && m_pendingArea != nullptr) {
      refreshPopupContent();
    }
  });

  m_paletteConn = paletteChanged().connect([this] {
    if (m_surface != nullptr) {
      m_surface->requestRedraw();
    }
  });

  m_state = State::Showing;
  m_activeFromBar = m_pendingFromBar;
  m_activeLayerParent = m_pendingLayerParent;
  m_activeXdgParent = m_pendingXdgParent;
  m_activeOutput = m_pendingOutput;
  scheduleProviderRefresh();
  m_surface->requestUpdate();
}

void TooltipManager::dismissPopup() {
  m_refreshTimer.stop();
  m_reshowQueued = false;
  m_retargetQueued = false;
  m_showAfterDestroy = false;
  // The hover target is gone (pointer left, area lost its tooltip, or the area was
  // destroyed). Clear it so a deferred reshow does not dereference a stale area.
  m_pendingArea = nullptr;
  switch (m_state) {
  case State::Pending:
    m_showTimer.stop();
    m_state = State::Idle;
    break;
  case State::Showing:
    if (m_sceneRoot == nullptr || m_surface == nullptr || m_activeFromBar) {
      // Bar tooltips disappear at once, without a fade.
      m_state = State::FadingOut;
      scheduleDestroyPopup();
      return;
    }
    m_state = State::FadingOut;
    if (m_fadeAnimId != 0) {
      m_animations.cancel(m_fadeAnimId);
    }
    m_fadeAnimId = m_animations.animate(
        m_sceneRoot->opacity(), 0.0F, Style::animFast, Easing::EaseOutQuad,
        [this](float v) {
          if (m_sceneRoot != nullptr) {
            m_sceneRoot->setOpacity(v);
            m_sceneRoot->markPaintDirty();
          }
        },
        [this] {
          m_fadeAnimId = 0;
          scheduleDestroyPopup();
        },
        this
    );
    if (m_surface != nullptr) {
      m_surface->requestRedraw();
    }
    break;
  case State::FadingOut:
  case State::Idle:
    break;
  }
}

void TooltipManager::scheduleDestroyPopup() {
  if (m_destroyScheduled) {
    return;
  }
  if (m_state == State::Idle && m_surface == nullptr) {
    return;
  }
  m_destroyScheduled = true;
  DeferredCall::callLater([this] {
    m_destroyScheduled = false;
    destroyPopup();
    if (!m_showAfterDestroy || m_pendingArea == nullptr) {
      m_showAfterDestroy = false;
      return;
    }
    m_showAfterDestroy = false;
    DeferredCall::callLater([this] {
      if (m_pendingArea != nullptr) {
        showPopup();
      }
    });
  });
}

void TooltipManager::destroyPopup() {
  m_refreshTimer.stop();
  m_animations.cancelAll();
  m_fadeAnimId = 0;
  m_paletteConn = {};
  m_sceneRoot.reset();
  if (m_surface != nullptr) {
    m_surface->setDismissedCallback(nullptr);
    m_surface->setSceneRoot(nullptr);
  }
  m_surface.reset();
  m_activeLayerParent = nullptr;
  m_activeXdgParent = nullptr;
  m_activeOutput = nullptr;
  m_activeFromBar = false;
  m_state = State::Idle;
}

void TooltipManager::refreshFromArea(InputArea* area) {
  if (area == nullptr || area != m_pendingArea || !area->hovered()) {
    return;
  }

  if (!area->hasTooltip()) {
    dismissPopup();
    return;
  }

  m_pendingContent = area->tooltipContent();
  switch (m_state) {
  case State::Pending:
    break;
  case State::Showing:
    refreshPopupContent();
    scheduleProviderRefresh();
    break;
  case State::Idle: {
    ScaledRenderer measureRenderer(*m_renderContext, pendingOutputScale());
    if (measureContent(measureRenderer, m_pendingContent).w > 0) {
      showPopup();
    }
    break;
  }
  case State::FadingOut:
    if (m_fadeAnimId != 0) {
      scheduleRetargetPopup();
    } else {
      // Fade finished and the surface teardown is queued; show again once it completes.
      m_showAfterDestroy = true;
    }
    break;
  }
}

void TooltipManager::refreshPopupContent() {
  if (m_surface == nullptr || m_renderContext == nullptr || m_pendingArea == nullptr) {
    return;
  }

  const auto [contentW, contentH] = measureContent(m_surface->renderTarget().renderer(), m_pendingContent);
  if (contentW == 0 || contentH == 0) {
    // The area is still hovered but its provider has nothing to show right now; keep tracking
    // it so a later refreshFromArea() can bring the tooltip back.
    InputArea* area = m_pendingArea;
    dismissPopup();
    m_pendingArea = area;
    return;
  }

  auto anchorConfig = buildTooltipAnchorConfig(m_pendingArea, m_pendingFromBar);
  anchorConfig.width = contentW;
  anchorConfig.height = contentH;
  // Leave the new size and position pending on the surface. Committing them now, while the
  // previous tooltip's buffer is still attached, makes the compositor stretch that stale buffer
  // to the new geometry (visible as a corrupted, scaled-up tooltip when moving between widgets).
  // The rebuilt scene is published with the matching buffer on the next redraw.
  m_surface->resize(contentW, contentH, false);
  m_surface->repositionAnchor(anchorConfig, false);

  m_renderContext->makeCurrent(m_surface->renderTarget());
  m_sceneRoot.reset();
  {
    UiPhaseScope layoutPhase(UiPhase::Layout);
    buildScene(m_pendingContent, static_cast<float>(contentW), static_cast<float>(contentH), 1.0F);
  }
  m_surface->requestRedraw();
}

void TooltipManager::scheduleProviderRefresh() {
  m_refreshTimer.stop();
  if (m_state != State::Showing || m_pendingArea == nullptr) {
    return;
  }

  const auto interval = m_pendingArea->tooltipRefreshInterval();
  if (interval.count() <= 0) {
    return;
  }

  InputArea* area = m_pendingArea;
  m_refreshTimer.start(interval, [this, area]() { refreshFromArea(area); });
}

float TooltipManager::pendingOutputScale() const {
  if (m_wayland != nullptr && m_pendingOutput != nullptr) {
    if (const WaylandOutput* output = m_wayland->findOutputByWl(m_pendingOutput); output != nullptr) {
      return output->configuredScale();
    }
  }
  return 1.0F;
}

TooltipManager::Size TooltipManager::measureContent(Renderer& renderer, const TooltipContent& content) {
  if (m_renderContext == nullptr) {
    return {};
  }

  const float scale = (m_config != nullptr) ? std::max(0.1F, m_config->config().accessibility.uiScale) : 1.0F;
  const float maxContentWidth = kMaxContentWidth * scale;
  const float fontSize = kFontSize * scale;
  const float padH = kPadH * scale;
  const float padV = kPadV * scale;
  const float tableColumnGap = kTableColumnGap * scale;

  if (const auto* text = std::get_if<std::string>(&content)) {
    auto label = makeTooltipTextLabel(*text, fontSize, maxContentWidth, renderer);
    label->measure(renderer);
    // The border is drawn inside the padding.
    auto w = static_cast<std::uint32_t>(std::ceil(label->width() + padH * 2.0F));
    auto h = static_cast<std::uint32_t>(std::ceil(label->height() + padV * 2.0F));
    return {std::max(w, 1U), std::max(h, 1U)};
  }

  if (const auto* rows = std::get_if<std::vector<TooltipRow>>(&content)) {
    if (rows->empty()) {
      return {};
    }
    float maxKeyW = 0.0F;
    float maxValW = 0.0F;
    float rowH = 0.0F;
    for (const auto& row : *rows) {
      auto km = renderer.measureText(row.key, fontSize);
      const auto vm =
          renderer.measureText(row.value, fontSize, FontWeight::Normal, 0.0F, 0, TextAlign::Start, kValueFontFamily);
      maxKeyW = std::max(maxKeyW, km.width);
      maxValW = std::max(maxValW, vm.width);
      rowH = std::max({rowH, km.bottom - km.top, vm.bottom - vm.top});
    }
    const TableColumnWidths columns = fitTableColumns(maxKeyW, maxValW);
    float contentW = columns.key + tableColumnGap + columns.value;
    float contentH = static_cast<float>(rows->size()) * rowH;
    auto w = static_cast<std::uint32_t>(std::ceil(contentW + padH * 2.0F + kBorder * 2.0F));
    auto h = static_cast<std::uint32_t>(std::ceil(contentH + padV * 2.0F + kBorder * 2.0F));
    return {std::max(w, 1U), std::max(h, 1U)};
  }

  return {};
}

void TooltipManager::buildScene(const TooltipContent& content, float w, float h, float opacity) {
  uiAssertNotRendering("TooltipManager::buildScene");
  if (m_renderContext == nullptr || m_surface == nullptr) {
    return;
  }
  Renderer& renderer = m_surface->renderTarget().renderer();

  m_sceneRoot = ui::node({});
  m_sceneRoot->setSize(w, h);
  m_sceneRoot->setOpacity(opacity);
  m_sceneRoot->setHitTestVisible(false);
  m_surface->setSceneRoot(m_sceneRoot.get());

  m_sceneRoot->addChild(
      ui::box({
          .fill = colorSpecFromRole(ColorRole::Surface, 0.85F),
          .radius = kRadius,
          .width = w,
          .height = h,
          .configure = [](Box& box) { box.setBorder(colorSpecFromRole(ColorRole::OnSurface, 0.08F), kBorder); },
      })
  );

  const float scale = (m_config != nullptr) ? std::max(0.1F, m_config->config().accessibility.uiScale) : 1.0F;
  const float maxContentWidth = kMaxContentWidth * scale;
  const float fontSize = kFontSize * scale;
  const float padH = kPadH * scale;
  const float padV = kPadV * scale;
  const float tableColumnGap = kTableColumnGap * scale;

  if (const auto* text = std::get_if<std::string>(&content)) {
    auto label = makeTooltipTextLabel(*text, fontSize, maxContentWidth, renderer);
    label->measure(renderer);
    // First baseline floor(ascent) below the top, rich text 1 px higher.
    const float richLift = text->find('<') != std::string::npos ? 1.0F : 0.0F;
    label->setPosition(padH, std::round(padV + qtLines(renderer, fontSize).ascent - richLift - label->baselineOffset()));
    m_sceneRoot->addChild(std::move(label));
    return;
  }

  if (const auto* rows = std::get_if<std::vector<TooltipRow>>(&content)) {
    const float containerW = w - (padH + kBorder) * 2.0F;

    float maxKeyW = 0.0F;
    float maxValW = 0.0F;
    for (const auto& row : *rows) {
      auto km = renderer.measureText(row.key, fontSize);
      const auto vm =
          renderer.measureText(row.value, fontSize, FontWeight::Normal, 0.0F, 0, TextAlign::Start, kValueFontFamily);
      maxKeyW = std::max(maxKeyW, km.width);
      maxValW = std::max(maxValW, vm.width);
    }
    const TableColumnWidths columns = fitTableColumns(maxKeyW, maxValW);

    auto container = ui::column({
        .width = containerW,
        .height = h - (padV + kBorder) * 2.0F,
        .configure = [padH, padV](Flex& flex) { flex.setPosition(padH + kBorder, padV + kBorder); },
    });

    for (const auto& row : *rows) {
      auto keyLabel = ui::label({
          .text = row.key,
          .fontSize = fontSize,
          .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
          .maxLines = 1,
      });
      const auto km = renderer.measureText(row.key, fontSize);
      if (km.width > columns.key + 0.5F) {
        keyLabel->setMaxWidth(columns.key);
      }
      keyLabel->measure(renderer);

      auto valLabel = ui::label({
          .text = row.value,
          .fontSize = fontSize,
          .fontFamily = std::string(kValueFontFamily),
          .color = colorSpecFromRole(ColorRole::OnSurface),
          .maxLines = 1,
          .textAlign = TextAlign::End,
          .ellipsize = row.valueEllipsize,
      });
      const auto vm =
          renderer.measureText(row.value, fontSize, FontWeight::Normal, 0.0F, 0, TextAlign::Start, kValueFontFamily);
      if (vm.width > columns.value + 0.5F) {
        valLabel->setMaxWidth(columns.value);
      }
      valLabel->measure(renderer);

      container->addChild(
          ui::row(
              {
                  .justify = FlexJustify::SpaceBetween,
                  .gap = tableColumnGap,
                  .widthPolicy = FlexSizePolicy::Fill,
              },
              std::move(keyLabel), std::move(valLabel)
          )
      );
    }

    container->layout(renderer);
    m_sceneRoot->addChild(std::move(container));
  }
}

void TooltipManager::prepareFrame(bool /*needsUpdate*/, bool /*needsLayout*/) {
  if (m_renderContext == nullptr || m_surface == nullptr) {
    return;
  }

  const auto width = m_surface->width();
  const auto height = m_surface->height();
  if (width == 0 || height == 0) {
    return;
  }

  m_renderContext->makeCurrent(m_surface->renderTarget());

  const auto w = static_cast<float>(width);
  const auto h = static_cast<float>(height);

  if (m_sceneRoot == nullptr && m_activeFromBar) {
    UiPhaseScope layoutPhase(UiPhase::Layout);
    buildScene(m_pendingContent, w, h, 1.0F);
  } else if (m_sceneRoot == nullptr) {
    UiPhaseScope layoutPhase(UiPhase::Layout);
    buildScene(m_pendingContent, w, h);

    if (m_fadeAnimId != 0) {
      m_animations.cancel(m_fadeAnimId);
    }
    m_fadeAnimId = m_animations.animate(
        0.0F, 1.0F, Style::animFast, Easing::EaseOutQuad,
        [this](float v) {
          if (m_sceneRoot != nullptr) {
            m_sceneRoot->setOpacity(v);
            m_sceneRoot->markPaintDirty();
          }
        },
        [this] { m_fadeAnimId = 0; }, this
    );
  }
}
