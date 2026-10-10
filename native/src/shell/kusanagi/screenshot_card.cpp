#include "shell/kusanagi/screenshot_card.h"

#include "compositors/compositor_platform.h"
#include "core/deferred_call.h"
#include "core/log.h"
#include "core/process/process.h"
#include "core/ui_phase.h"
#include "ipc/ipc_service.h"
#include "render/core/async_texture_cache.h"
#include "render/core/renderer.h"
#include "render/render_context.h"
#include "render/scene/input_area.h"
#include "render/scene/node.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "wayland/wayland_connection.h"
#include "wayland/wayland_seat.h"

#include "cursor-shape-v1-client-protocol.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <linux/input-event-codes.h>

// The surface is larger than the card so the spring-in and the shadow have room. A swipe past 30% of the
// card's width flies it off; a shorter one springs back.

namespace {

  constexpr Logger kLog("screenshot-card");

  constexpr float kWinW = 360.0F;
  constexpr float kWinH = 300.0F;
  constexpr float kCardW = 340.0F;
  constexpr float kCardH = 268.0F;
  constexpr float kShotH = 172.0F;
  constexpr float kHiddenScale = 0.86F;
  constexpr float kDragThreshold = 6.0F;
  constexpr const char* kIconFont = "JetBrainsMono Nerd Font";

  float easeLinear(float t, float /*s*/) { return t; }
  float easeOutBack(float t, float s) {
    const float u = t - 1.0F;
    return u * u * ((s + 1.0F) * u + s) + 1.0F;
  }
  float easeInCubic(float t, float /*s*/) { return t * t * t; }

  std::string utf8(char32_t cp) {
    std::string out;
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
    return out;
  }

  // The first bar's position. Popups, toasts and this card keep clear of it.
  std::string barEdge() {
    const auto& s = kusanagi::settings();
    if (const auto bars = s.find("bars"); bars != s.end() && bars->is_array() && !bars->empty()) {
      const auto& b = bars->front();
      if (b.is_object() && b.contains("position") && b["position"].is_string()) return b["position"].get<std::string>();
    }
    return kusanagi::opt<std::string>("bar", "position", "top");
  }

  struct ChipDef {
    char32_t icon;
    const char* label;
  };
  constexpr std::array<ChipDef, 4> kChips{{
      {0xf018f, "Copy"},
      {0xf03eb, "Edit"},
      {0xf024b, "Folder"},
      {0xf0a7a, "Delete"},
  }};

  ColorSpec role(ColorRole r, float a = 1.0F) { return colorSpecFromRole(r, a); }

} // namespace

KusanagiScreenshotCard::KusanagiScreenshotCard() = default;

KusanagiScreenshotCard::~KusanagiScreenshotCard() { destroySurface(); }

void KusanagiScreenshotCard::initialize(
    WaylandConnection& wayland, CompositorPlatform& platform, RenderContext* renderContext, AsyncTextureCache* textures
) {
  m_wayland = &wayland;
  m_platform = &platform;
  m_renderContext = renderContext;
  m_textures = textures;
}

void KusanagiScreenshotCard::registerIpc(IpcService& ipc) {
  ipc.bind(kusanagi::cli::msg::screenshotNotify, [this, &ipc](const std::string& args) -> std::string {
    std::string path = args;
    while (!path.empty() && (path.back() == ' ' || path.back() == '\n')) path.pop_back();
    while (!path.empty() && path.front() == ' ') path.erase(path.begin());
    if (path.size() >= 2 && (path.front() == '"' || path.front() == '\'') && path.back() == path.front()) {
      path = path.substr(1, path.size() - 2);
    }
    if (path.starts_with("file://")) path = path.substr(7);
    if (path.empty()) return "error: screenshot-notify needs a file\n";
    std::filesystem::path p(path);
    if (p.is_relative() && ipc.callerCwd().has_value()) p = std::filesystem::path(*ipc.callerCwd()) / p;
    show(p.lexically_normal().string());
    return "ok\n";
  });
}

