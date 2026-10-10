#include "shell/bar/widgets/kusanagi_workspaces_widget.h"

#include "compositors/compositor_platform.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"
#include "ui/palette.h"

#include "cursor-shape-v1-client-protocol.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

using json = nlohmann::json;

namespace {
  const char* kRoman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
  const char* kKanji[] = {"一", "二", "三", "四", "五", "六", "七", "八", "九", "十"};

  bool textStyle(const std::string& s) {
    return s == "numbers" || s == "roman" || s == "kanji" || s == "custom" || s == "dwl";
  }
} // namespace

KusanagiWorkspacesWidget::KusanagiWorkspacesWidget(CompositorPlatform& platform, wl_output* output, std::string specJson)
    : m_platform(platform), m_output(output) {
  nlohmann::json spec = nlohmann::json::parse(specJson, nullptr, false);
  if (!spec.is_object()) spec = nlohmann::json::object();
  m_spec = spec;
  auto pairOf = [&](const char* key, float& a, float& b) {
    const auto& v = spec[key];
    if (v.is_array() && v.size() >= 2 && v[0].is_number() && v[1].is_number()) {
      a = v[0].get<float>();
      b = v[1].get<float>();
    } else if (v.is_number()) {
      a = b = v.get<float>();
    }
  };
  pairOf("gap", m_gapStart, m_gapEnd);
  pairOf("inset", m_insetEdge, m_insetInner);
  pairOf("_groupInset", m_groupEdge, m_groupInner);
  m_farEdge = spec.value("_edge", std::string()) == "bottom" || spec.value("_edge", std::string()) == "right";
  // Module options win over the workspaces settings. icons may be a list or a space-separated string.
  m_style = spec.value("style", json()).is_string() && !spec["style"].get<std::string>().empty() ? spec["style"].get<std::string>()
                                                                                                : kusanagi::opt<std::string>("workspaces", "style", "pills");
  m_shown = kusanagi::opt<int>("workspaces", "shown", 5);
  m_activeColor = kusanagi::opt<std::string>("workspaces", "activeColor", "accent");
  m_glow = spec.value("glow", json()).is_boolean() ? spec["glow"].get<bool>() : kusanagi::opt<bool>("workspaces", "glow", true);
  std::string iconString = kusanagi::opt<std::string>("workspaces", "icons", "");
  if (const auto& ic = spec["icons"]; ic.is_array()) {
    iconString.clear();
    for (const auto& x : ic) iconString += (iconString.empty() ? "" : " ") + (x.is_string() ? x.get<std::string>() : x.dump());
  } else if (ic.is_string() && !ic.get<std::string>().empty()) {
    iconString = ic.get<std::string>();
  }
  std::istringstream icons(iconString);
  for (std::string s; icons >> s;) m_icons.push_back(s);
  // fontSize may be relative to the bar's ("+2"). Without one, dwl uses the bar's font size and the rest 12.
  const double barFont = spec.value("_barFontSize", kusanagi::opt<double>("bar", "fontSize", 11.0));
  const auto& fs = spec["fontSize"];
  if (fs.is_string() && !fs.get<std::string>().empty() && (fs.get<std::string>()[0] == '+' || fs.get<std::string>()[0] == '-')) {
    m_fontSize = static_cast<float>(barFont + std::atof(fs.get<std::string>().c_str()));
  } else if (fs.is_number() && fs.get<double>() > 0) {
    m_fontSize = fs.get<float>();
  } else {
    m_fontSize = static_cast<float>(m_style == "dwl" ? kusanagi::opt<double>("bar", "fontSize", 11.0) : 12.0);
  }
  if (const auto& pad = spec["padding"]; pad.is_array() && pad.size() >= 2) {
    m_padStart = pad[0].get<float>();
    m_padEnd = pad[1].get<float>();
  } else if (pad.is_number()) {
    m_padStart = m_padEnd = pad.get<float>();
  }
}

