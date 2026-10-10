#include "shell/kusanagi/settings/setup_app.h"

#include "cli/schema_msg.h"
#include "core/deferred_call.h"
#include "core/files/resource_paths.h"
#include "core/input/keybind_matcher.h"
#include "core/log.h"
#include "core/ui_phase.h"
#include "cursor-shape-v1-client-protocol.h"
#include "ipc/ipc_service.h"
#include "render/core/renderer.h"
#include "render/render_context.h"
#include "shell/kusanagi/kusanagi_ipc.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_page_bar.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "shell/kusanagi/wm_layout.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "util/file_utils.h"
#include "wayland/toplevel_surface.h"
#include "wayland/wayland_connection.h"
#include "wayland/wayland_seat.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <wayland-client-protocol.h>

namespace kusanagi {

  namespace {
    constexpr Logger kLog("kusanagi-setup");
    constexpr auto kPointer = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;

    constexpr float kWidth = 1000.0F;
    constexpr float kHeight = 700.0F;
    constexpr float kMinWidth = 820.0F;
    constexpr float kMinHeight = 560.0F;
    constexpr float kSideW = 230.0F;
    constexpr float kFooterH = 64.0F;
    // Keep the old app id so existing window rules that float it still apply.
    constexpr const char* kAppId = "org.quickshell";

    using json = nlohmann::json;

    // `page` is the id of the Settings page shown for the step, if any.
    struct Step {
      const char* id;
      const char* name;
      char32_t icon;
      const char* page;
      const char* desc;
    };
    constexpr Step kSteps[] = {
        {"welcome", "Welcome", 0xf0493, nullptr, nullptr},
        {"presets", "Look", 0xf0e09, "presets", "Pick a whole look. Everything in it can be changed later."},
        {"wallpaper", "Wallpaper", 0xf0e09, "wallpaper", "Your wallpaper folder, and how pictures change. The colours follow the wallpaper."},
        {"bar", "Bar", 0xf04e9, "bar", "Start from a template — or build your own, piece by piece."},
        {"lock", "Lock & idle", 0xf033e, "lock", "How the lock screen looks, and what happens when you step away."},
        {"extras", "Extras", 0xf0297, nullptr, nullptr},
        {"login", "Login screen", 0xf0004, "login", "Optional: Kusanagi as the screen you log in on (needs greetd)."},
        {"done", "Done", 0xf012c, nullptr, nullptr},
    };
    constexpr int kStepCount = static_cast<int>(std::size(kSteps));

    std::string trim(const std::string& s) {
      std::size_t a = 0, b = s.size();
      while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
      while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
      return s.substr(a, b - a);
    }

    std::string typeOf(const json& e) {
      if (e.is_string()) return e.get<std::string>();
      if (e.is_object() && e.contains("type") && e["type"].is_string()) return e["type"].get<std::string>();
      return "";
    }

    json currentBars() {
      const json b = sp::value("bars");
      return b.is_array() && !b.empty() ? b : sp::legacyBars();
    }

    // True if a module of this type is on any bar, inside groups too.
    bool usesModule(const std::string& type) {
      for (const auto& bar : currentBars()) {
        for (const char* s : {"start", "center", "end"}) {
          if (!bar.contains(s) || !bar[s].is_array()) continue;
          for (const auto& e : bar[s]) {
            if (typeOf(e) == type) return true;
            if (typeOf(e) == "group" && e.contains("modules") && e["modules"].is_array()) {
              for (const auto& m : e["modules"]) {
                if (typeOf(m) == type) return true;
              }
            }
          }
        }
      }
      return false;
    }

    json strip(const json& list, const std::string& type) {
      json out = json::array();
      if (!list.is_array()) return out;
      for (const auto& e : list) {
        if (typeOf(e) == type) continue;
        if (e.is_object() && typeOf(e) == "group") {
          json g = e;
          g["modules"] = strip(e.value("modules", json::array()), type);
          out.push_back(std::move(g));
        } else {
          out.push_back(e);
        }
      }
      return out;
    }

