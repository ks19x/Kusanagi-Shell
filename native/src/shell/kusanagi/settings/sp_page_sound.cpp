// Settings > Sound: default output and input, per-device and per-app volume. Uses the same PipeWire
// service as the control panel's Sound tab.

#include "cursor-shape-v1-client-protocol.h"
#include "pipewire/pipewire_service.h"
#include "render/core/renderer.h"
#include "shell/control_center/control_center_services.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <cmath>
#include <memory>
#include <string>

namespace kusanagi::sp {

  namespace {

    enum class Kind { Output, Input, App };

    PipeWireService* audio() { return services().audio; }

    // Nickname, else description, else name.
    std::string label(const AudioNode& n) { return !n.nickname.empty() ? n.nickname : audioDeviceLabel(n); }

    const std::vector<AudioNode>* listOf(Kind k) {
      PipeWireService* pw = audio();
      if (pw == nullptr) return nullptr;
      const AudioState& st = pw->state();
      return k == Kind::Output ? &st.sinks : k == Kind::Input ? &st.sources : &st.programOutputs;
    }

    const AudioNode* nodeOf(Kind k, std::uint32_t id) {
      if (const auto* list = listOf(k)) {
        for (const auto& n : *list) {
          if (n.id == id) return &n;
        }
      }
      return nullptr;
    }

    std::vector<std::string> keysOf(Kind k) {
      std::vector<std::string> out;
      if (const auto* list = listOf(k)) {
        for (const auto& n : *list) out.push_back(std::to_string(n.id));
      }
      return out;
    }

    void setVolume(Kind k, std::uint32_t id, float v) {
      PipeWireService* pw = audio();
      if (pw == nullptr) return;
      if (k == Kind::Output) pw->setSinkVolume(id, v);
      else if (k == Kind::Input) pw->setSourceVolume(id, v);
      else pw->setProgramOutputVolume(id, v);
    }

    void toggleMute(Kind k, std::uint32_t id) {
      PipeWireService* pw = audio();
      const AudioNode* n = nodeOf(k, id);
      if (pw == nullptr || n == nullptr) return;
      if (k == Kind::Output) pw->setSinkMuted(id, !n->muted);
      else if (k == Kind::Input) pw->setSourceMuted(id, !n->muted);
      else pw->setProgramOutputMuted(id, !n->muted);
    }

    // Volume slider for one node. Clicking the icon toggles mute.
    std::unique_ptr<Slider> volumeSlider(Kind k, std::uint32_t id, float height) {
      return std::make_unique<Slider>(
          Binding{
              .get = [k, id]() -> json {
                const AudioNode* n = nodeOf(k, id);
                return n != nullptr ? n->volume : 0.0F;
              },
              .set = [k, id](const json& v) { setVolume(k, id, v.get<float>()); },
          },
          SliderOpts{
              .toUnit = [](const json& v) { return v.is_number() ? v.get<float>() : 0.0F; },
              .fromUnit = [](float u) -> json { return u; },
              .text = [](const json& v) {
                return std::to_string(static_cast<int>(std::lround((v.is_number() ? v.get<double>() : 0.0) * 100.0))) + "%";
              },
              .height = height,
              .iconFn = [k, id]() -> char32_t {
                const AudioNode* n = nodeOf(k, id);
                const bool muted = n != nullptr && n->muted;
                return muted ? 0xf0581 : k == Kind::App ? 0xf075a : 0xf057e;
              },
              .labelFn = k == Kind::App ? std::function<std::string()>([id]() -> std::string {
                const AudioNode* n = nodeOf(Kind::App, id);
                if (n == nullptr) return "";
                return !n->applicationName.empty() ? n->applicationName : label(*n);
              })
                                        : std::function<std::string()>{},
              .muted = [k, id]() {
                const AudioNode* n = nodeOf(k, id);
                return n != nullptr && n->muted;
              },
              .onIconClicked = [k, id]() {
                toggleMute(k, id);
                refresh();
              },
          }
      );
    }

