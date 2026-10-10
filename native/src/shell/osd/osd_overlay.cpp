#include "shell/osd/osd_overlay.h"

#include "compositors/compositor_platform.h"
#include "config/config_service.h"
#include "config/config_types.h"
#include "core/deferred_call.h"
#include "core/log.h"
#include "core/ui_phase.h"
#include "ipc/ipc_arg_parse.h"
#include "ipc/ipc_service.h"
#include "render/core/color.h"
#include "render/core/renderer.h"
#include "render/render_context.h"
#include "render/scene/node.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "ui/builders.h"
#include "ui/palette.h"
#include "wayland/wayland_connection.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>

// The on-screen display: one pill that springs in from its edge, morphs its width between kinds while it
// is up and is unmapped when idle. Side positions use a vertical pill. Durations go through the shell's
// motion speed, which config/kusanagi_import.cpp sets from look.animSpeed.

namespace {

  constexpr Logger kLog("osd");

  constexpr float kSlide = 18.0F;
  constexpr float kHiddenScale = 0.88F;
  constexpr int kSegments = 16;
  constexpr float kTextLabelMax = 320.0F; // wifi, media and layout names are ellipsized past this
  constexpr const char* kIconFont = "JetBrainsMono Nerd Font";

  // The layer surface is a strip along its edge, only as long as the pill can get.
  constexpr std::uint32_t kStripLength = 600;
  constexpr std::uint32_t kStripDepth = 110;
  constexpr std::uint32_t kSideLength = 320;  // side pills reach 240 px, plus room for the spring and shadow
  constexpr float kBoxSize = 196.0F;
  constexpr float kBoxMargin = 48.0F; // room around the box for the spring and shadow

  // Easing curves; `s` is the overshoot, from kusanagi::bounce().
  float easeLinear(float t, float /*s*/) { return t; }
  float easeOutBack(float t, float s) {
    const float u = t - 1.0F;
    return u * u * ((s + 1.0F) * u + s) + 1.0F;
  }
  float easeInCubic(float t, float /*s*/) { return t * t * t; }
  float easeOutCubic(float t, float /*s*/) {
    const float u = 1.0F - t;
    return 1.0F - u * u * u;
  }
  float easeOutQuint(float t, float /*s*/) {
    const float u = 1.0F - t;
    return 1.0F - u * u * u * u * u;
  }