void KusanagiScreenshotCard::onOutputChange() {
  if (m_surface == nullptr || m_wayland == nullptr) return;
  const WaylandOutput* out = m_output != nullptr ? m_wayland->findOutputByWl(m_output) : nullptr;
  if (out == nullptr || !out->done) destroySurface();
}

void KusanagiScreenshotCard::show(const std::string& file) {
  if (m_wayland == nullptr || m_renderContext == nullptr) return;
  // A new shot while one is up: start over, since the corner or the output may have changed.
  destroySurface();
  m_file = file;
  ensureSurface();
  if (m_surface == nullptr) return;
  m_showPending = true;
  m_surface->requestUpdate();
}

void KusanagiScreenshotCard::hide() {
  if (!m_showing) return;
  m_showing = false;
  m_animations.cancel(m_countdown);
  m_countdown = 0;
  if (m_surface != nullptr) m_surface->setInputRegion({}); // let clicks through while hiding
  tween(m_scaleAnim, m_scale, kHiddenScale, 180.0F, easeInCubic, 0.0F);
  tween(m_opacityAnim, m_opacity, 0.0F, 160.0F, easeLinear, 0.0F, [this]() {
    DeferredCall::callLater([this]() {
      if (!m_showing && !m_showPending) destroySurface(); // unmap and free the preview
    });
  });
}

void KusanagiScreenshotCard::ensureSurface() {
  const std::string pos = kusanagi::opt<std::string>("screenshot", "position", "bottom-right");
  m_onRight = pos.ends_with("right");
  m_onBottom = pos.starts_with("bottom");
  const std::string edge = barEdge();

  m_output = m_platform != nullptr ? m_platform->preferredInteractiveOutput() : nullptr;
  if (m_output == nullptr) {
    for (const auto& o : m_wayland->outputs()) {
      if (o.done && o.output != nullptr && o.hasUsableGeometry()) {
        m_output = o.output;
        break;
      }
    }
  }
  if (m_output == nullptr) return;

  const auto w = static_cast<std::uint32_t>(kWinW);
  const auto h = static_cast<std::uint32_t>(kWinH);
  auto config = LayerSurfaceConfig{
      .nameSpace = "kusanagi-screenshot",
      .layer = LayerShellLayer::Overlay,
      .anchor = (m_onBottom ? LayerShellAnchor::Bottom : LayerShellAnchor::Top)
          | (m_onRight ? LayerShellAnchor::Right : LayerShellAnchor::Left),
      .width = w,
      .height = h,
      .exclusiveZone = 0, // stay clear of the bar's reserved space
      .marginTop = edge == "top" ? 6 : 14,
      .marginRight = 14,
      .marginBottom = edge == "bottom" ? 6 : 14,
      .marginLeft = 14,
      .keyboard = LayerShellKeyboard::None,
      .defaultWidth = w,
      .defaultHeight = h,
  };
  m_surface = std::make_unique<LayerSurface>(*m_wayland, std::move(config));
  m_surface->setRenderContext(m_renderContext);
  m_surface->setAnimationManager(&m_animations);
  m_surface->setConfigureCallback([this](std::uint32_t, std::uint32_t) { m_surface->requestLayout(); });
  m_surface->setPrepareFrameCallback([this](bool, bool) { prepareFrame(); });
  if (!m_surface->initialize(m_output)) {
    kLog.warn("failed to create the screenshot card surface");
    m_surface.reset();
    return;
  }
  m_surface->setInputRegion({});
}

void KusanagiScreenshotCard::destroySurface() {
  m_animations.cancelAll();
  m_opacityAnim = m_scaleAnim = m_swipeAnim = m_countdown = 0;
  m_input.setSceneRoot(nullptr);
  if (m_surface != nullptr) m_surface->setSceneRoot(nullptr);
  m_root.reset();
  m_surface.reset();
  m_card = nullptr;
  m_shadow = nullptr;
  m_cardArea = nullptr;
  m_bg = m_shotBg = m_hairline = nullptr;
  m_shot = nullptr;
  m_name = nullptr;
  m_chips = {};
  m_showing = false;
  m_showPending = false;
  m_pointerInside = false;
  m_dragging = m_dragged = false;
  m_opacity = 0.0F;
  m_scale = kHiddenScale;
  m_swipe = 0.0F;
  m_remaining = 1.0F;
}