    // Radio button and name (click to make it the default), then its volume slider.
    class Device : public Item {
    public:
      Device(Kind kind, std::uint32_t id) : m_kind(kind), m_id(id) {
        m_row = static_cast<InputArea*>(addChild(ui::inputArea({.cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER})));
        m_radio = static_cast<Box*>(m_row->addChild(ui::box({})));
        m_dot = static_cast<Box*>(m_row->addChild(ui::box({})));
        m_name = static_cast<Label*>(m_row->addChild(makeText("", 12.0F)));
        m_name->setMaxLines(1);
        m_row->setOnClick([this](const InputArea::PointerData&) {
          PipeWireService* pw = audio();
          if (pw == nullptr) return;
          m_kind == Kind::Output ? pw->setDefaultSink(m_id) : pw->setDefaultSource(m_id);
          refresh();
        });
        m_slider = static_cast<Slider*>(addChild(volumeSlider(kind, id, 30.0F)));
        sync();
      }

      void sync() override {
        const AudioNode* n = nodeOf(m_kind, m_id);
        PipeWireService* pw = audio();
        const std::uint32_t def = pw == nullptr ? 0 : m_kind == Kind::Output ? pw->state().defaultSinkId : pw->state().defaultSourceId;
        const bool isDefault = n != nullptr && n->id == def;
        bool changed = m_name->setText(n != nullptr ? label(*n) : "");
        if (isDefault != m_default || m_first) {
          m_default = isDefault;
          m_first = false;
          m_name->setFontWeight(isDefault ? FontWeight::Bold : FontWeight::Normal);
          m_radio->setBorder(isDefault ? accent() : textA(0.3F), 2.0F);
          m_dot->setVisible(isDefault);
          changed = true;
        }
        if (changed) requestLayout();
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        m_row->setPosition(0.0F, 0.0F);
        m_row->setSize(width, 24.0F);
        m_radio->setSize(16.0F, 16.0F);
        m_radio->setRadius(8.0F);
        m_radio->setFill(textA(0.0F));
        m_radio->setPosition(0.0F, 4.0F);
        m_dot->setSize(8.0F, 8.0F);
        m_dot->setRadius(4.0F);
        m_dot->setFill(accent());
        m_dot->setPosition(4.0F, 8.0F);
        m_name->setMaxWidth(std::max(1.0F, width - 26.0F));
        m_name->measure(renderer);
        m_name->setPosition(26.0F, std::round((24.0F - m_name->height()) / 2.0F));
        const float sh = m_slider->place(renderer, width);
        m_slider->setPosition(0.0F, 24.0F + 6.0F);
        const float h = 24.0F + 6.0F + sh;
        setSize(width, h);
        return h;
      }

    private:
      Kind m_kind;
      std::uint32_t m_id;
      InputArea* m_row = nullptr;
      Box* m_radio = nullptr;
      Box* m_dot = nullptr;
      Label* m_name = nullptr;
      Slider* m_slider = nullptr;
      bool m_default = false;
      bool m_first = true;
    };

    void deviceGroup(Column& page, const char* title, Kind kind, const char* none) {
      auto* g = page.add<Group>(title);
      g->add<Repeater>([kind]() { return keysOf(kind); },
                       [kind](const std::string& key) -> std::unique_ptr<Item> {
                         return std::make_unique<Device>(kind, static_cast<std::uint32_t>(std::stoul(key)));
                       });
      g->add<Text>(none, TextOpts{.color = dim()})->showIf([kind]() { return keysOf(kind).empty(); });
    }

  } // namespace

  void buildSound(Column& page) {
    // PipeWire state changes from elsewhere too (other apps, plugging devices, media keys), so re-sync.
    auto last = std::make_shared<AudioState>();
    page.add<Poll>(150, [last]() {
      PipeWireService* pw = audio();
      if (pw == nullptr || pw->state() == *last) return;
      *last = pw->state();
      refresh();
    });

    deviceGroup(page, "Output", Kind::Output, "No output devices");
    deviceGroup(page, "Input", Kind::Input, "No input devices");

    {
      auto* g = page.add<Group>("Apps", "Everything that's playing sound right now.");
      g->add<Repeater>([]() { return keysOf(Kind::App); },
                       [](const std::string& key) -> std::unique_ptr<Item> {
                         return volumeSlider(Kind::App, static_cast<std::uint32_t>(std::stoul(key)), 36.0F);
                       });
      g->add<Text>("Nothing is playing", TextOpts{.color = dim()})->showIf([]() { return keysOf(Kind::App).empty(); });
    }

    page.add<Chip>("Open the advanced mixer", 0xf066a)->onClick([]() { spawn({"pavucontrol"}); });
  }

} // namespace kusanagi::sp