  std::string utf8(char32_t cp) {
    std::string out;
    if (cp < 0x80) {
      out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
  }

  // What the pill shows: a Nerd Font icon, a label, a 0..1 level or none, and whether it is muted or off.
  struct View {
    char32_t icon = 0xf02fc;
    std::string label;
    bool hasBar = false;
    float level = 0.0F;
    std::string number;
    bool dim = false;
  };

  // Maps Tabler glyph names to the Material Design Nerd Font icons Kusanagi uses elsewhere.
  char32_t iconFor(const std::string& name) {
    struct Entry {
      const char* name;
      char32_t cp;
    };
    static constexpr Entry kIcons[] = {
        {"volume-mute", 0xf0581},     {"volume-low", 0xf057f},        {"volume-high", 0xf057e},
        {"microphone", 0xf036c},      {"microphone-mute", 0xf036d},   {"microphone-off", 0xf036d},
        {"brightness-low", 0xf00dd},  {"brightness-high", 0xf00df},   {"wifi", 0xf05a9},
        {"wifi-off", 0xf05aa},        {"bluetooth", 0xf00af},         {"bluetooth-off", 0xf00b2},
        {"caffeine-on", 0xf0176},     {"caffeine-off", 0xf0176},      {"nightlight-on", 0xf0594},
        {"nightlight-off", 0xf0594},  {"nightlight-forced", 0xf0594}, {"bell", 0xf009a},
        {"bell-off", 0xf009b},        {"keyboard", 0xf030c},          {"keyboard-off", 0xf030c},
        {"capslock", 0xf030c},        {"numlock", 0xf030c},           {"scrolllock", 0xf030c},
        {"disc", 0xf075a},            {"camera", 0xf0100},            {"camera-off", 0xf05df},
        {"screen-share", 0xf0379},    {"screen-share-off", 0xf0379},  {"performance", 0xf04c5},
        {"powersaver", 0xf032a},      {"balanced", 0xf05d1},          {"adjustments", 0xf062e},
        {"gamemode", 0xf0297},
    };
    for (const auto& e : kIcons) {
      if (name == e.name) {
        return e.cp;
      }
    }
    return 0xf02fc;
  }

  // The trailing number of "65%" or "Spotify - 65%". Reading it from the text keeps values over 100 %
  // that the bar clamps away.
  std::string trailingNumber(const std::string& text, float progress) {
    std::size_t end = text.size();
    while (end > 0 && !std::isdigit(static_cast<unsigned char>(text[end - 1]))) {
      --end;
    }
    std::size_t begin = end;
    while (begin > 0 && std::isdigit(static_cast<unsigned char>(text[begin - 1]))) {
      --begin;
    }
    if (begin < end && end + 2 >= text.size()) {
      return text.substr(begin, end - begin);
    }
    return std::to_string(static_cast<int>(std::lround(std::clamp(progress, 0.0F, 1.0F) * 100.0F)));
  }

  View viewFor(const OsdContent& c) {
    View v;
    v.dim = c.inactive;
    v.hasBar = c.showProgress;
    v.level = std::clamp(c.progress, 0.0F, 1.0F);
    if (v.hasBar) {
      v.number = trailingNumber(c.value, c.progress);
    }
    v.icon = iconFor(c.icon);
    v.label = c.value;
    switch (c.kind) {
    case OsdKind::Volume:
      if (v.hasBar) {
        v.icon = v.dim || v.number == "0" ? 0xf0581 : v.level < 0.34F ? 0xf057f : v.level < 0.67F ? 0xf0580 : 0xf057e;
        v.label = v.dim ? "Muted" : "Volume";
      }
      break;
    case OsdKind::Microphone:
      v.icon = v.dim ? 0xf036d : 0xf036c;
      if (v.hasBar) {
        v.label = v.dim ? "Mic muted" : "Microphone";
      }
      break;
    case OsdKind::Brightness:
      v.icon = v.level < 0.34F ? 0xf00dd : v.level < 0.67F ? 0xf00de : 0xf00df;
      v.label = "Brightness";
      break;
    case OsdKind::KeyboardBacklight:
      if (v.hasBar) {
        v.label = "Keyboard light";
      }
      break;
    case OsdKind::GameMode:
      v.icon = 0xf0297;
      v.hasBar = false;
      break;
    default:
      break;
    }
    return v;
  }

  [[nodiscard]] bool isOsdKindEnabled(const OsdKindsConfig& kinds, OsdKind kind) {
    switch (kind) {
    case OsdKind::Volume:
      return kinds.volume && kinds.volumeOutput; // osd.volume
    case OsdKind::Microphone:
      return kinds.volumeInput; // osd.mic, independent of osd.volume
    case OsdKind::Brightness:
      return kinds.brightness;
    case OsdKind::Wifi:
      return kinds.wifi;
    case OsdKind::Bluetooth:
      return kinds.bluetooth;
    case OsdKind::PowerProfile:
      return kinds.powerProfile;
    case OsdKind::Caffeine:
      return kinds.caffeine;
    case OsdKind::NightLight:
      return kinds.nightlight;
    case OsdKind::Dnd:
      return kinds.dnd;
    case OsdKind::LockKeys:
      return kinds.lockKeys;
    case OsdKind::KeyboardLayout:
      return kinds.keyboardLayout;
    case OsdKind::Media:
      return kinds.media;
    case OsdKind::Privacy:
      return kinds.privacy;
    case OsdKind::KeyboardBacklight:
      return kinds.keyboardBacklight;
    case OsdKind::GameMode:
      return kusanagi::opt<bool>("osd", "gamemode", true);
    }
    return true;
  }

  ColorSpec role(ColorRole r, float a = 1.0F) { return colorSpecFromRole(r, a); }

  // Matches the classic look for Nerd Font icons: the icon's box is as wide as the glyph's ink reaches (at
  // least its advance) and one line tall, the advance is centred in it, and the ink sits at the glyph's
  // natural height. That puts the icons a little right of centre, which is visible. A plain Label
  // draws from its left edge and centres the ink vertically instead, so lay out with width/height and
  // offset the Label by dx/dy.
  struct IconBox {
    float width = 0.0F;
    float height = 0.0F;
    float dx = 0.0F;
    float dy = 0.0F;
  };
  // Line box from the font's unhinted ascent + descent (the hinted extents Label uses run 1 to 2 px
  // taller), with the baseline one ascent below the top. This keeps the text where it always sat.
  struct LineMetrics {
    float ascent = 0.0F;
    float lineHeight = 0.0F;
  };
  LineMetrics lineMetrics(Renderer& renderer, float size, FontWeight weight, std::string_view family) {
    const TextMetrics big = renderer.measureFont(1000.0F, weight, family);
    return LineMetrics{
        .ascent = -big.top / 1000.0F * size,
        .lineHeight = std::round((big.bottom - big.top) / 1000.0F * size),
    };
  }

  // The y that puts a Label's baseline one ascent below `top`. A plain Label sits the baseline half
  // a cap height below its box centre.
  float baselineLabelY(Renderer& renderer, const Label& label, float top, FontWeight weight) {
    const LineMetrics lm = lineMetrics(renderer, label.fontSize(), weight, kusanagi::font());
    const float capHeight = renderer.measureFont(label.fontSize(), weight, kusanagi::font()).capHeight;
    return top + lm.ascent - (label.height() + capHeight) / 2.0F;
  }

  IconBox iconBox(Renderer& renderer, const Label& icon) {
    const TextMetrics m = renderer.measureText(
        icon.text(), icon.fontSize(), FontWeight::Normal, 0.0F, 0, TextAlign::Start, kIconFont
    );
    const float inkHeight = std::max(0.0F, m.inkBottom - m.inkTop);
    const LineMetrics lm = lineMetrics(renderer, icon.fontSize(), FontWeight::Normal, kIconFont);
    const float width = std::max(m.width, m.inkRight);
    return IconBox{
        .width = width,
        .height = lm.lineHeight,
        .dx = (width - m.width) / 2.0F,
        .dy = (lm.ascent + m.inkTop) - (icon.height() - inkHeight) * 0.5F,
    };
  }

} // namespace

OsdContent gameModeOsdContent(bool active) {
  return OsdContent{
      .kind = OsdKind::GameMode,
      .icon = "gamemode",
      .value = active ? "Game mode on" : "Game mode off",
      .showProgress = false,
      .inactive = !active,
  };
}

OsdOverlay::OsdOverlay() = default;

OsdOverlay::~OsdOverlay() = default;

void OsdOverlay::initialize(
    WaylandConnection& wayland, CompositorPlatform& platform, ConfigService* config, RenderContext* renderContext
) {
  m_wayland = &wayland;
  m_platform = &platform;
  m_config = config;
  m_renderContext = renderContext;
  m_lastConfiguredEnabled = m_config == nullptr || m_config->config().osd.enabled;
}

void OsdOverlay::registerIpc(IpcService& ipc) {
  ipc.bind(kusanagi::cli::msg::osdEnable, [this](const std::string& args) -> std::string {
    if (!kusanagi::ipc::splitWords(args).empty()) {
      return "error: osd-enable takes no arguments\n";
    }
    setEnabledOverride(true);
    return "ok\n";
  });
  ipc.bind(kusanagi::cli::msg::osdDisable, [this](const std::string& args) -> std::string {
    if (!kusanagi::ipc::splitWords(args).empty()) {
      return "error: osd-disable takes no arguments\n";
    }
    setEnabledOverride(false);
    return "ok\n";
  });
  ipc.bind(kusanagi::cli::msg::osdToggle, [this](const std::string& args) -> std::string {
    if (!kusanagi::ipc::splitWords(args).empty()) {
      return "error: osd-toggle takes no arguments\n";
    }
    setEnabledOverride(!isEnabled());
    return isEnabled() ? "on\n" : "off\n";
  });
}

bool OsdOverlay::isEnabled() const noexcept {
  const bool configuredEnabled = m_config == nullptr || m_config->config().osd.enabled;
  return m_runtimeEnabledOverride.value_or(configuredEnabled);
}

void OsdOverlay::setEnabledOverride(bool enabled) {
  m_runtimeEnabledOverride = enabled;
  if (!enabled) {
    destroySurfaces();
  }
}

void OsdOverlay::requestRedraw() {
  for (auto& inst : m_instances) {
    if (inst->surface != nullptr) {
      inst->surface->requestRedraw();
    }
  }
}

void OsdOverlay::requestLayout() {
  for (auto& inst : m_instances) {
    if (inst->surface != nullptr) {
      inst->surface->requestLayout();
    }
  }
}

void OsdOverlay::show(const OsdContent& content) {
  if (m_config != nullptr && !isOsdKindEnabled(m_config->config().osd.kinds, content.kind)) {
    return;
  }
  showContent(content);
}

void OsdOverlay::preview(const OsdContent& content) { showContent(content); }

void OsdOverlay::showContent(const OsdContent& content) {
  if (m_wayland == nullptr || m_renderContext == nullptr || !isEnabled()) {
    return;
  }
  const bool followFocusedOutput = m_config != nullptr && m_config->config().osd.followFocusedOutput;
  if (followFocusedOutput && !isVisible() && m_platform != nullptr) {
    m_targetOutput = m_platform->preferredInteractiveOutput();
  }
  m_content = content;
  ensureSurfaces();
  for (auto& inst : m_instances) {
    if (inst->surface == nullptr) {
      continue;
    }
    inst->showPending = true;
    inst->surface->requestUpdate();
  }
}

bool OsdOverlay::isVisible() const {
  return std::ranges::any_of(m_instances, [](const auto& inst) { return inst->showing || inst->showPending; });
}

std::vector<std::string> OsdOverlay::osdMonitors() const {
  if (m_config == nullptr) {
    return {};
  }
  return m_config->config().osd.monitors;
}

bool OsdOverlay::shouldRenderOnOutput(const WaylandOutput& output) const {
  if (m_followFocusedOutput) {
    return output.output == m_targetOutput;
  }
  const auto selectedMonitors = osdMonitors();
  if (selectedMonitors.empty()) {
    return true;
  }
  return std::ranges::any_of(selectedMonitors, [&output](const std::string& match) {
    return outputMatchesSelector(match, output);
  });
}

void OsdOverlay::onOutputChange() {
  if (m_instances.empty()) {
    return;
  }
  const bool wasVisible = isVisible();
  ensureSurfaces();
  if (wasVisible) {
    for (auto& inst : m_instances) {
      if (inst->surface != nullptr && !inst->showing && !inst->showPending) {
        inst->showPending = true;
        inst->surface->requestUpdate();
      }
    }
  }
  requestLayout();
}

void OsdOverlay::onConfigReload() {
  const bool configuredEnabled = m_config == nullptr || m_config->config().osd.enabled;
  if (configuredEnabled != m_lastConfiguredEnabled) {
    m_runtimeEnabledOverride.reset();
    m_lastConfiguredEnabled = configuredEnabled;
  }
  if (!isEnabled()) {
    destroySurfaces();
    return;
  }
  onOutputChange();
}

void OsdOverlay::ensureSurfaces() {
  if (m_wayland == nullptr || m_renderContext == nullptr) {
    return;
  }

  const auto style = kusanagi::opt<std::string>("osd", "style", "pill");
  const auto position = kusanagi::opt<std::string>("osd", "position", "top");
  m_minimal = style == "minimal";
  m_showValue = kusanagi::opt<bool>("osd", "showValue", true);
  m_mode = style == "box"         ? Mode::Box
      : position == "bottom"      ? Mode::Bottom
      : position == "left"        ? Mode::Left
      : position == "right"       ? Mode::Right
                                  : Mode::Top;

  m_followFocusedOutput = m_config != nullptr && m_config->config().osd.followFocusedOutput;
  if (!m_followFocusedOutput) {
    m_targetOutput = nullptr;
  } else {
    const WaylandOutput* targetOutput = m_targetOutput != nullptr ? m_wayland->findOutputByWl(m_targetOutput) : nullptr;
    if (targetOutput == nullptr || !targetOutput->done || !targetOutput->hasUsableGeometry()) {
      m_targetOutput = m_platform != nullptr ? m_platform->preferredInteractiveOutput() : nullptr;
    }
  }
  const auto selectedMonitors = m_followFocusedOutput ? std::vector<std::string>{} : osdMonitors();

  // Anything that changes the surfaces or the scene rebuilds from scratch; the OSD is short-lived anyway.
  const std::string layoutKey = style + "|" + position + "|" + (m_showValue ? "v" : "") + "|"
      + kusanagi::font() + "|" + std::to_string(kusanagi::radius()) + "|"
      + std::to_string(kusanagi::surfaceBorderWidth()) + "|" + (kusanagi::shadows() ? "s" : "");
  if (!m_instances.empty() && (layoutKey != m_lastLayoutKey || selectedMonitors != m_lastMonitorSelectors)) {
    destroySurfaces();
  }
  m_lastLayoutKey = layoutKey;
  m_lastMonitorSelectors = selectedMonitors;

  const bool anyConfiguredPresent =
      selectedMonitors.empty()
      || std::any_of(m_wayland->outputs().begin(), m_wayland->outputs().end(), [this](const WaylandOutput& output) {
           return output.done && output.output != nullptr && output.hasUsableGeometry() && shouldRenderOnOutput(output);
         });

  std::erase_if(m_instances, [this, anyConfiguredPresent](const std::unique_ptr<Instance>& inst) {
    if (inst->output == nullptr) {
      return true;
    }
    const WaylandOutput* wlOutput = m_wayland->findOutputByWl(inst->output);
    if (wlOutput == nullptr || !wlOutput->done || !wlOutput->hasUsableGeometry()) {
      return true;
    }
    return anyConfiguredPresent && !shouldRenderOnOutput(*wlOutput);
  });

  for (const auto& output : m_wayland->outputs()) {
    if (!output.done || output.output == nullptr || !output.hasUsableGeometry()) {
      continue;
    }
    if (anyConfiguredPresent && !shouldRenderOnOutput(output)) {
      continue;
    }
    const float outputHeight = static_cast<float>(
        output.logicalHeight > 0 ? output.logicalHeight : output.height / std::max(1, output.scale)
    );

    auto existingIt = std::ranges::find_if(m_instances, [&output](const auto& inst) {
      return inst != nullptr && inst->output == output.output;
    });
    if (existingIt != m_instances.end()) {
      if (std::abs((*existingIt)->outputHeight - outputHeight) < 0.5F) {
        continue;
      }
      m_instances.erase(existingIt); // box mode sizes by the output height
    }

    auto inst = std::make_unique<Instance>();
    inst->output = output.output;
    inst->outputHeight = outputHeight;

    std::uint32_t anchor = LayerShellAnchor::Top;
    std::uint32_t width = kStripLength;
    std::uint32_t height = kStripDepth;
    switch (m_mode) {
    case Mode::Top:
      break;
    case Mode::Bottom:
      anchor = LayerShellAnchor::Bottom;
      break;
    case Mode::Left:
    case Mode::Right:
      anchor = m_mode == Mode::Left ? LayerShellAnchor::Left : LayerShellAnchor::Right;
      width = kStripDepth;
      height = kSideLength;
      break;
    case Mode::Box: {
      // From just above the square (centred at 68 % of the screen height) down to the bottom edge.
      const float top = std::round(outputHeight * 0.68F - kBoxSize / 2.0F) - kBoxMargin;
      anchor = LayerShellAnchor::Bottom;
      width = static_cast<std::uint32_t>(kBoxSize + kBoxMargin * 2.0F);
      height = static_cast<std::uint32_t>(std::max(1.0F, outputHeight - std::max(0.0F, top)));
      break;
    }
    }

    auto surfaceConfig = LayerSurfaceConfig{
        .nameSpace = "kusanagi-osd",
        .layer = LayerShellLayer::Overlay,
        .anchor = anchor,
        .width = width,
        .height = height,
        .exclusiveZone = -1, // placed from the screen edge, not the bar
        .keyboard = LayerShellKeyboard::None,
        .defaultWidth = width,
        .defaultHeight = height,
    };

    inst->surface = std::make_unique<LayerSurface>(*m_wayland, std::move(surfaceConfig));
    inst->surface->setRenderContext(m_renderContext);
    auto* instPtr = inst.get();
    inst->surface->setConfigureCallback([instPtr](std::uint32_t /*width*/, std::uint32_t /*height*/) {
      instPtr->surface->requestLayout();
    });
    inst->surface->setPrepareFrameCallback([this, instPtr](bool needsUpdate, bool /*needsLayout*/) {
      prepareFrame(*instPtr, needsUpdate);
    });
    inst->surface->setAnimationManager(&inst->animations);

    if (!inst->surface->initialize(output.output)) {
      kLog.warn("osd overlay: failed to initialize surface on {}", output.connectorName);
      continue;
    }

    inst->surface->setInputRegion({}); // click-through
    m_instances.push_back(std::move(inst));
  }
}

void OsdOverlay::destroySurfaces() {
  for (auto& inst : m_instances) {
    inst->animations.cancelAll();
  }
  m_instances.clear();
}

void OsdOverlay::prepareFrame(Instance& inst, bool needsUpdate) {
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
      || static_cast<std::uint32_t>(std::round(inst.sceneRoot->width())) != width
      || static_cast<std::uint32_t>(std::round(inst.sceneRoot->height())) != height;
  if (needsSceneBuild) {
    UiPhaseScope layoutPhase(UiPhase::Layout);
    buildScene(inst, width, height);
  }

  if ((needsUpdate && inst.showPending) || needsSceneBuild) {
    UiPhaseScope layoutPhase(UiPhase::Layout);
    updateInstanceContent(inst);
    if (inst.showPending) {
      startShow(inst);
      inst.showPending = false;
    }
    applyMotion(inst);
  }
}

void OsdOverlay::buildScene(Instance& inst, std::uint32_t width, std::uint32_t height) {
  uiAssertNotRendering("OsdOverlay::buildScene");
  if (inst.surface == nullptr) {
    return;
  }
  // A rebuild starts from the hidden state; the pill only exists while it is mapped.
  inst.animations.cancelAll();
  inst.widthAnim = inst.levelAnim = inst.slideAnim = inst.scaleAnim = inst.opacityAnim = inst.hideTimer = 0;
  inst.showing = false;
  inst.slide = kSlide;
  inst.scale = kHiddenScale;
  inst.opacity = 0.0F;
  inst.width = 0.0F;
  inst.segments.clear();

  inst.sceneRoot = ui::node({});
  inst.sceneRoot->setSize(static_cast<float>(width), static_cast<float>(height));
  inst.surface->setSceneRoot(inst.sceneRoot.get());

  auto pill = ui::node({.out = &inst.pill});
  pill->setOpacity(0.0F);

  if (kusanagi::shadows()) {
    auto shadow = ui::box({.out = &inst.shadow});
    shadow->setStyle(RoundedRectStyle{
        .fill = rgba(0.0F, 0.0F, 0.0F, 0.4F),
        .softness = 16.0F,
        .outerShadow = true,
        .shadowCutoutOffsetY = 6.0F,
    });
    shadow->setZIndex(0);
    pill->addChild(std::move(shadow));
  }

  const float panelOpacity = static_cast<float>(kusanagi::opt<double>("panel", "opacity", 0.94));
  pill->addChild(ui::box({
      .out = &inst.background,
      .fill = role(ColorRole::Surface, std::max(0.88F, panelOpacity)),
      .border = kusanagi::surfaceBorder(),
      .borderWidth = kusanagi::surfaceBorderWidth(),
      .configure = [](Box& box) { box.setZIndex(1); },
  }));

  const bool box = m_mode == Mode::Box;
  const bool vertical = m_mode == Mode::Left || m_mode == Mode::Right;

  if (!box && !vertical && !m_minimal) {
    pill->addChild(ui::box({
        .out = &inst.circle,
        .radius = 15.0F,
        .width = 30.0F,
        .height = 30.0F,
        .configure = [](Box& b) { b.setZIndex(2); },
    }));
  }
  const float iconSize = box ? 64.0F : vertical ? 18.0F : m_minimal ? 14.0F : 16.0F;
  pill->addChild(ui::label({
      .out = &inst.icon,
      .text = utf8(0xf057e),
      .fontSize = iconSize,
      .fontFamily = std::string(kIconFont),
      .configure = [](Label& l) { l.setZIndex(3); },
  }));
  if (!vertical) {
    pill->addChild(ui::label({
        .out = &inst.label,
        .text = "",
        .fontSize = 12.0F,
        .fontWeight = FontWeight::Bold,
        .fontFamily = kusanagi::font(),
        .maxLines = 1,
        .ellipsize = TextEllipsize::End,
        .configure = [](Label& l) { l.setZIndex(3); },
    }));
  }
  pill->addChild(ui::label({
      .out = &inst.value,
      .text = "",
      .fontSize = vertical ? 11.0F : 12.0F,
      .fontWeight = vertical ? FontWeight::Bold : FontWeight::Normal,
      .fontFamily = kusanagi::font(),
      .color = role(vertical ? ColorRole::OnSurface : ColorRole::OnSurfaceVariant),
      .maxLines = 1,
      .configure = [](Label& l) { l.setZIndex(3); },
  }));

  if (box) {
    for (int i = 0; i < kSegments; ++i) {
      Box* seg = nullptr;
      pill->addChild(ui::box({
          .out = &seg,
          .radius = 1.5F,
          .width = 7.0F,
          .height = 6.0F,
          .configure = [](Box& b) { b.setZIndex(2); },
      }));
      inst.segments.push_back(seg);
    }
  } else {
    const float thickness = (m_minimal && !vertical) ? 4.0F : 6.0F;
    pill->addChild(ui::box({
        .out = &inst.track,
        .fill = role(ColorRole::OnSurface, 0.12F),
        .radius = thickness / 2.0F,
        .configure = [](Box& b) { b.setZIndex(2); },
    }));
    pill->addChild(ui::box({
        .out = &inst.fill,
        .radius = std::min(3.0F, thickness / 2.0F),
        .configure = [](Box& b) { b.setZIndex(3); },
    }));
  }

  inst.sceneRoot->addChild(std::move(pill));
}

void OsdOverlay::updateInstanceContent(Instance& inst) {
  if (inst.surface == nullptr || inst.pill == nullptr || inst.icon == nullptr || inst.value == nullptr) {
    return;
  }
  Renderer& renderer = inst.surface->renderTarget().renderer();
  const View v = viewFor(m_content);
  const bool box = m_mode == Mode::Box;
  const bool vertical = m_mode == Mode::Left || m_mode == Mode::Right;

  const ColorSpec text = role(v.dim ? ColorRole::OnSurfaceVariant : ColorRole::OnSurface);
  const ColorSpec accent = role(v.dim ? ColorRole::OnSurfaceVariant : ColorRole::Primary);

  inst.hasBar = v.hasBar;
  inst.targetLevel = v.level;

  inst.icon->setText(utf8(v.icon));
  inst.icon->measure(renderer);
  const IconBox icon = iconBox(renderer, *inst.icon);
  if (inst.label != nullptr) {
    inst.label->setText(v.label);
    inst.label->setColor(text);
    inst.label->setMaxWidth(v.hasBar && !box ? 92.0F : kTextLabelMax);
    inst.label->measure(renderer);
  }
  if (inst.fill != nullptr) {
    inst.fill->setFill(accent);
  }

  // Box: a big icon, the name and a segmented level, centred in the square.
  if (box) {
    inst.icon->setColor(text);
    inst.value->setVisible(false);
    const float labelH = lineMetrics(renderer, 12.0F, FontWeight::Bold, kusanagi::font()).lineHeight;
    const float columnH = icon.height + 14.0F + labelH + (v.hasBar ? 14.0F + 6.0F : 0.0F);
    float y = std::round((kBoxSize - columnH) / 2.0F);
    inst.icon->setPosition((kBoxSize - icon.width) / 2.0F + icon.dx, y + icon.dy);
    y += icon.height + 14.0F;
    inst.label->setPosition((kBoxSize - inst.label->width()) / 2.0F, baselineLabelY(renderer, *inst.label, y, FontWeight::Bold));
    y += labelH + 14.0F;
    const int lit = static_cast<int>(std::lround(v.level * kSegments));
    const float rowW = kSegments * 7.0F + (kSegments - 1) * 2.0F;
    float x = (kBoxSize - rowW) / 2.0F;
    for (int i = 0; i < static_cast<int>(inst.segments.size()); ++i) {
      Box* seg = inst.segments[static_cast<std::size_t>(i)];
      seg->setVisible(v.hasBar);
      seg->setFill(i < lit ? accent : role(ColorRole::OnSurface, 0.14F));
      seg->setPosition(x, y);
      x += 9.0F;
    }
    inst.targetWidth = kBoxSize;
    inst.pillHeight = kBoxSize;
    return;
  }

  // Vertical pill on a side edge: the number, the bar, then the icon. An empty number takes no room.
  if (vertical) {
    const float w = m_minimal ? 34.0F : 52.0F;
    const float h = v.hasBar ? (m_minimal ? 200.0F : 240.0F) : 52.0F;
    const bool showValue = v.hasBar && m_showValue;
    inst.icon->setColor(accent);
    inst.value->setVisible(showValue);
    inst.track->setVisible(v.hasBar);
    inst.fill->setVisible(v.hasBar);
    float y = 14.0F;
    if (showValue) {
      inst.value->setText(v.number);
      inst.value->measure(renderer);
      const float valueH = lineMetrics(renderer, 11.0F, FontWeight::Bold, kusanagi::font()).lineHeight;
      inst.value->setPosition((w - inst.value->width()) / 2.0F, baselineLabelY(renderer, *inst.value, y, FontWeight::Bold));
      y += valueH + 10.0F;
    }
    if (v.hasBar) {
      // At most 150 px, but never long enough to push the icon out of the slimmer minimal pill.
      inst.trackLength = std::clamp(h - y - 10.0F - icon.height - 4.0F, 40.0F, 150.0F);
      inst.track->setPosition(std::round((w - 6.0F) / 2.0F), y);
      inst.track->setSize(6.0F, inst.trackLength);
      y += inst.trackLength + 10.0F;
    }
    inst.icon->setPosition((w - icon.width) / 2.0F + icon.dx, y + icon.dy);
    inst.targetWidth = w;
    inst.pillHeight = h;
    return;
  }

  // Horizontal pill, vertically centred.
  const float h = m_minimal ? 30.0F : 46.0F;
  const float spacing = m_minimal ? 10.0F : 12.0F;
  const float left = m_minimal ? 14.0F : 8.0F;
  float x = left;
  if (inst.circle != nullptr) {
    const float cy = (h - 30.0F) / 2.0F;
    inst.circle->setFill(v.dim ? role(ColorRole::OnSurface, 0.12F) : role(ColorRole::Primary));
    inst.circle->setPosition(x, cy);
    inst.icon->setColor(role(v.dim ? ColorRole::OnSurfaceVariant : ColorRole::Surface));
    inst.icon->setPosition(
        x + (30.0F - icon.width) / 2.0F + icon.dx, cy + (30.0F - icon.height) / 2.0F + icon.dy
    );
    x += 30.0F + spacing;
  } else {
    inst.icon->setColor(accent);
    inst.icon->setPosition(x + icon.dx, (h - icon.height) / 2.0F + icon.dy);
    x += icon.width + spacing;
  }
  const bool showLabel = !m_minimal || !v.hasBar;
  inst.label->setVisible(showLabel);
  if (showLabel) {
    const float labelH = lineMetrics(renderer, 12.0F, FontWeight::Bold, kusanagi::font()).lineHeight;
    inst.label->setPosition(x, baselineLabelY(renderer, *inst.label, (h - labelH) / 2.0F, FontWeight::Bold));
    x += (v.hasBar ? 92.0F : inst.label->width()) + spacing;
  }
  inst.track->setVisible(v.hasBar);
  inst.fill->setVisible(v.hasBar);
  if (v.hasBar) {
    inst.trackLength = m_minimal ? 170.0F : 130.0F;
    const float thickness = m_minimal ? 4.0F : 6.0F;
    inst.track->setPosition(x, (h - thickness) / 2.0F);
    inst.track->setSize(inst.trackLength, thickness);
    x += inst.trackLength + spacing;
  }
  const bool showValue = v.hasBar && m_showValue;
  inst.value->setVisible(showValue);
  if (showValue) {
    inst.value->setText(v.number);
    inst.value->measure(renderer);
    const float valueH = lineMetrics(renderer, 12.0F, FontWeight::Normal, kusanagi::font()).lineHeight;
    inst.value->setPosition(
        x + 30.0F - inst.value->width(), baselineLabelY(renderer, *inst.value, (h - valueH) / 2.0F, FontWeight::Normal)
    );
    x += 30.0F + spacing;
  }
  const float rowWidth = x - spacing - left;
  inst.targetWidth = m_minimal ? rowWidth + 28.0F : v.hasBar ? rowWidth + 24.0F : rowWidth + 40.0F;
  inst.pillHeight = h;
}

void OsdOverlay::tween(
    Instance& inst, AnimationManager::Id& id, float& value, float to, float durationMs, float (*ease)(float, float),
    float overshoot, std::function<void()> done
) {
  if (id != 0) {
    inst.animations.cancel(id);
    id = 0;
  }
  if (std::abs(value - to) < 1.0e-4F) {
    value = to;
    applyMotion(inst);
    if (done) {
      done();
    }
    return;
  }
  const float from = value;
  Instance* instPtr = &inst;
  auto token = std::make_shared<AnimationManager::Id>(0);
  id = inst.animations.animate(
      0.0F, 1.0F, durationMs, Easing::Linear,
      [this, instPtr, &value, from, to, ease, overshoot](float t) {
        value = from + (to - from) * ease(t, overshoot);
        applyMotion(*instPtr);
      },
      [&id, token, done = std::move(done)]() {
        if (id == *token) {
          id = 0;
        }
        if (done) {
          done();
        }
      }
  );
  *token = id;
}

void OsdOverlay::startShow(Instance& inst) {
  if (inst.hideTimer != 0) {
    inst.animations.cancel(inst.hideTimer);
    inst.hideTimer = 0;
  }
  const bool fresh = inst.opacity <= 0.01F;
  if (fresh) {
    // Nothing on screen yet, so no width morph and no bar sweep.
    inst.width = inst.targetWidth;
    inst.level = inst.targetLevel;
  } else {
    tween(inst, inst.widthAnim, inst.width, inst.targetWidth, 280.0F, easeOutQuint, 0.0F);
    tween(inst, inst.levelAnim, inst.level, inst.targetLevel, 160.0F, easeOutCubic, 0.0F);
  }
  if (!inst.showing) {
    inst.showing = true;
    tween(inst, inst.slideAnim, inst.slide, 0.0F, 420.0F, easeOutBack, kusanagi::bounce(1.4F));
    tween(inst, inst.scaleAnim, inst.scale, 1.0F, 420.0F, easeOutBack, kusanagi::bounce(1.6F));
    tween(inst, inst.opacityAnim, inst.opacity, 1.0F, 200.0F, easeLinear, 0.0F);
  }

  const auto timeout = static_cast<float>(std::max(200, kusanagi::opt<int>("osd", "timeout", 1400)));
  Instance* instPtr = &inst;
  inst.hideTimer = inst.animations.animateTimer(
      0.0F, 1.0F, timeout, Easing::Linear, [](float /*v*/) {},
      [this, instPtr]() {
        instPtr->hideTimer = 0;
        startHide(*instPtr);
      }
  );
}

void OsdOverlay::startHide(Instance& inst) {
  inst.showing = false;
  tween(inst, inst.slideAnim, inst.slide, kSlide, 200.0F, easeInCubic, 0.0F);
  tween(inst, inst.scaleAnim, inst.scale, kHiddenScale, 200.0F, easeInCubic, 0.0F);
  tween(inst, inst.opacityAnim, inst.opacity, 0.0F, 180.0F, easeLinear, 0.0F, [this]() {
    DeferredCall::callLater([this]() {
      const bool allIdle = std::ranges::all_of(m_instances, [](const auto& i) {
        return !i->showing && !i->showPending && i->opacity <= 0.01F;
      });
      if (allIdle) {
        destroySurfaces(); // unmapped when idle
        m_targetOutput = nullptr;
      }
    });
  });
}

void OsdOverlay::applyMotion(Instance& inst) {
  if (inst.pill == nullptr || inst.sceneRoot == nullptr || inst.background == nullptr) {
    return;
  }
  const float sw = inst.sceneRoot->width();
  const float sh = inst.sceneRoot->height();
  const float w = inst.width;
  const float h = inst.pillHeight;

  float x = std::round((sw - w) / 2.0F);
  float y = 0.0F;
  switch (m_mode) {
  case Mode::Top:
    y = 40.0F - inst.slide;
    break;
  case Mode::Bottom:
    y = sh - h - 30.0F + inst.slide;
    break;
  case Mode::Left:
    x = 18.0F - inst.slide;
    y = std::round((sh - h) / 2.0F);
    break;
  case Mode::Right:
    x = sw - w - 18.0F + inst.slide;
    y = std::round((sh - h) / 2.0F);
    break;
  case Mode::Box: {
    const float screenTop = inst.outputHeight - sh; // the surface hangs from the bottom edge
    y = std::round(inst.outputHeight * 0.68F - h / 2.0F) - screenTop + inst.slide / 2.0F;
    break;
  }
  }

  const float radius = m_mode == Mode::Box ? std::max(18.0F, kusanagi::radius() + 6.0F) : std::min(w, h) / 2.0F;
  inst.pill->setPosition(x, y);
  inst.pill->setSize(w, h);
  inst.pill->setScale(inst.scale);
  inst.pill->setOpacity(std::clamp(inst.opacity, 0.0F, 1.0F));
  inst.background->setSize(w, h);
  inst.background->setRadius(radius);
  if (inst.shadow != nullptr) {
    inst.shadow->setPosition(0.0F, 6.0F);
    inst.shadow->setSize(w, h);
    inst.shadow->setRadius(radius);
  }

  if (inst.fill != nullptr && inst.track != nullptr && inst.hasBar) {
    const float level = std::clamp(inst.level, 0.0F, 1.0F);
    if (m_mode == Mode::Left || m_mode == Mode::Right) {
      const float fh = inst.trackLength * level;
      inst.fill->setPosition(inst.track->x(), inst.track->y() + inst.trackLength - fh);
      inst.fill->setSize(6.0F, fh);
      inst.fill->setVisible(fh > 0.5F);
    } else {
      const float thickness = m_minimal ? 4.0F : 6.0F;
      const float fw = inst.trackLength * level;
      inst.fill->setPosition(inst.track->x(), inst.track->y());
      inst.fill->setSize(fw, thickness);
      inst.fill->setVisible(fw > 0.5F);
    }
  }
  if (inst.surface != nullptr) {
    inst.surface->requestRedraw();
  }
}