    // Adds or removes a module in the end section of every bar. The classic bar is converted to a layout first.
    void barModule(const std::string& type, bool on) {
      json bars = currentBars();
      for (auto& b : bars) {
        for (const char* s : {"start", "center", "end"}) b[s] = strip(b.value(s, json::array()), type);
        if (on) b["end"].insert(b["end"].begin(), json{{"type", type}});
      }
      sp::set("bars", bars);
    }

    class Logo : public sp::Item {
    public:
      explicit Logo(float size) : m_size(size) { m_image = static_cast<Image*>(addChild(ui::image({.fit = ImageFit::Contain}))); }
      float place(Renderer& renderer, float width) override {
        if (!m_loaded) {
          m_loaded = true;
          (void)m_image->setSourceFile(renderer, paths::assetPath("kusanagi-logo.svg").string(), static_cast<int>(m_size * 2.0F), true);
        }
        m_image->setSize(m_size, m_size);
        setSize(widthFor(width), m_size);
        return m_size;
      }

    private:
      float m_size;
      Image* m_image = nullptr;
      bool m_loaded = false;
    };

    // Steps that aren't Settings pages

    void buildWelcome(sp::Column& col, const std::shared_ptr<std::vector<std::string>>& missing) {
      col.add<sp::Gap>(12.0F); // Plus the column's 18 px spacing gives 30 px at the top.
      col.add<Logo>(84.0F);
      col.add<sp::Text>("Welcome to Kusanagi", sp::TextOpts{.px = 30.0F, .bold = true});
      col.add<sp::Text>("Your bar, panels, launcher, notifications, lock screen and more — all one program. A few steps to make it "
                        "yours; skip any of them, and change everything later in Settings (Super+I).",
                        sp::TextOpts{.px = 13.0F, .color = sp::textA(0.8F), .wrap = true});
      auto* g = col.add<sp::Group>("Your system");
      g->add<sp::Row>("Compositor", std::make_unique<sp::Text>(wm::name(), sp::TextOpts{.color = sp::accent()}));
      auto found = std::make_unique<sp::Text>("", sp::TextOpts{});
      found->bindText([missing]() { return missing->empty() ? std::string("none — all set") : std::to_string(missing->size()) + " found"; });
      found->bindColor([missing]() { return missing->empty() ? sp::ok() : sp::danger(); });
      g->add<sp::Row>("Missing pieces", std::string{}, std::move(found))->bindHint([missing]() {
        return missing->empty() ? std::string() : std::string("run  kusanagi doctor  in a terminal for the exact install command");
      });
      auto* list = g->add<sp::Column>(3.0F);
      list->showIf([missing]() { return !missing->empty(); });
      list->add<sp::Repeater>(
          [missing]() {
            std::vector<std::string> keys;
            for (std::size_t i = 0; i < missing->size() && i < 12; ++i) keys.push_back((*missing)[i]);
            return keys;
          },
          [](const std::string& line) -> std::unique_ptr<sp::Item> {
            return std::make_unique<sp::Text>("·  " + line, sp::TextOpts{.px = 11.0F, .color = sp::dim(), .elide = true});
          },
          3.0F
      );
    }

    void buildExtras(sp::Column& col) {
      {
        auto* g = col.add<sp::Group>("Take over from other tools");
        g->add<sp::Row>("Lock and blank the screen when idle", "instead of hypridle / swayidle",
                        std::make_unique<sp::Switch>(sp::bind("idle.enabled")));
        g->add<sp::Row>("Ask for admin passwords", "a polkit agent for GParted, mounting disks, pkexec…",
                        std::make_unique<sp::Switch>(sp::bind("polkit.enabled")));
      }
      {
        auto* g = col.add<sp::Group>("On the bar", "Adds them to the end of your bar (the custom layout; the classic bar becomes one).");
        const auto module = [](const char* type) {
          return std::make_unique<sp::Switch>(sp::Binding{
              .get = [type]() -> json { return usesModule(type); },
              .set = [type](const json& v) { barModule(type, sp::truthy(v)); },
          });
        };
        g->add<sp::Row>("Recording indicator", "shows while recording or replaying — click to save a clip", module("recorder"));
        g->add<sp::Row>("Package updates", "how many are waiting — click to install", module("updates"));
      }
      {
        auto* g = col.add<sp::Group>("Launcher");
        g->add<sp::Row>("Find Kusanagi commands", "type \"lock\", \"bar\", \"replay\"…", std::make_unique<sp::Switch>(sp::bind("launcher.commands")));
        g->add<sp::Row>("Web search as the last result", std::make_unique<sp::Switch>(sp::bind("launcher.webSearch")));
      }
    }

