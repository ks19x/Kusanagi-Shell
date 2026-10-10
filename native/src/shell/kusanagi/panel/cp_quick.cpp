#include "cursor-shape-v1-client-protocol.h"
#include "core/process/process.h"
#include "util/file_utils.h"
#include "render/core/renderer.h"
#include "shell/control_center/control_center_services.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/panel/control_panel.h"
#include "shell/kusanagi/panel/cp_controls.h"
#include "shell/kusanagi/panel/cp_pages.h"
#include "shell/panel/panel_manager.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace kusanagi {

  namespace {
    // The built-in presets, then the saved looks from presets.json.
    struct Preset {
      std::string id;
      std::string name;
    };
    std::vector<Preset> presets() {
      std::vector<Preset> out{
          {"kusanagi", "Kusanagi"}, {"minimal", "Minimal"},   {"glass", "Floating glass"}, {"zen", "Zen"},
          {"terminal", "Terminal"}, {"neon", "Neon"},         {"paper", "Paper"},          {"hud", "Gamer HUD"},
          {"nordic", "Nordic"},     {"ink", "Ink"},           {"aurora", "Aurora"},        {"material", "Material You"},
          {"candy", "Candy"},       {"notch", "Notch"},       {"cyber", "Cyber"},          {"win11", "Windows 11"},
          {"zen2", "Zen pill"},     {"powerline", "Powerline"}, {"sidebar", "Sidebar"},    {"dock", "Dock"},
      };
      std::ifstream f(std::filesystem::path(FileUtils::configDir()) / "presets.json");
      if (f) {
        const auto j = nlohmann::json::parse(f, nullptr, false);
        if (j.is_object() && j.contains("saved") && j["saved"].is_array()) {
          for (const auto& p : j["saved"]) {
            if (p.is_object() && p.contains("name") && p["name"].is_string()) {
              out.push_back({p.value("id", "user:" + p["name"].get<std::string>()), p["name"].get<std::string>()});
            }
          }
        }
      }
      return out;
    }

    const std::vector<std::string> kSwatches{"#f5a3b5", "#f5b38a", "#e9d27c", "#8fd6b0",
                                             "#8cc8f0", "#b9a6f2", "#e8505b", "#e8e8e8"};
    const std::vector<std::pair<std::string, std::string>> kBarStyles{
        {"islands", "Islands"}, {"solid", "Solid"}, {"floating", "Floating"}, {"clear", "Clear"}};
    const std::vector<std::pair<std::string, std::string>> kWorkspaces{
        {"pills", "Pills"}, {"dots", "Dots"},   {"numbers", "1 2 3"}, {"roman", "I II III"},
        {"kanji", "一 二 三"}, {"dwl", "dwl"}, {"custom", "Custom"}};
    const std::vector<std::pair<double, std::string>> kMotion{{0.0, "Off"}, {0.7, "Snappy"}, {1.0, "Smooth"}, {1.4, "Relaxed"}};

    std::string lower(std::string s) {
      std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    int motionIndex() {
      const double speed = opt<double>("look", "animSpeed", 1.0);
      int best = 0;
      for (std::size_t i = 1; i < kMotion.size(); ++i) {
        if (std::fabs(kMotion[i].first - speed) < std::fabs(kMotion[best].first - speed)) best = static_cast<int>(i);
      }
      return best;
    }

    int barIndex() {
      const std::string style = opt<std::string>("bar", "style", "islands");
      for (std::size_t i = 0; i < kBarStyles.size(); ++i) {
        if (kBarStyles[i].first == style) return static_cast<int>(i);
      }
      return 0;
    }

    std::unique_ptr<Label> section(const std::string& s) {
      auto l = cp::text("", 10.0F, true, cp::dim());
      cp::setSpacedText(*l, s, 2.0F);
      return l;
    }
  } // namespace

  CpQuick::CpQuick(ControlPanel& panel, const ControlCenterServices& services) : CpPage(panel, services) {
    m_presetTitle = static_cast<Label*>(addChild(section("PRESET")));
    for (const auto& p : presets()) {
      auto chip = std::make_unique<cp::Chip>(p.name);
      chip->setOnActivate([id = p.id]() { (void)process::runAsync(std::vector<std::string>{"kusanagi", "preset", id}); });
      m_presets.push_back(static_cast<cp::Chip*>(addChild(std::move(chip))));
    }

    m_accentTitle = static_cast<Label*>(addChild(section("ACCENT")));
    auto wall = std::make_unique<cp::Chip>("Wallpaper", 0xf0e09, opt<std::string>("look", "accent", "").empty());
    wall->setOnActivate([]() { (void)setOption("look", "accent", ""); });
    m_wallpaperAccent = static_cast<cp::Chip*>(addChild(std::move(wall)));
    for (const auto& c : kSwatches) {
      Swatch s;
      s.color = c;
      s.area = static_cast<InputArea*>(addChild(ui::inputArea({
          .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
          .onClick = [c](const InputArea::PointerData&) { (void)setOption("look", "accent", c); },
      })));
      s.area->setSize(28.0F, 28.0F);
      s.dot = static_cast<Box*>(s.area->addChild(ui::box({})));
      s.dot->setSize(28.0F, 28.0F);
      s.dot->setRadius(14.0F);
      s.dot->setFill(kusanagi::color(c));
      InputArea* area = s.area;
      area->setOnEnter([area](const InputArea::PointerData&) { cp::tweenScale(*area, 1.12F, 160); });
      area->setOnLeave([area]() { cp::tweenScale(*area, 1.0F, 160); });
      m_swatches.push_back(s);
    }

    m_barTitle = static_cast<Label*>(addChild(section("BAR")));
    auto bar = std::make_unique<cp::Segmented>(11.0F);
    std::vector<cp::Segment> barOptions;
    for (std::size_t i = 0; i < kBarStyles.size(); ++i) barOptions.push_back({kBarStyles[i].second, static_cast<int>(i)});
    bar->setOptions(std::move(barOptions), barIndex());
    bar->setOnPicked([this](int i) {
      m_barStyle->setCurrent(i, true);
      (void)setOption("bar", "style", kBarStyles[static_cast<std::size_t>(i)].first);
    });
    m_barStyle = static_cast<cp::Segmented*>(addChild(std::move(bar)));

    m_wsTitle = static_cast<Label*>(addChild(section("WORKSPACES")));
    for (const auto& [value, label] : kWorkspaces) {
      auto chip = std::make_unique<cp::Chip>(label);
      chip->setOnActivate([v = value]() { (void)setOption("workspaces", "style", v); });
      m_wsChips.push_back(static_cast<cp::Chip*>(addChild(std::move(chip))));
    }

    m_motionTitle = static_cast<Label*>(addChild(section("MOTION")));
    auto motion = std::make_unique<cp::Segmented>(11.0F);
    std::vector<cp::Segment> motionOptions;
    for (std::size_t i = 0; i < kMotion.size(); ++i) motionOptions.push_back({kMotion[i].second, static_cast<int>(i)});
    motion->setOptions(std::move(motionOptions), motionIndex());
    motion->setOnPicked([this](int i) {
      m_motion->setCurrent(i, true);
      (void)setOption("look", "animSpeed", kMotion[static_cast<std::size_t>(i)].first);
    });
    m_motion = static_cast<cp::Segmented*>(addChild(std::move(motion)));

    // Opens the full settings app.
    m_all = static_cast<InputArea*>(addChild(ui::inputArea({
        .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
        .onClick = [this](const InputArea::PointerData&) { m_panel.ipcClosed("settings-open"); },
    })));
    m_allBg = static_cast<Box*>(m_all->addChild(ui::box({})));
    m_allBg->setFill(cp::accent(0.18F));
    m_allIcon = static_cast<Label*>(m_all->addChild(cp::icon(0xf0493, 16.0F, cp::accent())));
    m_allText = static_cast<Label*>(m_all->addChild(cp::text("All settings", 13.0F, true)));
    m_all->setOnEnter([this](const InputArea::PointerData&) {
      cp::tweenColor(*m_allBg, cp::accent(0.18F), cp::accent(), 160, [this](const ColorSpec& c) { m_allBg->setFill(c); });
      m_allIcon->setColor(cp::bgPanel());
      m_allText->setColor(cp::bgPanel());
    });
    m_all->setOnLeave([this]() {
      cp::tweenColor(*m_allBg, cp::accent(), cp::accent(0.18F), 160, [this](const ColorSpec& c) { m_allBg->setFill(c); });
      m_allIcon->setColor(cp::accent());
      m_allText->setColor(cp::textA(1.0F));
    });
  }

  bool CpQuick::sync(Renderer& /*renderer*/) {
    // Follow settings.json, whether we changed it or it was edited by hand.
    const std::string accent = lower(opt<std::string>("look", "accent", ""));
    const std::string ws = opt<std::string>("workspaces", "style", "pills");
    m_wallpaperAccent->setOn(accent.empty());
    for (auto& s : m_swatches) s.dot->setBorder(cp::textA(1.0F), accent == s.color ? 3.0F : 0.0F);
    for (std::size_t i = 0; i < m_wsChips.size(); ++i) m_wsChips[i]->setOn(kWorkspaces[i].first == ws);
    m_barStyle->setCurrent(barIndex(), true);
    m_motion->setCurrent(motionIndex(), true);
    return false;
  }

  float CpQuick::flow(Renderer& renderer, std::vector<Node*> items, float y, float width, float spacing) {
    float x = 0.0F;
    float rowH = 0.0F;
    for (Node* n : items) {
      if (auto* chip = dynamic_cast<cp::Chip*>(n); chip != nullptr) chip->layout(renderer);
      if (x > 0.0F && x + n->width() > width) {
        x = 0.0F;
        y += rowH + spacing;
        rowH = 0.0F;
      }
      n->setPosition(x, y);
      x += n->width() + spacing;
      rowH = std::max(rowH, n->height());
    }
    return y + rowH;
  }

  float CpQuick::layout(Renderer& renderer, float width) {
    float y = 0.0F;
    const auto title = [&](Label* l) {
      l->measure(renderer);
      l->setPosition(0.0F, y + 4.0F);
      y += 4.0F + l->height() + 12.0F;
    };
    title(m_presetTitle);
    y = flow(renderer, std::vector<Node*>(m_presets.begin(), m_presets.end()), y, width, 6.0F) + 12.0F;

    title(m_accentTitle);
    std::vector<Node*> accent{m_wallpaperAccent};
    for (auto& s : m_swatches) accent.push_back(s.area);
    y = flow(renderer, accent, y, width, 8.0F) + 12.0F;

    title(m_barTitle);
    m_barStyle->setPosition(0.0F, y);
    m_barStyle->setSize(width, 32.0F);
    m_barStyle->layout(renderer);
    y += 32.0F + 12.0F;

    title(m_wsTitle);
    y = flow(renderer, std::vector<Node*>(m_wsChips.begin(), m_wsChips.end()), y, width, 6.0F) + 12.0F;

    title(m_motionTitle);
    m_motion->setPosition(0.0F, y);
    m_motion->setSize(width, 32.0F);
    m_motion->layout(renderer);
    y += 32.0F + 12.0F + 4.0F + 12.0F;

    const float h = 44.0F;
    m_all->setPosition(0.0F, y);
    m_all->setSize(width, h);
    m_allBg->setSize(width, h);
    m_allBg->setRadius(h / 2.0F);
    m_allIcon->measure(renderer);
    m_allText->measure(renderer);
    const float rowW = m_allIcon->width() + 10.0F + m_allText->width();
    const float rowH = std::max(m_allIcon->height(), m_allText->height());
    const float rx = std::round((width - rowW) / 2.0F);
    const float ry = std::round((h - rowH) / 2.0F);
    m_allIcon->setPosition(rx, ry);
    m_allText->setPosition(rx + m_allIcon->width() + 10.0F, ry);
    return y + h;
  }

} // namespace kusanagi