void KusanagiScreenshotCard::prepareFrame() {
  if (m_renderContext == nullptr || m_surface == nullptr) return;
  const auto width = m_surface->width();
  const auto height = m_surface->height();
  if (width == 0 || height == 0) return;
  m_renderContext->makeCurrent(m_surface->renderTarget());
  if (m_root == nullptr) {
    UiPhaseScope layoutPhase(UiPhase::Layout);
    buildScene(static_cast<float>(width), static_cast<float>(height));
  }
  if (m_showPending) {
    m_showPending = false;
    startShow();
  }
}

void KusanagiScreenshotCard::buildScene(float width, float height) {
  uiAssertNotRendering("KusanagiScreenshotCard::buildScene");
  Renderer& renderer = m_surface->renderTarget().renderer();

  const float radius = std::max(10.0F, kusanagi::radius() - 2.0F);
  const float shotRadius = std::max(6.0F, radius - 6.0F);
  const float panelOpacity = static_cast<float>(kusanagi::opt<double>("panel", "opacity", 0.94));
  const std::string font = kusanagi::font();

  m_root = ui::node({});
  m_root->setSize(width, height);
  m_surface->setSceneRoot(m_root.get());

  auto card = ui::node({.out = &m_card});
  card->setSize(kCardW, kCardH);
  card->setOpacity(0.0F);
  // Springs in from its own corner.
  card->setTransformOrigin(m_onRight ? kCardW : 0.0F, m_onBottom ? kCardH : 0.0F);

  if (kusanagi::shadows()) {
    auto shadow = ui::box({.out = &m_shadow});
    shadow->setStyle(RoundedRectStyle{
        .fill = rgba(0.0F, 0.0F, 0.0F, 0.42F),
        .softness = 17.0F,
        .outerShadow = true,
        .shadowCutoutOffsetY = 8.0F,
    });
    shadow->setRadius(radius);
    shadow->setSize(kCardW, kCardH);
    shadow->setPosition(0.0F, 8.0F);
    card->addChild(std::move(shadow));
  }

  // The card: hover pauses the countdown, press and drag swipes, a click on the picture opens it.
  auto area = ui::inputArea({
      .out = &m_cardArea,
      .acceptedButtons = InputArea::buttonMask({BTN_LEFT}),
      .width = kCardW,
      .height = kCardH,
  });
  area->addChild(ui::box({
      .out = &m_bg,
      .fill = role(ColorRole::Surface, std::max(0.9F, panelOpacity)),
      .border = kusanagi::surfaceBorder(),
      .borderWidth = kusanagi::surfaceBorderWidth(),
      .radius = radius,
      .width = kCardW,
      .height = kCardH,
  }));

  // Preview
  const float shotW = kCardW - 20.0F;
  auto shotBg = ui::box({
      .out = &m_shotBg,
      .fill = role(ColorRole::OnSurface, 0.06F),
      .radius = shotRadius,
      .width = shotW,
      .height = kShotH,
  });
  shotBg->setPosition(10.0F, 10.0F);
  area->addChild(std::move(shotBg));
  auto shot = ui::image({.out = &m_shot, .fit = ImageFit::Contain, .radius = shotRadius, .width = shotW, .height = kShotH});
  shot->setPosition(10.0F, 10.0F);
  if (!m_file.empty()) {
    Image* img = shot.get();
    img->setAsyncReadyCallback([this]() {
      if (m_surface != nullptr) m_surface->requestRedraw();
    });
    if (m_textures != nullptr) {
      (void)img->setSourceFileAsync(renderer, *m_textures, m_file, 640);
    } else {
      (void)img->setSourceFile(renderer, m_file, 640);
    }
  }
  area->addChild(std::move(shot));

  // File name, elided in the middle
  const std::string name = std::filesystem::path(m_file).filename().string();
  area->addChild(ui::label({
      .out = &m_name,
      .text = name,
      .fontSize = 11.0F,
      .fontFamily = font,
      .color = role(ColorRole::OnSurfaceVariant),
      .maxWidth = shotW,
      .maxLines = 1,
      .ellipsize = TextEllipsize::Middle,
      .baselineMode = LabelBaselineMode::FontLine,
  }));
  m_name->measure(renderer);
  m_name->setPosition(10.0F, 10.0F + kShotH + 8.0F);

  // Action chips, centred along the bottom
  float rowW = 0.0F;
  std::array<float, 4> chipW{};
  for (std::size_t i = 0; i < kChips.size(); ++i) {
    auto& c = m_chips[i];
    auto chip = ui::inputArea({
        .out = &c.area,
        .acceptedButtons = InputArea::buttonMask({BTN_LEFT}),
        .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
    });
    chip->addChild(ui::box({
        .out = &c.face,
        .fill = role(ColorRole::OnSurface, 0.05F),
        .border = role(ColorRole::OnSurface, 0.06F),
        .borderWidth = 1.0F,
        .radius = 14.0F,
    }));
    chip->addChild(ui::label({
        .out = &c.icon,
        .text = utf8(kChips[i].icon),
        .fontSize = 13.0F,
        .fontFamily = std::string(kIconFont),
        .color = role(ColorRole::OnSurfaceVariant),
        .maxLines = 1,
        .baselineMode = LabelBaselineMode::FontLine,
    }));
    chip->addChild(ui::label({
        .out = &c.label,
        .text = kChips[i].label,
        .fontSize = 11.0F,
        .fontFamily = font,
        .color = role(ColorRole::OnSurfaceVariant),
        .maxLines = 1,
        .baselineMode = LabelBaselineMode::FontLine,
    }));
    c.icon->measure(renderer);
    c.label->measure(renderer);
    // Matches the classic look: an icon is as wide as its ink reaches (at least its advance), with the
    // advance centred in that width.
    const TextMetrics im =
        renderer.measureText(c.icon->text(), 13.0F, FontWeight::Normal, 0.0F, 0, TextAlign::Start, kIconFont);
    const float iconW = std::max(im.width, im.inkRight);
    const float innerW = iconW + 6.0F + c.label->width();
    const float innerH = std::max(c.icon->height(), c.label->height());
    const float w = std::round(innerW + 24.0F);
    chipW[i] = w;
    chip->setSize(w, 28.0F);
    c.face->setSize(w, 28.0F);
    const float ix = std::round((w - innerW) / 2.0F);
    const float iy = std::floor((28.0F - innerH) / 2.0F);
    c.icon->setPosition(ix + (iconW - im.width) / 2.0F, iy);
    c.label->setPosition(ix + iconW + 6.0F, iy);
    Chip* cp = &c;
    chip->setOnEnter([this, cp](const InputArea::PointerData&) {
      cp->face->setFill(role(ColorRole::OnSurface, 0.09F));
      if (m_surface != nullptr) m_surface->requestRedraw();
    });
    chip->setOnLeave([this, cp]() {
      cp->face->setFill(role(ColorRole::OnSurface, 0.05F));
      if (m_surface != nullptr) m_surface->requestRedraw();
    });
    const int action = static_cast<int>(i);
    chip->setOnClick([this, action](const InputArea::PointerData&) { act(action); });
    rowW += w;
    area->addChild(std::move(chip));
  }
  rowW += 6.0F * static_cast<float>(kChips.size() - 1);
  float x = std::round((kCardW - rowW) / 2.0F);
  for (std::size_t i = 0; i < kChips.size(); ++i) {
    m_chips[i].area->setPosition(x, kCardH - 12.0F - 28.0F);
    x += chipW[i] + 6.0F;
  }

  // Countdown bar
  area->addChild(ui::box({
      .out = &m_hairline,
      .fill = role(ColorRole::Primary, 0.8F),
      .radius = 1.0F,
      .width = kCardW - 2.0F * radius,
      .height = 2.0F,
  }));
  m_hairline->setPosition(radius, kCardH - 2.0F);

  area->setOnPress([this](const InputArea::PointerData& p) {
    if (!m_showing) return;
    if (p.pressed) {
      m_pressX = p.sceneX;
      m_dragging = false;
      m_dragged = false;
      return;
    }
    if (m_dragging) {
      m_dragging = false;
      if (std::abs(m_swipe) > kCardW * 0.3F) {
        flyOff();
      } else {
        tween(m_swipeAnim, m_swipe, 0.0F, 320.0F, easeOutBack, kusanagi::bounce(1.2F));
      }
    }
  });
  area->setOnMotion([this](const InputArea::PointerData& p) {
    if (m_cardArea == nullptr || !m_cardArea->pressed() || !m_showing) return;
    // The card moves under the pointer, so measure in surface coordinates.
    float sx = 0.0F;
    float sy = 0.0F;
    Node::mapToScene(m_cardArea, p.localX, p.localY, sx, sy);
    const float dx = sx - m_pressX;
    if (!m_dragging && std::abs(dx) > kDragThreshold) {
      m_dragging = true;
      m_dragged = true;
      m_animations.cancel(m_swipeAnim);
      m_swipeAnim = 0;
    }
    if (m_dragging) {
      m_swipe = dx;
      applyMotion();
    }
  });
  area->setOnClick([this](const InputArea::PointerData& p) {
    if (m_dragged || !m_showing) return;
    const bool onShot = p.localX >= 10.0F && p.localX <= kCardW - 10.0F && p.localY >= 10.0F && p.localY <= 10.0F + kShotH;
    if (onShot) act(-1);
  });
  card->addChild(std::move(area));
  m_root->addChild(std::move(card));

  m_input.setSceneRoot(m_root.get());
  m_input.setCursorShapeCallback([this](std::uint32_t serial, std::uint32_t shape) {
    if (m_wayland != nullptr) m_wayland->setCursorShape(serial, shape);
  });
}

