#include "pipewire/pipewire_service.h"
#include "render/core/renderer.h"
#include "shell/control_center/control_center_services.h"
#include "shell/kusanagi/panel/control_panel.h"
#include "shell/kusanagi/panel/cp_controls.h"
#include "shell/kusanagi/panel/cp_pages.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace kusanagi {

  namespace {
    std::string lower(std::string s) {
      std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      return s;
    }

    // A rough device icon from its name: headphones, headset, HDMI/DisplayPort or speakers.
    char32_t deviceIcon(const AudioNode& n, bool input) {
      const std::string s = lower(audioDeviceLabel(n) + " " + n.name);
      const auto has = [&s](const char* w) { return s.find(w) != std::string::npos; };
      if (input) return has("headset") || has("usb") ? 0xf02cb : 0xf036c;
      if (has("headphone") || has("headset") || has("bluez")) return 0xf02cb;
      if (has("hdmi") || has("displayport")) return 0xf0379;
      return 0xf04c3;
    }
  } // namespace

  CpSound::CpSound(ControlPanel& panel, const ControlCenterServices& services) : CpPage(panel, services) {
    buildList(m_outputs, "OUTPUT", false);

    m_apps = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    m_appsTitle = static_cast<Label*>(m_apps->addChild(cp::caption("APPS")));
    m_appsEmpty = static_cast<Label*>(m_apps->addChild(cp::text("Nothing is playing", 12.0F, false, cp::dim())));

    buildList(m_inputs, "INPUT", true);

    auto settings = std::make_unique<cp::Chip>("Sound settings", 0xf0493);
    settings->setOnActivate([this]() { m_panel.ipcClosed("settings-open"); });
    m_chips.push_back(static_cast<cp::Chip*>(addChild(std::move(settings))));
    auto mixer = std::make_unique<cp::Chip>("Mixer", 0xf066a);
    mixer->setOnActivate([this]() { m_panel.runClosed({"pavucontrol"}); });
    m_chips.push_back(static_cast<cp::Chip*>(addChild(std::move(mixer))));
  }

  void CpSound::buildList(DeviceList& list, const char* title, bool input) {
    list.input = input;
    list.card = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    list.title = static_cast<Label*>(list.card->addChild(cp::caption(title)));
    list.empty = static_cast<Label*>(list.card->addChild(cp::text("No devices", 12.0F, false, cp::dim())));
    auto slider = std::make_unique<cp::Slider>(false);
    slider->setLabel(input ? "Input volume" : "Output volume");
    slider->setOnMoved([this, input](float v) {
      if (m_services.audio == nullptr) return;
      input ? m_services.audio->setMicVolume(v) : m_services.audio->setVolume(v);
    });
    slider->setOnIconClicked([this, input]() {
      if (m_services.audio == nullptr) return;
      const AudioNode* n = input ? m_services.audio->defaultSource() : m_services.audio->defaultSink();
      if (n == nullptr) return;
      input ? m_services.audio->setMicMuted(!n->muted) : m_services.audio->setMuted(!n->muted);
    });
    list.volume = static_cast<cp::Slider*>(list.card->addChild(std::move(slider)));
  }

  bool CpSound::syncList(DeviceList& list) {
    PipeWireService* pw = m_services.audio;
    bool changed = false;
    std::vector<const AudioNode*> nodes;
    if (pw != nullptr) {
      for (const auto& n : list.input ? pw->state().sources : pw->state().sinks) nodes.push_back(&n);
    }
    std::vector<std::uint32_t> ids;
    for (const auto* n : nodes) ids.push_back(n->id);
    if (ids != list.ids) {
      // The devices changed: rebuild the rows.
      for (cp::ListRow* r : list.rows) (void)list.card->removeChild(r);
      list.rows.clear();
      for (const std::uint32_t id : ids) {
        auto row = std::make_unique<cp::ListRow>(cp::ListRow::Look{});
        const bool input = list.input;
        row->setOnActivate([this, id, input]() {
          if (m_services.audio == nullptr) return;
          input ? m_services.audio->setDefaultSource(id) : m_services.audio->setDefaultSink(id);
        });
        list.rows.push_back(static_cast<cp::ListRow*>(list.card->addChild(std::move(row))));
      }
      list.ids = ids;
      changed = true;
    }
    const AudioNode* current = pw == nullptr ? nullptr : list.input ? pw->defaultSource() : pw->defaultSink();
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      const bool isDefault = current != nullptr && nodes[i]->id == current->id;
      changed = list.rows[i]->set(deviceIcon(*nodes[i], list.input), audioDeviceLabel(*nodes[i]), 0xf012c, isDefault) || changed;
    }
    list.empty->setVisible(nodes.empty());
    const bool hasCurrent = current != nullptr;
    if (hasCurrent != list.volume->visible()) changed = true;
    list.volume->setVisible(hasCurrent);
    if (hasCurrent) {
      list.volume->setIcon(current->muted ? (list.input ? 0xf036d : 0xf0581) : (list.input ? 0xf036c : 0xf057e));
      list.volume->setValue(current->volume);
      list.volume->setMuted(current->muted);
    }
    return changed;
  }

  bool CpSound::sync(Renderer& /*renderer*/) {
    bool changed = syncList(m_outputs);
    changed = syncList(m_inputs) || changed;

    // A slider per app that's playing.
    PipeWireService* pw = m_services.audio;
    std::vector<const AudioNode*> apps;
    if (pw != nullptr) {
      for (const auto& n : pw->state().programOutputs) apps.push_back(&n);
    }
    std::vector<std::uint32_t> ids;
    for (const auto* n : apps) ids.push_back(n->id);
    if (ids != m_appIds) {
      for (cp::Slider* s : m_appSliders) (void)m_apps->removeChild(s);
      m_appSliders.clear();
      for (const std::uint32_t id : ids) {
        auto slider = std::make_unique<cp::Slider>(false);
        slider->setOnMoved([this, id](float v) {
          if (m_services.audio != nullptr) m_services.audio->setProgramOutputVolume(id, v);
        });
        slider->setOnIconClicked([this, id]() {
          if (m_services.audio == nullptr) return;
          for (const auto& n : m_services.audio->state().programOutputs) {
            if (n.id == id) m_services.audio->setProgramOutputMuted(id, !n.muted);
          }
        });
        m_appSliders.push_back(static_cast<cp::Slider*>(m_apps->addChild(std::move(slider))));
      }
      m_appIds = ids;
      changed = true;
    }
    for (std::size_t i = 0; i < apps.size(); ++i) {
      const AudioNode& n = *apps[i];
      m_appSliders[i]->setIcon(n.muted ? 0xf0581 : 0xf075a);
      changed = m_appSliders[i]->setLabel(!n.applicationName.empty() ? n.applicationName : audioDeviceLabel(n)) || changed;
      m_appSliders[i]->setValue(n.volume);
      m_appSliders[i]->setMuted(n.muted);
    }
    m_appsEmpty->setVisible(apps.empty());
    return changed;
  }

  float CpSound::layoutList(Renderer& renderer, DeviceList& list, float width) {
    const float inner = width - 24.0F;
    float y = 12.0F;
    list.title->measure(renderer);
    list.title->setPosition(12.0F, y);
    y += list.title->height() + 4.0F;
    for (cp::ListRow* row : list.rows) {
      y += 4.0F;
      row->setPosition(12.0F, y);
      row->setSize(inner, 34.0F);
      row->layout(renderer);
      y += 34.0F;
    }
    if (list.empty->visible()) {
      list.empty->measure(renderer);
      y += 4.0F;
      list.empty->setPosition(12.0F, y);
      y += list.empty->height();
    }
    if (list.volume->visible()) {
      y += 4.0F + 4.0F + 4.0F;  // a 4 px spacer and the gaps around it
      list.volume->setPosition(12.0F, y);
      list.volume->setSize(inner, 36.0F);
      list.volume->layout(renderer);
      y += 36.0F;
    }
    const float h = y + 12.0F;
    list.card->setSize(width, h);
    return h;
  }

  float CpSound::layout(Renderer& renderer, float width) {
    float y = 0.0F;
    m_outputs.card->setPosition(0.0F, y);
    y += layoutList(renderer, m_outputs, width) + 12.0F;

    // Apps
    {
      const float inner = width - 24.0F;
      float ay = 12.0F;
      m_appsTitle->measure(renderer);
      m_appsTitle->setPosition(12.0F, ay);
      ay += m_appsTitle->height();
      for (cp::Slider* s : m_appSliders) {
        ay += 8.0F;
        s->setPosition(12.0F, ay);
        s->setSize(inner, 36.0F);
        s->layout(renderer);
        ay += 36.0F;
      }
      if (m_appsEmpty->visible()) {
        m_appsEmpty->measure(renderer);
        ay += 8.0F;
        m_appsEmpty->setPosition(12.0F, ay);
        ay += m_appsEmpty->height();
      }
      m_apps->setPosition(0.0F, y);
      m_apps->setSize(width, ay + 12.0F);
      y += ay + 12.0F + 12.0F;
    }

    m_inputs.card->setPosition(0.0F, y);
    y += layoutList(renderer, m_inputs, width) + 12.0F;

    float x = 0.0F;
    float rowH = 0.0F;
    for (cp::Chip* c : m_chips) {
      c->layout(renderer);
      c->setPosition(x, y);
      x += c->width() + 8.0F;
      rowH = std::max(rowH, c->height());
    }
    return y + rowH;
  }

} // namespace kusanagi