void KusanagiWorkspacesWidget::create() {
  auto root = ui::node({});
  root->addChild(m_box.create()); // the module's own box, behind the slots
  // Hovering highlights the whole module, never a single workspace. The cursor is the arrow unless the
  // module has a click action of its own.
  auto hover = ui::inputArea({});
  m_hoverArea = hover.get();
  m_hoverArea->setAcceptedButtons(0);
  m_hoverArea->setCursorShape(m_spec.value("_pointer", false) ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
  m_hoverArea->setOnEnter([this](const InputArea::PointerData&) {
    m_hovered = true;
    requestUpdate();
  });
  m_hoverArea->setOnLeave([this]() {
    m_hovered = false;
    requestUpdate();
  });
  root->addChild(std::move(hover));
  auto slots = ui::node({});
  m_slotsNode = slots.get();
  root->addChild(std::move(slots));
  setRoot(std::move(root));
}

std::string KusanagiWorkspacesWidget::glyph(int n) const {
  if (m_style == "roman" && n >= 1 && n <= 10) return kRoman[n - 1];
  if (m_style == "kanji" && n >= 1 && n <= 10) return kKanji[n - 1];
  if (m_style == "custom" && n >= 1 && static_cast<std::size_t>(n) <= m_icons.size()) return m_icons[n - 1];
  return std::to_string(n);
}

// 1-based slots, padded to workspaces.shown; beyond that only occupied or active ones.
std::vector<KusanagiWorkspacesWidget::Slot> KusanagiWorkspacesWidget::collect() const {
  auto list = m_platform.workspaces(m_output);
  std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
  std::vector<Slot> out;
  static const bool extWorkspace = std::getenv("HYPRLAND_INSTANCE_SIGNATURE") == nullptr && std::getenv("NIRI_SOCKET") == nullptr;
  for (std::size_t i = 0; i < list.size(); ++i) {
    const auto& w = list[i];
    int n = static_cast<int>(i) + 1;
    try {
      n = std::stoi(w.name);
    } catch (...) {
    }
    // On ext-workspace compositors (Mango) the active tag always counts as occupied.
    const bool occupied = w.occupied || (w.active && extWorkspace);
    if (n > m_shown && !occupied && !w.active) continue;
    out.push_back({n, w.id, w.active, occupied, w.urgent});
  }
  std::sort(out.begin(), out.end(), [](const Slot& a, const Slot& b) { return a.n < b.n; });
  return out;
}

void KusanagiWorkspacesWidget::doUpdate(Renderer& /*renderer*/) {
  auto slots = collect();
  const bool changed = slots.size() != m_slots.size()
      || !std::equal(slots.begin(), slots.end(), m_slots.begin(), [](const Slot& a, const Slot& b) {
           return a.n == b.n && a.active == b.active && a.occupied == b.occupied && a.urgent == b.urgent;
         });
  if (changed) {
    m_slots = std::move(slots);
    requestRedraw();
  }
}

void KusanagiWorkspacesWidget::rebuild(std::size_t count) {
  auto* rootNode = m_slotsNode;
  while (!rootNode->children().empty()) {
    rootNode->removeChild(rootNode->children().back().get());
  }
  m_nodes.assign(count, {});
  for (std::size_t i = 0; i < count; ++i) {
    auto& nodes = m_nodes[i];
    auto area = ui::inputArea({});
    nodes.area = area.get();
    area->addChild(ui::box({.out = &nodes.block}));
    area->addChild(ui::box({.out = &nodes.mark}));
    area->addChild(ui::box({.out = &nodes.halo}));
    area->addChild(ui::box({.out = &nodes.dot}));
    area->addChild(ui::box({.out = &nodes.underline}));
    area->addChild(ui::label({.out = &nodes.label, .maxLines = 1}));
    area->setCursorShape(m_spec.value("_pointer", false) ? WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER : WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
    area->setOnEnter([this](const InputArea::PointerData&) {
      m_hovered = true;
      requestUpdate();
    });
    area->setOnLeave([this]() {
      m_hovered = false;
      requestUpdate();
    });
    area->setOnClick([this, i](const InputArea::PointerData&) {
      if (i >= m_slots.size()) return;
      for (const auto& w : m_platform.workspaces(m_output)) {
        if (w.id == m_slots[i].id) {
          m_platform.activateWorkspace(m_output, w);
          break;
        }
      }
    });
    rootNode->addChild(std::move(area));
  }
}

void KusanagiWorkspacesWidget::doLayout(Renderer& renderer, float containerWidth, float containerHeight) {
  auto* rootNode = root();
  if (rootNode == nullptr) return;
  m_vertical = containerHeight > containerWidth;
  if (m_nodes.size() != m_slots.size()) rebuild(m_slots.size());

  const float s = m_contentScale;
  const bool text = textStyle(m_style);
  const bool dwl = m_style == "dwl";
  // colors {active, occupied, empty, urgent, onActive} override the defaults.
  const nlohmann::json colors = m_spec.value("colors", nlohmann::json::object());
  auto colorOr = [&colors](const char* key, const std::string& fallback) {
    const auto it = colors.find(key);
    return kusanagi_bar::color(it != colors.end() && it->is_string() && !it->get<std::string>().empty() ? it->get<std::string>() : fallback);
  };
  const ColorSpec active = colorOr("active", m_activeColor == "accent2" ? "accent2" : m_activeColor == "text" ? "text" : "accent");
  const ColorSpec occupied = colorOr("occupied", "text/0.85");
  const ColorSpec empty = colorOr("empty", "text/0.35");
  const ColorSpec urgent = colorOr("urgent", "danger");
  const ColorSpec onActive = colorOr("onActive", "bg");
  const ColorSpec clear = clearColorSpec();

  // The module box across the bar (minus the group's inset and its own); the slots fill it.
  const float cross = m_vertical ? containerWidth : containerHeight;
  const float edgeIn = (m_groupEdge + m_insetEdge) * s;
  const float innerIn = (m_groupInner + m_insetInner) * s;
  const float boxCross = std::max(0.0F, cross - edgeIn - innerIn);
  const float boxC0 = m_farEdge ? innerIn : edgeIn;
  const float slotCross = boxCross;
  const float capS = KusanagiBox::capSize(m_spec.value("capStart", std::string("none")), boxCross);
  const float capE = KusanagiBox::capSize(m_spec.value("capEnd", std::string("none")), boxCross);
  float along = (m_gapStart + m_padStart) * s + capS;
  for (std::size_t i = 0; i < m_slots.size(); ++i) {
    const Slot& slot = m_slots[i];
    SlotNodes& nodes = m_nodes[i];
    const bool isEmpty = !slot.occupied;

    // The label, for text styles and the active pill's number.
    nodes.label->setFontFamily(kusanagi::font());
    float len = 0.0F;
    if (text) {
      nodes.label->setText(glyph(slot.n));
      nodes.label->setFontSize(m_fontSize * s);
      nodes.label->setFontWeight(slot.active && !dwl ? FontWeight::Bold : FontWeight::Normal);
      nodes.label->setColor(dwl && (slot.active || slot.urgent) ? onActive
                            : slot.urgent                       ? urgent
                            : slot.active                       ? active
                            : nodes.hovered                     ? kusanagi::color("text")
                            : isEmpty                           ? empty
                                                                : occupied);
      nodes.label->measure(renderer);
      const float labelAlong = m_vertical ? nodes.label->height() : nodes.label->width();
      len = dwl ? std::max(slotCross, labelAlong + 14.0F * s) : labelAlong + 4.0F * s + 8.0F * s;
    }

    // Dot or pill
    const bool pill = slot.active && m_style == "pills";
    const float dotW = (pill && !m_vertical ? 26.0F : pill ? 14.0F : 10.0F) * s;
    const float dotH = (pill && m_vertical ? 26.0F : pill ? 14.0F : 10.0F) * s;
    if (!text) len = (m_vertical ? dotH : dotW) + 8.0F * s;

    const float w = m_vertical ? slotCross : len;
    const float h = m_vertical ? len : slotCross;
    nodes.area->setSize(w, h);
    nodes.area->setPosition(m_vertical ? boxC0 : along, m_vertical ? along : boxC0);
    along += len;

    nodes.dot->setVisible(!text);
    // The active dot glows when glow is on; an urgent one always does.
    const bool halo = !text && ((slot.active && m_glow) || slot.urgent);
    nodes.halo->setVisible(halo);
    if (halo) {
      const float blur = (slot.urgent ? 8.0F : 10.0F) * s;
      const float dx = m_vertical ? std::round((w - dotW) / 2) : 4.0F * s;
      const float dy = m_vertical ? 4.0F * s : std::round((h - dotH) / 2);
      Color c = resolveColorSpec(slot.urgent ? urgent : active);
      c.a *= slot.urgent ? 0.5F : 0.55F;
      // An outer shadow in the dot's shape, fading out over the blur.
      nodes.halo->setStyle(RoundedRectStyle{.fill = c, .softness = blur * 0.65F, .outerShadow = true});
      nodes.halo->setRadius(6.0F * s);
      nodes.halo->setPosition(dx, dy);
      nodes.halo->setSize(dotW, dotH);
    }
    if (!text) {
      nodes.dot->setSize(dotW, dotH);
      nodes.dot->setPosition(m_vertical ? std::round((w - dotW) / 2) : 4.0F * s, m_vertical ? 4.0F * s : std::round((h - dotH) / 2));
      nodes.dot->setRadius(6.0F * s);
      nodes.dot->setFill(slot.urgent ? urgent : nodes.hovered ? kusanagi::color("text/0.55") : slot.active ? active : isEmpty ? clear : occupied);
      nodes.dot->setBorder(isEmpty && !slot.active ? empty : clear, slot.active ? 0.0F : 1.0F * s);
    }
    // The active pill shows its number.
    nodes.label->setVisible(text || pill);
    if (pill) {
      nodes.label->setText(std::to_string(slot.n));
      nodes.label->setFontSize(10.0F * s);
      nodes.label->setFontWeight(FontWeight::Bold);
      nodes.label->setColor(onActive);
      nodes.label->measure(renderer);
      nodes.label->setPosition(nodes.dot->x() + std::round((dotW - nodes.label->width()) / 2),
                               nodes.dot->y() + std::round((dotH - nodes.label->height()) / 2));
    } else if (text) {
      const float lx = dwl || m_vertical ? std::round((w - nodes.label->width()) / 2) : 6.0F * s;
      const float ly = m_vertical && !dwl ? 6.0F * s : std::round((h - nodes.label->height()) / 2) - (dwl ? 0.0F : 1.0F * s);
      nodes.label->setPosition(lx, ly);
    }

    // dwl: a block per tag, with a small square when occupied.
    nodes.block->setVisible(dwl);
    nodes.mark->setVisible(dwl && !isEmpty);
    if (dwl) {
      nodes.block->setSize(w, h);
      nodes.block->setPosition(0.0F, 0.0F);
      nodes.block->setRadius(0.0F);
      nodes.block->setFill(slot.urgent ? urgent : slot.active ? active : nodes.hovered ? kusanagi::color("text/0.1") : clear);
      nodes.mark->setSize(4.0F * s, 4.0F * s);
      nodes.mark->setPosition(3.0F * s, 3.0F * s);
      nodes.mark->setRadius(0.0F);
      nodes.mark->setFill(slot.active ? onActive : kusanagi::color("text/0.8"));
    }

    // Text styles underline the active one.
    nodes.underline->setVisible(text && !dwl && slot.active);
    if (text && !dwl && slot.active) {
      const float uw = std::max(8.0F * s, nodes.label->width());
      nodes.underline->setSize(uw, 2.0F * s);
      nodes.underline->setRadius(1.0F * s);
      nodes.underline->setFill(active);
      nodes.underline->setPosition(nodes.label->x() + (nodes.label->width() - uw) / 2, nodes.label->y() + nodes.label->height() + 1.0F * s);
    }
  }
  along += m_padEnd * s + capE;
  const float boxAlong = along - m_gapStart * s;
  if (m_vertical) m_box.apply(m_spec, m_hovered, true, boxC0, m_gapStart * s, boxCross, boxAlong, s);
  else m_box.apply(m_spec, m_hovered, false, m_gapStart * s, boxC0, boxAlong, boxCross, s);
  m_hoverArea->setPosition(m_vertical ? boxC0 : m_gapStart * s, m_vertical ? m_gapStart * s : boxC0);
  m_hoverArea->setSize(m_vertical ? boxCross : boxAlong, m_vertical ? boxAlong : boxCross);
  along += m_gapEnd * s;
  m_slotsNode->setSize(m_vertical ? containerWidth : along, m_vertical ? along : containerHeight);
  rootNode->setSize(m_vertical ? containerWidth : along, m_vertical ? along : containerHeight);
}