void KusanagiScreenshotCard::startShow() {
  m_showing = true;
  m_swipe = 0.0F;
  m_remaining = 1.0F;
  m_opacity = 0.0F;
  m_scale = kHiddenScale;
  tween(m_opacityAnim, m_opacity, 1.0F, 200.0F, easeLinear, 0.0F);
  tween(m_scaleAnim, m_scale, 1.0F, 460.0F, easeOutBack, kusanagi::bounce(1.3F));
  startCountdown();
}

void KusanagiScreenshotCard::startCountdown() {
  m_animations.cancel(m_countdown);
  m_countdown = 0;
  if (!m_showing || m_remaining <= 0.0F) return;
  const float total = static_cast<float>(std::max(500, kusanagi::opt<int>("screenshot", "timeout", 6000)));
  // screenshot.timeout is a real duration, not scaled by look.animSpeed.
  m_countdown = m_animations.animateTimer(
      m_remaining, 0.0F, total * m_remaining, Easing::Linear,
      [this](float v) {
        m_remaining = v;
        applyMotion();
      },
      [this]() {
        m_countdown = 0;
        m_remaining = 0.0F;
        hide();
      },
      this
  );
}

void KusanagiScreenshotCard::pauseCountdown() {
  m_animations.cancel(m_countdown);
  m_countdown = 0;
}