    void buildDone(sp::Column& col) {
      col.add<sp::Gap>(12.0F);
      col.add<sp::Text>("You're set", sp::TextOpts{.px = 30.0F, .bold = true});
      col.add<sp::Text>("A few keys to remember (Kusanagi's defaults — your compositor config may differ):",
                        sp::TextOpts{.px = 13.0F, .color = sp::textA(0.8F), .wrap = true});
      auto* g = col.add<sp::Group>("");
      static const std::pair<const char*, const char*> keys[] = {
          {"Super + Space", "launcher — apps, = calc, : emoji, / files, ? web"},
          {"Super + I", "Settings"},
          {"Super + L", "lock"},
          {"Super + V", "clipboard history"},
          {"Super + A", "wallpapers"},
          {"Super + N", "notifications"},
          {"Super + G", "game mode"},
          {"Super + B", "control panel"},
      };
      for (const auto& [key, what] : keys) {
        g->add<sp::Row>(what, std::make_unique<sp::Text>(key, sp::TextOpts{.bold = true, .color = sp::accent()}));
      }
      col.add<sp::Text>("Run this again any time: kusanagi setup — or search \"setup\" in the launcher.",
                        sp::TextOpts{.px = 11.0F, .color = sp::dim(), .wrap = true});
    }
  } // namespace

  struct SetupApp::StepItem : public Node {
    StepItem(SetupApp& owner, int idx) : app(owner), index(idx) {
      area = static_cast<InputArea*>(addChild(ui::inputArea({.cursorShape = kPointer})));
      bg = static_cast<Box*>(area->addChild(ui::box({})));
      circle = static_cast<Box*>(area->addChild(ui::box({})));
      number = static_cast<Label*>(area->addChild(sp::makeText("", 10.0F, true)));
      name = static_cast<Label*>(area->addChild(sp::makeText(kSteps[idx].name, 12.0F)));
      area->setOnClick([this](const InputArea::PointerData&) {
        DeferredCall::callLater([a = &app, i = index]() { a->goTo(i); });
      });
    }

    void restyle(int step) {
      const bool active = index == step;
      const bool done = index < step;
      bg->setFill(active ? sp::accent(0.13F) : sp::textA(0.0F));
      circle->setFill(active ? sp::accent() : done ? sp::accent(0.3F) : sp::textA(0.08F));
      number->setText(done ? "✓" : std::to_string(index + 1));
      number->setColor(active ? sp::bgPanel() : sp::textA(1.0F));
      name->setFontWeight(active ? FontWeight::Bold : FontWeight::Normal);
      name->setColor(active ? sp::textA(1.0F) : sp::textA(0.7F));
    }

    void place(Renderer& renderer) {
      const float w = 186.0F, h = 36.0F;
      area->setSize(w, h);
      bg->setSize(w, h);
      bg->setRadius(10.0F);
      circle->setSize(22.0F, 22.0F);
      circle->setRadius(11.0F);
      circle->setPosition(10.0F, 7.0F);
      number->measure(renderer);
      number->setPosition(10.0F + std::round((22.0F - number->width()) / 2.0F), 7.0F + std::round((22.0F - number->height()) / 2.0F));
      name->measure(renderer);
      name->setPosition(10.0F + 22.0F + 12.0F, std::round((h - name->height()) / 2.0F));
      setSize(w, h);
    }

    SetupApp& app;
    int index;
    InputArea* area = nullptr;
    Box* bg = nullptr;
    Box* circle = nullptr;
    Label* number = nullptr;
    Label* name = nullptr;
  };

  SetupApp& SetupApp::instance() {
    static SetupApp app;
    return app;
  }