void KusanagiScreenshotCard::act(int action) {
  const std::string file = m_file;
  if (file.empty()) return;
  const std::string dir = std::filesystem::path(file).parent_path().string();
  switch (action) {
  case -1:
    (void)process::runAsync(std::vector<std::string>{"xdg-open", file});
    break;
  case 0:
    (void)process::runAsync(std::vector<std::string>{"sh", "-c", "wl-copy --type image/png < \"$1\"", "sh", file});
    break;
  case 1: {
    const std::string editor = kusanagi::opt<std::string>("screenshot", "editor", "swappy -f");
    (void)process::runAsync(std::vector<std::string>{"sh", "-c", editor + " \"$1\"", "sh", file});
    break;
  }
  case 2:
    (void)process::runAsync(std::vector<std::string>{"xdg-open", dir});
    break;
  case 3:
    (void)process::runAsync(std::vector<std::string>{"rm", "-f", file});
    break;
  default:
    break;
  }
  // Input callbacks run mid-dispatch, so change state on the next tick.
  DeferredCall::callLater([this]() { hide(); });
}

void KusanagiScreenshotCard::flyOff() {
  const float to = (m_onRight ? 1.0F : -1.0F) * (kCardW + 40.0F);
  tween(m_swipeAnim, m_swipe, to, 320.0F, easeOutBack, kusanagi::bounce(1.2F));
  DeferredCall::callLater([this]() { hide(); });
}

void KusanagiScreenshotCard::tween(
    AnimationManager::Id& id, float& value, float to, float durationMs, float (*ease)(float, float), float overshoot,
    std::function<void()> done
) {
  if (id != 0) {
    m_animations.cancel(id);
    id = 0;
  }
  if (std::abs(value - to) < 1.0e-4F) {
    value = to;
    applyMotion();
    if (done) done();
    return;
  }
  const float from = value;
  auto token = std::make_shared<AnimationManager::Id>(0);
  id = m_animations.animate(
      0.0F, 1.0F, durationMs, Easing::Linear,
      [this, &value, from, to, ease, overshoot](float t) {
        value = from + (to - from) * ease(t, overshoot);
        applyMotion();
      },
      [&id, token, done = std::move(done)]() {
        if (id == *token) id = 0;
        if (done) done();
      }
  );
  *token = id;
}

void KusanagiScreenshotCard::applyMotion() {
  if (m_card == nullptr || m_surface == nullptr) return;
  const float y = m_onBottom ? kWinH - kCardH - 4.0F : 4.0F;
  m_card->setPosition(std::round(m_swipe), y);
  m_card->setScale(m_scale);
  m_card->setOpacity(std::clamp(m_opacity, 0.0F, 1.0F));
  if (m_hairline != nullptr) {
    const float radius = std::max(10.0F, kusanagi::radius() - 2.0F);
    const float w = (kCardW - 2.0F * radius) * std::clamp(m_remaining, 0.0F, 1.0F);
    m_hairline->setSize(w, 2.0F);
    m_hairline->setVisible(w > 0.5F);
  }
  // The input region follows the card while it is up.
  if (m_showing) {
    const int x = static_cast<int>(std::round(m_swipe));
    m_surface->setInputRegion({InputRect{x, static_cast<int>(y), static_cast<int>(kCardW), static_cast<int>(kCardH)}});
  }
  m_surface->requestRedraw();
}

bool KusanagiScreenshotCard::onPointerEvent(const PointerEvent& event) {
  if (m_surface == nullptr || m_root == nullptr) return false;
  const bool ours = event.surface != nullptr && event.surface == m_surface->wlSurface();
  const auto sx = static_cast<float>(event.sx);
  const auto sy = static_cast<float>(event.sy);
  bool consumed = false;
  switch (event.type) {
  case PointerEvent::Type::Enter:
    if (ours) {
      m_pointerInside = true;
      m_input.pointerEnter(sx, sy, event.serial);
      pauseCountdown();
    }
    break;
  case PointerEvent::Type::Leave:
    if (ours) {
      m_pointerInside = false;
      m_input.pointerLeave();
      if (m_showing && !m_dragging) startCountdown();
    }
    break;
  case PointerEvent::Type::Motion:
    if (m_pointerInside) {
      m_input.pointerMotion(sx, sy, 0);
      consumed = true;
    }
    break;
  case PointerEvent::Type::Button:
    if (m_pointerInside || (m_cardArea != nullptr && m_cardArea->pressed())) {
      m_input.pointerButton(sx, sy, event.button, event.pressed, event.serial, event.time, event.touch);
      consumed = true;
      // A swipe released outside the card: the countdown goes on.
      if (!event.pressed && !m_pointerInside && m_showing && m_countdown == 0) startCountdown();
    }
    break;
  case PointerEvent::Type::Axis:
    break;
  }
  if (m_surface != nullptr && m_root != nullptr && (m_root->paintDirty() || m_root->layoutDirty())) {
    m_surface->requestRedraw();
  }
  return consumed;
}