  void SetupApp::initialize(WaylandConnection& wayland, RenderContext* renderContext, IpcService* ipc) {
    m_wayland = &wayland;
    m_renderContext = renderContext;
    m_ipc = ipc;
  }

  void SetupApp::registerIpc(IpcService& ipc) {
    ipc.bind(kusanagi::cli::msg::kusanagiSetup, [this](const std::string& args) -> std::string {
      std::string a = trim(args);
      if (a.empty() || a == "open" || a == "show") {
        open();
      } else if (a == "toggle") {
        isOpen() ? close() : open();
      } else if (a == "hide") {
        close();
      } else {
        return "error: kusanagi-setup takes open | toggle | hide\n";
      }
      return isOpen() ? std::string("open ") + kSteps[m_step].id + "\n" : "closed\n";
    });
  }

  void SetupApp::checkFirstRun() {
    const auto path = std::filesystem::path(FileUtils::configDir()) / "settings.json";
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) return;
    // Write the defaults out so there is a file to hand-edit.
    std::filesystem::create_directories(path.parent_path(), ec);
    {
      std::ofstream out(path.string() + ".tmp");
      out << sp::defaults().dump(4) << "\n";
    }
    std::filesystem::rename(path.string() + ".tmp", path, ec);
    kLog.info("first run: wrote {}, opening the setup", path.string());
    m_firstRun.start(std::chrono::milliseconds(2500), [this]() { open(); });
  }

  void SetupApp::open() {
    if (isOpen()) {
      // Already open: go back to the first step and raise it.
      goTo(0);
      m_wayland->activateSurface(m_surface->wlSurface());
      return;
    }
    if (m_wayland == nullptr || m_renderContext == nullptr || !m_wayland->hasXdgShell()) return;
    wl_output* output = m_wayland->lastPointerOutput();
    if (output == nullptr && !m_wayland->outputs().empty()) output = m_wayland->outputs().front().output;

    sp::addHost(this, sp::Host{
                          .changed = [this]() { onSettingsChanged(); },
                          .requestLayout = [this]() { requestLayout(); },
                          .ipc = [this](const std::string& line) { return m_ipc != nullptr ? m_ipc->execute(line) : std::string("error\n"); },
                      });

    m_surface = std::make_unique<ToplevelSurface>(*m_wayland);
    m_surface->setRenderContext(m_renderContext);
    m_surface->setAnimationManager(&m_animations);
    m_surface->setClosedCallback([this]() { DeferredCall::callLater([this]() { close(); }); });
    m_surface->setOutputChangedCallback([this](wl_output*) { requestLayout(); });
    m_surface->setConfigureCallback([this](std::uint32_t, std::uint32_t) { requestLayout(); });
    m_surface->setPrepareFrameCallback([this](bool, bool) { prepareFrame(); });
    m_surface->setUpdateCallback([]() {});
    ToplevelSurfaceConfig cfg{
        .width = static_cast<std::uint32_t>(kWidth),
        .height = static_cast<std::uint32_t>(kHeight),
        .minWidth = static_cast<std::uint32_t>(kMinWidth),
        .minHeight = static_cast<std::uint32_t>(kMinHeight),
        .title = "Kusanagi Setup",
        .appId = kAppId,
    };
    if (!m_surface->initialize(output, cfg)) {
      kLog.warn("couldn't create the setup window");
      m_surface.reset();
      sp::removeHost(this);
      return;
    }
    m_step = 0;
    m_sceneDirty = true;
    m_contentNeedsBuild = true;
    m_pointerInside = false;
    readMissing();
  }

  void SetupApp::close() {
    if (!isOpen()) return;
    sp::flush();
    destroyWindow();
  }

  void SetupApp::destroyWindow() {
    if (m_surface != nullptr) {
      m_dispatcher.pointerLeave();
      m_dispatcher.setSceneRoot(nullptr);
      m_surface->setSceneRoot(nullptr);
    }
    m_animations.cancelAll();
    m_root.reset();
    m_surface.reset();
    m_steps.clear();
    m_bg = m_side = m_divider = m_footer = m_footerLine = nullptr;
    m_logo = nullptr;
    m_brand = m_brandSub = m_headName = m_headDesc = nullptr;
    m_main = m_holder = nullptr;
    m_scroller = nullptr;
    m_content = nullptr;
    m_skip = m_back = m_next = m_finish = nullptr;
    m_pointerInside = false;
    m_scroll = m_scrollTarget = 0.0F;
    sp::removeHost(this);
  }

  // Collects what `kusanagi doctor` reports as missing, for the welcome step.
  void SetupApp::readMissing() {
    std::weak_ptr<std::vector<std::string>> weak = m_missing;
    sp::run({"sh", "-c", "\"$0\" doctor 2>/dev/null | sed -n 's/^ *✗ *//p'", cliCommand()}, [weak](const std::string& out, int) {
      auto m = weak.lock();
      if (!m) return;
      std::vector<std::string> lines;
      std::istringstream in(out);
      for (std::string l; std::getline(in, l);) {
        if (!trim(l).empty()) lines.push_back(l);
      }
      *m = std::move(lines);
      sp::refresh();
    });
  }

  void SetupApp::requestLayout() {
    if (m_surface != nullptr) m_surface->requestLayout();
  }

  bool SetupApp::ownsKeyboardSurface(wl_surface* surface) const noexcept {
    return m_surface != nullptr && surface != nullptr && surface == m_surface->wlSurface();
  }

  void SetupApp::onSettingsChanged() {
    if (m_content != nullptr) m_content->syncTree();
    requestLayout();
  }

  void SetupApp::onConfigReload() {
    if (!isOpen()) return;
    sp::reapplyPending();
    if (kusanagi::font() != m_builtFont) {
      // Every label carries its font, so rebuild everything (keeping step and scroll).
      m_sceneDirty = true;
      m_contentNeedsBuild = true;
      m_keepScroll = true;
      requestLayout();
      return;
    }
    onSettingsChanged();
  }

  void SetupApp::goTo(int step) {
    step = std::clamp(step, 0, kStepCount - 1);
    if (step == m_step && !m_contentNeedsBuild) return;
    m_step = step;
    m_contentNeedsBuild = true;
    for (StepItem* s : m_steps) s->restyle(m_step);
    requestLayout();
  }

  void SetupApp::buildContent() {
    m_contentNeedsBuild = false;
    if (m_holder == nullptr) return;
    m_animations.cancelForOwner(m_holder);
    if (m_content != nullptr) {
      m_holder->removeChild(m_content);
      m_content = nullptr;
    }
    const Step& st = kSteps[m_step];
    std::unique_ptr<sp::Column> col;
    if (st.page != nullptr) {
      // A Settings page. Pages without a builder show their placeholder.
      col = std::make_unique<sp::Column>(22.0F);
      for (const auto& p : sp::pages()) {
        if (std::string(p.id) != st.page) continue;
        p.build != nullptr ? p.build(*col) : sp::buildPlaceholder(*col, p);
      }
    } else if (std::string(st.id) == "welcome") {
      col = std::make_unique<sp::Column>(18.0F);
      buildWelcome(*col, m_missing);
    } else if (std::string(st.id) == "extras") {
      col = std::make_unique<sp::Column>(22.0F);
      buildExtras(*col);
    } else {
      col = std::make_unique<sp::Column>(18.0F);
      buildDone(*col);
    }
    m_content = static_cast<sp::Column*>(m_holder->addChild(std::move(col)));

    const bool heading = st.page != nullptr || std::string(st.id) == "extras";
    m_headName->setVisible(heading);
    m_headDesc->setVisible(heading);
    m_headName->setText(st.name);
    m_headDesc->setText(st.desc != nullptr ? st.desc : std::string(st.id) == "extras" ? "A few things Kusanagi can take over. All optional." : "");

    for (sp::Chip* c : {m_skip, m_back, m_next, m_finish}) c->syncTree();

    if (m_keepScroll) {
      m_keepScroll = false;
      return;
    }
    // New step: back to the top and fade in.
    m_scroll = m_scrollTarget = 0.0F;
    m_content->setOpacity(0.0F);
    m_animations.animate(
        0.0F, 1.0F, 220.0F, Easing::Linear, [this](float t) {
          if (m_content != nullptr) m_content->setOpacity(t);
        },
        {}, m_holder
    );
  }

  void SetupApp::scrollTo(float offset, bool animate) {
    if (m_scroller == nullptr) return;
    const float maxOff = std::max(0.0F, m_contentH + 30.0F - m_scroller->height());
    const float to = std::clamp(offset, 0.0F, maxOff);
    m_scrollTarget = to;
    m_animations.cancelForOwner(m_scroller);
    if (!animate) {
      m_scroll = to;
      requestLayout();
      return;
    }
    const float from = m_scroll;
    m_animations.animate(
        0.0F, 1.0F, 160.0F, Easing::EaseOutCubic, [this, from, to](float t) {
          m_scroll = from + (to - from) * t;
          requestLayout();
        },
        {}, m_scroller
    );
  }

  void SetupApp::buildScene() {
    m_sceneDirty = false;
    m_dispatcher.setSceneRoot(nullptr);
    m_surface->setSceneRoot(nullptr);
    m_animations.cancelAll();
    m_steps.clear();
    m_content = nullptr;
    m_builtFont = kusanagi::font();

    m_root = ui::node({});
    m_root->setAnimationManager(&m_animations);
    m_bg = static_cast<Box*>(m_root->addChild(ui::box({})));

    m_side = static_cast<Box*>(m_root->addChild(ui::box({})));
    m_logo = static_cast<Image*>(m_side->addChild(ui::image({.fit = ImageFit::Contain})));
    m_logoLoaded = false;
    m_brand = static_cast<Label*>(m_side->addChild(sp::makeText("", 14.0F, true)));
    sp::setSpacedText(*m_brand, "KUSANAGI", 4.0F);
    m_brandSub = static_cast<Label*>(m_side->addChild(sp::makeText("", 10.0F, false, sp::dim())));
    sp::setSpacedText(*m_brandSub, "setup", 1.0F);
    for (int i = 0; i < kStepCount; ++i) {
      auto item = std::make_unique<StepItem>(*this, i);
      item->restyle(m_step);
      m_steps.push_back(static_cast<StepItem*>(m_side->addChild(std::move(item))));
    }
    m_divider = static_cast<Box*>(m_root->addChild(ui::box({})));

    m_main = m_root->addChild(ui::node({}));
    m_headName = static_cast<Label*>(m_main->addChild(sp::makeText("", 24.0F, true)));
    m_headName->setMaxLines(1);
    m_headDesc = static_cast<Label*>(m_main->addChild(sp::makeText("", 12.0F, false, sp::dim())));
    m_headDesc->setMaxLines(1);
    m_scroller = static_cast<InputArea*>(m_main->addChild(ui::inputArea({})));
    m_scroller->setClipChildren(true);
    m_scroller->setOnAxisHandler([this](const InputArea::PointerData& d) {
      if (d.axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return false;
      scrollTo(m_scrollTarget + d.scrollDelta(60.0F), true);
      return true;
    });
    m_holder = m_scroller->addChild(ui::node({}));

    m_footer = static_cast<Box*>(m_root->addChild(ui::box({})));
    m_footerLine = static_cast<Box*>(m_footer->addChild(ui::box({})));
    m_skip = static_cast<sp::Chip*>(m_footer->addChild(std::make_unique<sp::Chip>("Skip setup")));
    m_skip->onClick([this]() { DeferredCall::callLater([this]() { close(); }); })->showIf([this]() { return m_step < kStepCount - 1; });
    m_back = static_cast<sp::Chip*>(m_footer->addChild(std::make_unique<sp::Chip>("Back", 0xf0141)));
    m_back->onClick([this]() { DeferredCall::callLater([this]() { goTo(m_step - 1); }); })->showIf([this]() { return m_step > 0; });
    m_next = static_cast<sp::Chip*>(m_footer->addChild(std::make_unique<sp::Chip>("Start", 0xf0142)));
    m_next->onWhen([]() { return true; })
        ->labelFrom([this]() { return std::string(m_step == 0 ? "Start" : "Next"); })
        ->onClick([this]() { DeferredCall::callLater([this]() { goTo(m_step + 1); }); })
        ->showIf([this]() { return m_step < kStepCount - 1; });
    m_finish = static_cast<sp::Chip*>(m_footer->addChild(std::make_unique<sp::Chip>("Finish", 0xf012c)));
    m_finish->onWhen([]() { return true; })
        ->onClick([this]() { DeferredCall::callLater([this]() { close(); }); })
        ->showIf([this]() { return m_step == kStepCount - 1; });

    m_contentNeedsBuild = true;
    m_dispatcher.setTextInputContext(m_surface->wlSurface(), m_wayland->textInputService());
    m_dispatcher.setCursorShapeCallback([this](std::uint32_t serial, std::uint32_t shape) { m_wayland->setCursorShape(serial, shape); });
    m_dispatcher.setSceneRoot(m_root.get());
    m_surface->setSceneRoot(m_root.get());
  }

  void SetupApp::layoutScene(Renderer& renderer) {
    const auto w = static_cast<float>(m_surface->width());
    const auto h = static_cast<float>(m_surface->height());
    m_root->setSize(w, h);
    m_bg->setSize(w, h);
    m_bg->setFill(sp::bgPanel(std::max(0.92F, sp::get<float>("panel.opacity", 0.95F))));

    m_side->setPosition(0.0F, 0.0F);
    m_side->setSize(kSideW, h);
    m_side->setFill(sp::textA(0.03F));
    m_brand->measure(renderer);
    m_brandSub->measure(renderer);
    const float colH = m_brand->height() + m_brandSub->height();
    const float rowH = std::max(36.0F, colH);
    if (!m_logoLoaded) {
      m_logoLoaded = true;
      (void)m_logo->setSourceFile(renderer, paths::assetPath("kusanagi-logo.svg").string(), 72, true);
    }
    m_logo->setSize(36.0F, 36.0F);
    m_logo->setPosition(22.0F, 26.0F); // Matches the classic look: top-aligned, not centred.
    const float cy = 26.0F + std::round((rowH - colH) / 2.0F);
    m_brand->setPosition(22.0F + 36.0F + 12.0F, cy);
    m_brandSub->setPosition(22.0F + 36.0F + 12.0F, cy + m_brand->height());
    float y = 26.0F + rowH + 4.0F + 22.0F + 4.0F;
    for (StepItem* s : m_steps) {
      s->place(renderer);
      s->setPosition(22.0F, y);
      y += 36.0F + 4.0F;
    }
    m_divider->setPosition(kSideW, 0.0F);
    m_divider->setSize(1.0F, h);
    m_divider->setFill(sp::textA(0.06F));

    const float fw = std::max(1.0F, w - kSideW);
    m_footer->setPosition(kSideW, h - kFooterH);
    m_footer->setSize(fw, kFooterH);
    m_footer->setFill(sp::textA(0.02F));
    m_footerLine->setSize(fw, 1.0F);
    m_footerLine->setFill(sp::textA(0.06F));
    for (sp::Chip* c : {m_skip, m_back, m_next, m_finish}) {
      c->setVisible(c->shown());
      c->place(renderer, 0.0F);
    }
    m_skip->setPosition(28.0F, std::round((kFooterH - m_skip->height()) / 2.0F));
    float rx = fw - 28.0F;
    for (sp::Chip* c : {m_finish, m_next, m_back}) {
      if (!c->shown()) continue;
      rx -= c->width();
      c->setPosition(rx, std::round((kFooterH - c->height()) / 2.0F));
      rx -= 8.0F;
    }

    const float mainX = kSideW + 1.0F;
    const float mainW = std::max(1.0F, w - mainX);
    const float mainH = std::max(1.0F, h - kFooterH);
    m_main->setPosition(mainX, 0.0F);
    m_main->setSize(mainW, mainH);
    if (m_contentNeedsBuild) buildContent();
    float top = 0.0F;
    if (m_headName->visible()) {
      m_headName->measure(renderer);
      m_headDesc->setMaxWidth(std::max(1.0F, mainW - 72.0F));
      m_headDesc->measure(renderer);
      m_headName->setPosition(36.0F, 30.0F);
      m_headDesc->setPosition(36.0F, 30.0F + m_headName->height() + 4.0F);
      top = 30.0F + m_headName->height() + 4.0F + m_headDesc->height();
    }
    const float scrollTop = top + 22.0F;
    m_scroller->setPosition(0.0F, scrollTop);
    m_scroller->setSize(mainW, std::max(0.0F, mainH - scrollTop));
    const float contentW = std::min(740.0F, mainW - 72.0F);
    if (m_content != nullptr) {
      m_contentH = m_content->place(renderer, std::max(1.0F, contentW));
      m_content->setPosition(0.0F, 0.0F);
    }
    const float maxOff = std::max(0.0F, m_contentH + 30.0F - m_scroller->height());
    m_scroll = std::min(m_scroll, maxOff);
    m_scrollTarget = std::min(m_scrollTarget, maxOff);
    m_holder->setPosition(36.0F, -std::round(m_scroll));
    m_holder->setSize(std::max(1.0F, contentW), m_contentH);
  }

  void SetupApp::prepareFrame() {
    if (m_surface == nullptr || m_renderContext == nullptr) return;
    if (m_surface->width() == 0 || m_surface->height() == 0) return;
    m_renderContext->makeCurrent(m_surface->renderTarget());
    Renderer& renderer = m_surface->renderTarget().renderer();
    UiPhaseScope layoutPhase(UiPhase::Layout);
    if (m_sceneDirty || m_root == nullptr) buildScene();
    layoutScene(renderer);
    if (m_animations.hasActive()) m_surface->requestRedraw();
  }

  void SetupApp::wakeAfterInput() {
    if (m_root == nullptr || m_surface == nullptr) return;
    if (m_root->layoutDirty()) {
      m_surface->requestLayout();
    } else if (m_root->paintDirty() || m_animations.hasActive()) {
      m_surface->requestRedraw();
    }
  }

  bool SetupApp::onPointerEvent(const PointerEvent& event) {
    if (!isOpen()) return false;
    const bool onThis = event.surface != nullptr && event.surface == m_surface->wlSurface();
    bool consumed = false;
    switch (event.type) {
    case PointerEvent::Type::Enter:
      if (onThis) {
        m_pointerInside = true;
        m_dispatcher.pointerEnter(static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial);
        consumed = true;
      }
      break;
    case PointerEvent::Type::Leave:
      if (onThis) {
        m_pointerInside = false;
        m_dispatcher.pointerLeave();
        consumed = true;
      }
      break;
    case PointerEvent::Type::Motion:
      if (onThis || m_pointerInside) {
        m_dispatcher.pointerMotion(static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial);
        consumed = m_pointerInside;
      }
      break;
    case PointerEvent::Type::Button:
      if (onThis || m_pointerInside) {
        m_dispatcher.pointerMotion(static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial);
        m_dispatcher.pointerButton(
            static_cast<float>(event.sx), static_cast<float>(event.sy), event.button, event.pressed, event.serial, event.time, event.touch
        );
        consumed = m_pointerInside;
      }
      break;
    case PointerEvent::Type::Axis:
      if (m_pointerInside) {
        m_dispatcher.pointerAxis(
            static_cast<float>(event.sx), static_cast<float>(event.sy), event.axis, event.axisSource, event.axisValue, event.axisDiscrete,
            event.axisValue120, event.axisLines
        );
        consumed = true;
      }
      break;
    }
    wakeAfterInput();
    return consumed;
  }

  void SetupApp::onKeyboardEvent(const KeyboardEvent& event) {
    if (!isOpen()) return;
    if (event.pressed && KeybindMatcher::matches(KeybindAction::Cancel, event.sym, event.modifiers)) {
      DeferredCall::callLater([this]() { close(); });
      return;
    }
    m_dispatcher.keyEvent(event.sym, event.utf32, event.modifiers, event.pressed, event.preedit);
    wakeAfterInput();
  }

} // namespace kusanagi
