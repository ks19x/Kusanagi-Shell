#include "core/process/portable_environment.h"
#include "shell/greeter/kusanagi_greeter.h"

#include "config/config_types.h"
#include "config/kusanagi_import.h"
#include "core/deferred_call.h"
#include "core/input/key_modifiers.h"
#include "core/log.h"
#include "core/timer_manager.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/animation/animation_manager.h"
#include "render/core/blur_cache.h"
#include "render/backend/render_backend.h"
#include "render/core/renderer.h"
#include "render/core/texture_manager.h"
#include "render/gl_shared_context.h"
#include "render/render_context.h"
#include "render/scene/input_area.h"
#include "render/scene/input_dispatcher.h"
#include "render/scene/node.h"
#include "render/scene/wallpaper_node.h"
#include "shell/greeter/greetd_client.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/lockscreen/kusanagi_lock_view.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"
#include "ui/palette.h"
#include "util/file_utils.h"
#include "wayland/toplevel_surface.h"
#include "wayland/wayland_connection.h"
#include "wayland/wayland_seat.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <linux/input-event-codes.h>
#include <memory>
#include <optional>
#include <poll.h>
#include <pwd.h>
#include <regex>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <wayland-client-core.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace kusanagi::greeter {

  namespace {

    constexpr Logger kLog("greeter");
    constexpr const char* kIconFont = "JetBrainsMono Nerd Font";

    // Nerd Font codepoints for the pills.
    constexpr char32_t kUserIcon = 0xF0004;    // nf-md-account
    constexpr char32_t kSessionIcon = 0xF0379; // nf-md-monitor
    constexpr char32_t kRebootIcon = 0xF0709;  // nf-md-restart
    constexpr char32_t kPowerIcon = 0xF0425;   // nf-md-power
    constexpr char32_t kCloseIcon = 0xF0156;   // nf-md-close

    constexpr float kPillHeight = 40.0F;
    constexpr float kPillPad = 28.0F;     // added to the row's width
    constexpr float kPillSpacing = 10.0F; // between icon and label, and between pills
    constexpr float kMargin = 28.0F;      // the bottom rows
    constexpr float kCloseMargin = 20.0F; // the preview's close pill

    std::atomic<bool> g_quit{false};
    void onSignal(int /*sig*/) { g_quit = true; }

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

    float outCubic(float t) { return 1.0F - std::pow(1.0F - std::clamp(t, 0.0F, 1.0F), 3.0F); }
    float outQuint(float t) { return 1.0F - std::pow(1.0F - std::clamp(t, 0.0F, 1.0F), 5.0F); }

    std::string env(const char* name) {
      const char* v = std::getenv(name);
      return v != nullptr ? std::string(v) : std::string();
    }

    std::string trim(std::string s) {
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
      }
      std::size_t i = 0;
      while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
      }
      return s.substr(i);
    }

    std::string readFile(const std::string& path) {
      std::ifstream in(path);
      std::string out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      return out;
    }

    // Single-quotes a word for sh; greetd joins start_session's words with spaces and runs them through sh.
    std::string shQuote(const std::string& word) {
      std::string out = "'";
      for (const char c : word) {
        if (c == '\'') {
          out += "'\\''";
        } else {
          out += c;
        }
      }
      return out + "'";
    }

    // Fire and forget, fully detached. The double fork leaves nothing to reap later.
    void spawnDetached(const std::vector<std::string>& argv) {
      const pid_t pid = ::fork();
      if (pid < 0) {
        return;
      }
      if (pid == 0) {
        ::setsid();
        if (::fork() != 0) {
          ::_exit(0);
        }
        std::vector<char*> args;
        for (const auto& a : argv) {
          args.push_back(const_cast<char*>(a.c_str()));
        }
        args.push_back(nullptr);
        process::portable::restoreHostEnvironment();
        ::execvp(args[0], args.data());
        ::_exit(127);
      }
      int status = 0;
      while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
      }
    }

    struct User {
      std::string name;
      std::string display;
      std::string shell;
      std::string face;
    };

    struct Session {
      std::string id;
      std::string name;
      std::string exec;
      std::string desktop;
    };

    // Users from passwd with uid 1000 to 59999 and a login shell (not nologin or false).
    std::vector<User> readUsers(const std::string& home) {
      std::vector<User> users;
      auto add = [&](const std::string& name, unsigned uid, const std::string& gecos, const std::string& shell) {
        static const std::regex noLogin("nologin|false$");
        if (uid < 1000 || uid >= 60000 || std::regex_search(shell, noLogin)) {
          return;
        }
        const std::string first = gecos.substr(0, gecos.find(','));
        users.push_back({name, first.empty() ? name : first, shell.empty() ? "/bin/sh" : shell, home + "/faces/" + name});
      };
      // KUSANAGI_GREETER_USERS=<passwd-format file> is only for testing the picker.
      if (const std::string file = env("KUSANAGI_GREETER_USERS"); !file.empty()) {
        std::istringstream lines(readFile(file));
        for (std::string line; std::getline(lines, line);) {
          std::vector<std::string> f;
          std::size_t start = 0;
          for (std::size_t i = 0; i <= line.size(); ++i) {
            if (i == line.size() || line[i] == ':') {
              f.push_back(line.substr(start, i - start));
              start = i + 1;
            }
          }
          if (f.size() >= 7) {
            add(f[0], static_cast<unsigned>(std::strtoul(f[2].c_str(), nullptr, 10)), f[4], f[6]);
          }
        }
        return users;
      }
      ::setpwent();
      while (const passwd* pw = ::getpwent()) {
        add(pw->pw_name != nullptr ? pw->pw_name : "", pw->pw_uid, pw->pw_gecos != nullptr ? pw->pw_gecos : "",
            pw->pw_shell != nullptr ? pw->pw_shell : "");
      }
      ::endpwent();
      return users;
    }

    // Sessions from /usr/share/wayland-sessions/*.desktop: id (the file name), Name=, Exec=, DesktopNames=.
    std::vector<Session> readSessions() {
      std::vector<std::filesystem::path> files;
      std::error_code ec;
      for (const auto& entry : std::filesystem::directory_iterator("/usr/share/wayland-sessions", ec)) {
        if (entry.path().extension() == ".desktop" && entry.is_regular_file(ec)) {
          files.push_back(entry.path());
        }
      }
      std::ranges::sort(files);
      std::vector<Session> sessions;
      for (const auto& file : files) {
        std::string name;
        std::string exec;
        std::string desktopNames;
        std::istringstream lines(readFile(file.string()));
        for (std::string line; std::getline(lines, line);) {
          auto take = [&](const char* key, std::string& out) {
            const std::size_t n = std::strlen(key);
            if (out.empty() && line.compare(0, n, key) == 0) {
              out = line.substr(n);
            }
          };
          take("Name=", name);
          take("Exec=", exec);
          take("DesktopNames=", desktopNames);
        }
        Session s;
        s.id = file.stem().string();
        s.name = name.empty() ? s.id : name;
        s.exec = exec.empty() ? s.id : exec;
        const std::string desktop = !desktopNames.empty() ? desktopNames : !name.empty() ? name : s.id;
        s.desktop = desktop.substr(0, desktop.find(';'));
        sessions.push_back(std::move(s));
      }
      return sessions;
    }

    // An icon (and optional label) in a capsule that lights up under the pointer.
    struct Pill {
      Node* node = nullptr;
      Box* bg = nullptr;
      Label* icon = nullptr;
      Label* label = nullptr;
      InputArea* area = nullptr;
      bool danger = false;
      bool hovered = false;
      float hover = 0.0F; // 0 to 1, drives the hover colour fade
      AnimationManager::Id anim = 0;
      float width = 0.0F;
    };

  } // namespace

  class Greeter {
  public:
    explicit Greeter(bool preview) : m_preview(preview) {}

    ~Greeter() {
      m_animations.cancelAll();
      m_surface.reset();
      m_view.reset();
      if (m_glReady) {
        releaseTextures();
      }
      // The scene goes before the render context: members are destroyed in reverse order.
    }

    int run();

  private:
    bool connectGreetd();
    void loadLook();
    void build();
    void buildPill(Pill& pill, char32_t icon, bool danger, std::function<void()> onClick);
    void applyPillColor(Pill& pill);
    void setPillHovered(Pill& pill, bool hovered);
    void prepareFrame(bool needsUpdate, bool needsLayout);
    void layout(Renderer& renderer, float width, float height);
    float layoutPill(Renderer& renderer, Pill& pill, const std::string& label);
    void applyWallpaper();
    void releaseTextures();
    void playEnter();
    void playExit();
    void setBackground(float opacity, float scale);
    void loop();

    // The login itself
    [[nodiscard]] const User& user() const;
    [[nodiscard]] const Session* session() const;
    void submit();
    void fail(const std::string& message);
    void launch();
    void onGreetd(const GreetdClient::Response& response);
    void clearPassword();
    void onKey(const KeyboardEvent& event);
    void onPointer(const PointerEvent& event);
    void power(const char* what);
    void dirty();

    bool m_preview = false;
    bool m_glReady = false;
    std::string m_home;

    WaylandConnection m_wayland;
    GlSharedContext m_glShared;
    RenderContext m_renderContext;
    // Declared before m_root: ~Node cancels its animations through it.
    AnimationManager m_animations;
    Node m_root;
    std::unique_ptr<LockView> m_view;
    std::unique_ptr<ToplevelSurface> m_surface;
    InputDispatcher m_input;

    Box* m_windowFill = nullptr;
    Node* m_background = nullptr;
    WallpaperNode* m_wallpaper = nullptr;
    Box* m_dim = nullptr;
    Node* m_chrome = nullptr;
    Pill m_userPill;
    Pill m_sessionPill;
    Pill m_rebootPill;
    Pill m_powerPill;
    Pill m_closePill;

    std::string m_wallPath;
    bool m_wallDirty = true;
    TextureHandle m_wallTexture{};
    TextureHandle m_blurTexture{};
    BlurCache m_blurCache;
    std::uint32_t m_blurWidth = 0;
    std::uint32_t m_blurHeight = 0;
    bool m_entered = false;
    AnimationManager::Id m_bgAnim = 0;

    std::vector<User> m_users;
    std::vector<Session> m_sessions;
    std::size_t m_ui = 0;
    std::size_t m_si = 0;
    User m_fallbackUser;

    GreetdClient m_greetd;
    std::string m_password;
    std::string m_error;
    bool m_busy = false;
    bool m_unlocking = false;
    std::uint64_t m_failures = 0;
    bool m_answered = false;       // the typed password went to this attempt's first prompt
    bool m_awaitingInput = false;  // a further prompt (e.g. a verification code) waits for the user
    bool m_recreate = false;       // greetd had a session half set up: cancel, then create again
    bool m_launched = false;
    int m_exitCode = 0;
  };

  // Setup

  int Greeter::run() {
    m_home = env("HOME");
    m_fallbackUser = {env("USER"), env("USER"), "/bin/sh", ""};

    if (!m_preview && !connectGreetd()) {
      return 1;
    }
    loadLook();
    m_users = readUsers(m_home);
    m_sessions = readSessions();
    {
      const std::string want = opt<std::string>("greeter", "user", "");
      const auto it = std::ranges::find(m_users, want, &User::name);
      m_ui = it != m_users.end() ? static_cast<std::size_t>(it - m_users.begin()) : 0;
      const std::string wantSession = opt<std::string>("greeter", "session", "");
      const auto sit = std::ranges::find(m_sessions, wantSession, &Session::id);
      m_si = sit != m_sessions.end() ? static_cast<std::size_t>(sit - m_sessions.begin()) : 0;
    }
    kLog.info(
        "{}: {} user(s), {} session(s), style {}", m_preview ? "preview" : "greetd", m_users.size(), m_sessions.size(),
        opt<std::string>("lock", "style", "center")
    );

    m_wayland.setMinimalGlobals(true);
    if (!m_wayland.connect()) {
      kLog.error("can't connect to the Wayland display");
      return 1;
    }
    m_glShared.initialize(m_wayland.display(), false);
    m_renderContext.initialize(m_glShared);
    m_glReady = true;
    m_renderContext.setTextFontFamily(font());

    build();

    m_surface = std::make_unique<ToplevelSurface>(m_wayland);
    m_surface->setRenderContext(&m_renderContext);
    m_surface->setAnimationManager(&m_animations);
    m_surface->setSceneRoot(&m_root);
    m_surface->setClosedCallback([this]() {
      if (m_preview) {
        g_quit = true;
      }
    });
    m_surface->setConfigureCallback([this](std::uint32_t, std::uint32_t) { m_surface->requestLayout(); });
    m_surface->setPrepareFrameCallback([this](bool needsUpdate, bool needsLayout) {
      prepareFrame(needsUpdate, needsLayout);
    });

    wl_output* output = nullptr;
    std::uint32_t width = 1920;
    std::uint32_t height = 1080;
    for (const auto& out : m_wayland.outputs()) {
      if (out.output != nullptr) {
        output = out.output;
        if (out.width > 0 && out.height > 0) {
          width = static_cast<std::uint32_t>(out.width / std::max(1, out.scale));
          height = static_cast<std::uint32_t>(out.height / std::max(1, out.scale));
        }
        break;
      }
    }
    // The whole screen (cage fullscreens it anyway); the preview is a 1280x800 window.
    ToplevelSurfaceConfig cfg{
        .width = m_preview ? 1280U : width,
        .height = m_preview ? 800U : height,
        .title = m_preview ? "Kusanagi login screen — preview" : "Kusanagi login",
        .appId = "dev.kusanagi.Greeter",
    };
    if (!m_surface->initialize(output, cfg)) {
      kLog.error("can't create the window");
      return 1;
    }

    m_wayland.setPointerEventCallback([this](const PointerEvent& event) { onPointer(event); });
    m_wayland.setKeyboardEventCallback([this](const KeyboardEvent& event) { onKey(event); });

    loop();
    return m_exitCode;
  }

  bool Greeter::connectGreetd() {
    const std::string path = env("GREETD_SOCK");
    std::string error;
    if (!m_greetd.connect(path, error)) {
      kLog.error("can't connect to greetd ({}): {}", path, error);
      return false;
    }
    return true;
  }

  void Greeter::loadLook() {
    // settings.json and colors.json, the copies `kusanagi greeter sync` puts in the greeter's home.
    if (!kusanagi::config::loadKusanagiLook(FileUtils::configDir())) {
      kLog.warn("no settings.json in {}: default look", FileUtils::configDir());
    }
    m_wallPath = trim(readFile(m_home + "/.config/kusanagi/wallpaper"));
  }

  void Greeter::build() {
    m_root.setAnimationManager(&m_animations);

    m_windowFill = static_cast<Box*>(m_root.addChild(ui::box({.fill = color("panel")})));
    m_windowFill->setZIndex(0);

    // The blurred wallpaper with lock.dim on top.
    m_background = m_root.addChild(ui::node({.zIndex = 1}));
    m_wallpaper = static_cast<WallpaperNode*>(m_background->addChild(std::make_unique<WallpaperNode>()));
    m_wallpaper->setFillMode(WallpaperFillMode::Crop);
    m_dim = static_cast<Box*>(m_background->addChild(ui::box({.fill = color("#000000")})));
    m_dim->setZIndex(1);
    setBackground(0.0F, 1.08F);

    m_view = std::make_unique<LockView>(
        m_root, m_animations,
        [this]() {
          if (m_surface != nullptr) {
            m_surface->requestUpdate();
          }
        },
        [this]() {
          if (m_surface != nullptr) {
            m_surface->requestLayout();
          }
        },
        [this]() {
          if (m_surface != nullptr) {
            m_surface->requestRedraw();
          }
        }
    );

    // User and session on the left, reboot and power off on the right.
    m_chrome = m_root.addChild(ui::node({.zIndex = 10}));
    buildPill(m_userPill, kUserIcon, false, [this]() {
      if (m_users.empty()) {
        return;
      }
      if (m_awaitingInput) {
        (void)m_greetd.cancelSession();
        m_awaitingInput = false;
      }
      m_ui = (m_ui + 1) % m_users.size();
      clearPassword();
      m_error.clear();
      dirty();
    });
    buildPill(m_sessionPill, kSessionIcon, false, [this]() {
      if (!m_sessions.empty()) {
        m_si = (m_si + 1) % m_sessions.size();
        dirty();
      }
    });
    buildPill(m_rebootPill, kRebootIcon, false, [this]() { power("reboot"); });
    buildPill(m_powerPill, kPowerIcon, true, [this]() { power("poweroff"); });
    buildPill(m_closePill, kCloseIcon, false, []() { g_quit = true; });

    m_input.setSceneRoot(&m_root);
    m_input.setCursorShapeCallback([this](std::uint32_t serial, std::uint32_t shape) {
      m_wayland.setCursorShape(serial, shape);
    });
  }

  void Greeter::buildPill(Pill& pill, char32_t icon, bool danger, std::function<void()> onClick) {
    pill.danger = danger;
    pill.node = m_chrome->addChild(ui::node({}));
    pill.bg = static_cast<Box*>(pill.node->addChild(ui::box({
        .border = color("text/0.1"),
        .borderWidth = 1.0F,
        .radius = kPillHeight / 2.0F,
    })));
    pill.icon = static_cast<Label*>(pill.node->addChild(ui::label({
        .text = utf8(icon),
        .fontSize = 16.0F,
        .fontFamily = std::string(kIconFont),
        .color = color("text"),
        .maxLines = 1,
        .baselineMode = LabelBaselineMode::FontLine,
    })));
    pill.label = static_cast<Label*>(pill.node->addChild(ui::label({
        .fontSize = 12.0F,
        .fontWeight = FontWeight::Bold,
        .fontFamily = font(),
        .color = color("text"),
        .maxLines = 1,
        .baselineMode = LabelBaselineMode::FontLine,
    })));
    pill.area = static_cast<InputArea*>(pill.node->addChild(ui::inputArea({
        .acceptedButtons = InputArea::buttonMask({BTN_LEFT}),
        .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
        .onEnter = [this, &pill](const InputArea::PointerData&) { setPillHovered(pill, true); },
        .onLeave = [this, &pill]() { setPillHovered(pill, false); },
        .onClick = [onClick = std::move(onClick)](const InputArea::PointerData&) { onClick(); },
    })));
    applyPillColor(pill);
  }

  void Greeter::applyPillColor(Pill& pill) {
    const Color base = resolveColorSpec(color("panel/0.55"));
    const Color lit = resolveColorSpec(color(pill.danger ? "danger/0.3" : "accent/0.3"));
    pill.bg->setFill(ColorSpec{.role = std::nullopt, .fixed = lerpColor(base, lit, pill.hover), .alpha = 1.0F});
  }

  void Greeter::setPillHovered(Pill& pill, bool hovered) {
    if (pill.hovered == hovered) {
      return;
    }
    pill.hovered = hovered;
    m_animations.cancel(pill.anim);
    const float from = pill.hover;
    const float to = hovered ? 1.0F : 0.0F;
    // Linear, and a reversal takes the full duration too.
    pill.anim = m_animations.animate(
        from, to, 140.0F, Easing::Linear,
        [this, &pill](float v) {
          pill.hover = v;
          applyPillColor(pill);
          m_surface->requestRedraw();
        },
        [&pill]() { pill.anim = 0; }, pill.bg
    );
  }

  // Frame

  void Greeter::prepareFrame(bool needsUpdate, bool needsLayout) {
    if (m_surface == nullptr || m_surface->renderContext() == nullptr) {
      return;
    }
    Renderer& renderer = m_surface->renderTarget().renderer();
    if (needsUpdate) {
      LockView::Prompt prompt;
      prompt.who = user().display;
      // One dot per code point, not per byte.
      prompt.passwordLength = static_cast<std::size_t>(std::ranges::count_if(m_password, [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0U) != 0x80U;
      }));
      prompt.busy = m_busy;
      prompt.error = m_error;
      prompt.failures = m_failures;
      prompt.focused = true;
      // The user's face file, else ~/.face.
      m_view->setAvatarPath(!user().face.empty() ? user().face : m_home + "/.face");
      m_view->setPrompt(prompt);
      m_view->sync(renderer);
    }
    if (needsUpdate || needsLayout) {
      layout(renderer, static_cast<float>(m_surface->width()), static_cast<float>(m_surface->height()));
    }
    if (!m_entered && m_surface->width() > 0) {
      m_entered = true;
      playEnter();
    }
  }

  void Greeter::layout(Renderer& renderer, float width, float height) {
    m_root.setSize(width, height);
    m_windowFill->setPosition(0.0F, 0.0F);
    m_windowFill->setSize(width, height);

    m_background->setPosition(0.0F, 0.0F);
    m_background->setSize(width, height);
    m_background->setTransformOrigin(width / 2.0F, height / 2.0F);
    applyWallpaper();
    m_wallpaper->setPosition(0.0F, 0.0F);
    m_wallpaper->setSize(width, height);
    const float dim = m_view->dim();
    m_dim->setVisible(dim > 0.0F);
    m_dim->setFill(color("#000000/" + std::to_string(dim)));
    m_dim->setPosition(0.0F, 0.0F);
    m_dim->setSize(width, height);

    m_view->setHidden(false);
    m_view->layout(renderer, width, height);

    m_chrome->setPosition(0.0F, 0.0F);
    m_chrome->setSize(width, height);
    const float y = height - kMargin - kPillHeight;
    // Left: the user (only when there's a choice) and the session.
    float x = kMargin;
    m_userPill.node->setVisible(m_users.size() > 1);
    if (m_users.size() > 1) {
      const float w = layoutPill(renderer, m_userPill, user().display);
      m_userPill.node->setPosition(x, y);
      x += w + kPillSpacing;
    }
    const Session* s = session();
    (void)layoutPill(renderer, m_sessionPill, s != nullptr ? s->name : "No sessions");
    m_sessionPill.node->setPosition(x, y);
    // Right: reboot and power off.
    const float pw = layoutPill(renderer, m_powerPill, "");
    const float rw = layoutPill(renderer, m_rebootPill, "");
    m_powerPill.node->setPosition(width - kMargin - pw, y);
    m_rebootPill.node->setPosition(width - kMargin - pw - kPillSpacing - rw, y);
    // The preview's way out.
    m_closePill.node->setVisible(m_preview);
    if (m_preview) {
      const float cw = layoutPill(renderer, m_closePill, "Close preview");
      m_closePill.node->setPosition(width - kCloseMargin - cw, kCloseMargin);
    }
  }

  float Greeter::layoutPill(Renderer& renderer, Pill& pill, const std::string& label) {
    pill.label->setVisible(!label.empty());
    pill.label->setText(label);
    pill.icon->measure(renderer);
    pill.label->measure(renderer);
    // The icon and label row is centred on whole pixels; the pill's width stays fractional.
    const float rowW = pill.icon->width() + (label.empty() ? 0.0F : kPillSpacing + pill.label->width());
    const float rowH = std::max(pill.icon->height(), label.empty() ? 0.0F : pill.label->height());
    const float w = rowW + kPillPad;
    const float rowX = std::round((w - rowW) / 2.0F);
    const float rowY = std::round((kPillHeight - rowH) / 2.0F);
    pill.icon->setPosition(rowX, rowY + std::round((rowH - pill.icon->height()) / 2.0F));
    pill.label->setPosition(
        std::round(rowX + pill.icon->width() + kPillSpacing), rowY + std::round((rowH - pill.label->height()) / 2.0F)
    );
    pill.node->setSize(w, kPillHeight);
    pill.bg->setPosition(0.0F, 0.0F);
    pill.bg->setSize(w, kPillHeight);
    pill.area->setPosition(0.0F, 0.0F);
    pill.area->setSize(w, kPillHeight);
    pill.width = w;
    return w;
  }

  void Greeter::applyWallpaper() {
    const std::uint32_t bw = m_surface->renderTarget().bufferWidth();
    const std::uint32_t bh = m_surface->renderTarget().bufferHeight();
    if (bw != m_blurWidth || bh != m_blurHeight) {
      m_wallDirty = true;
    }
    if (!m_wallDirty || bw == 0 || bh == 0) {
      return;
    }
    m_wallDirty = false;
    m_blurWidth = bw;
    m_blurHeight = bh;
    std::error_code ec;
    if (m_wallPath.empty() || !std::filesystem::is_regular_file(m_wallPath, ec)) {
      m_wallpaper->setVisible(false);
      return;
    }
    auto& textures = m_renderContext.textureManager();
    m_renderContext.makeCurrent(m_surface->renderTarget());
    if (m_wallTexture.id == 0) {
      // Never more pixels than the screen has; a 4K wallpaper decoded whole is ~35 MB for nothing.
      m_wallTexture = textures.loadFromFile(m_wallPath, static_cast<int>(std::max(bw, bh)), true);
      if (m_wallTexture.id == 0) {
        kLog.warn("can't load the wallpaper {}", m_wallPath);
        m_wallpaper->setVisible(false);
        return;
      }
    }
    // lock.blur maps onto the native lock's blur: intensity = blur * 0.3, radius = intensity * 40.
    const float intensity = std::clamp(static_cast<float>(opt<double>("lock", "blur", 0.55) * 0.3), 0.0F, 1.0F);
    m_blurTexture = {}; // the cache owns it
    if (intensity > 0.0F) {
      m_blurTexture = m_blurCache.get(m_renderContext.backend(), m_wallTexture, bw, bh, intensity * 40.0F, 3);
    }
    if (m_blurTexture.id != 0) {
      // Only the blurred copy is shown, so drop the source; a resize decodes it again.
      textures.unload(m_wallTexture);
      m_wallTexture = {};
    }
    const TextureHandle shown = m_blurTexture.id != 0 ? m_blurTexture : m_wallTexture;
    m_wallpaper->setTextures(
        shown.id, {}, static_cast<float>(shown.width), static_cast<float>(shown.height), 0.0F, 0.0F
    );
    m_wallpaper->setTransition(WallpaperTransition::Fade, 0.0F, TransitionParams{});
    m_wallpaper->setFillMode(WallpaperFillMode::Crop);
    m_wallpaper->setVisible(true);
  }

  void Greeter::releaseTextures() {
    auto& textures = m_renderContext.textureManager();
    m_renderContext.backend().makeCurrentNoSurface();
    m_blurTexture = {};
    m_blurCache.destroy();
    if (m_wallTexture.id != 0) {
      textures.unload(m_wallTexture);
      m_wallTexture = {};
    }
  }

  void Greeter::setBackground(float opacity, float scale) {
    m_background->setOpacity(opacity);
    m_background->setScale(scale);
  }

  void Greeter::playEnter() {
    m_view->playEnter();
    // The wallpaper fades in and zooms from 1.08 down to 1.
    const float fadeMs = static_cast<float>(ms(420));
    const float zoomMs = static_cast<float>(ms(700));
    const float startOpacity = m_background->opacity();
    const float startScale = m_background->scale();
    m_animations.cancel(m_bgAnim);
    m_bgAnim = m_animations.animate(
        0.0F, std::max(fadeMs, zoomMs), std::max(fadeMs, zoomMs), Easing::Linear,
        [this, fadeMs, zoomMs, startOpacity, startScale](float t) {
          const float o = startOpacity + (1.0F - startOpacity) * outCubic(t / fadeMs);
          const float s = startScale + (1.0F - startScale) * outQuint(t / zoomMs);
          setBackground(o, s);
          m_surface->requestRedraw();
        },
        [this]() { m_bgAnim = 0; }, m_background
    );
  }

  void Greeter::playExit() {
    m_view->playExit({});
    // The wallpaper fades out and grows back to 1.08.
    const float fadeMs = static_cast<float>(ms(420));
    const float zoomMs = static_cast<float>(ms(700));
    const float startOpacity = m_background->opacity();
    const float startScale = m_background->scale();
    m_animations.cancel(m_bgAnim);
    m_bgAnim = m_animations.animate(
        0.0F, std::max(fadeMs, zoomMs), std::max(fadeMs, zoomMs), Easing::Linear,
        [this, fadeMs, zoomMs, startOpacity, startScale](float t) {
          setBackground(startOpacity * (1.0F - outCubic(t / fadeMs)), startScale + (1.08F - startScale) * outQuint(t / zoomMs));
          m_surface->requestRedraw();
        },
        [this]() { m_bgAnim = 0; }, m_background
    );
  }

  void Greeter::dirty() {
    if (m_surface != nullptr) {
      m_surface->requestUpdate();
    }
  }

  // Input

  void Greeter::onPointer(const PointerEvent& event) {
    switch (event.type) {
    case PointerEvent::Type::Enter:
      m_input.pointerEnter(static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial);
      break;
    case PointerEvent::Type::Leave:
      m_input.pointerLeave();
      break;
    case PointerEvent::Type::Motion:
      m_input.pointerMotion(static_cast<float>(event.sx), static_cast<float>(event.sy), event.serial);
      break;
    case PointerEvent::Type::Button:
      m_input.pointerButton(
          static_cast<float>(event.sx), static_cast<float>(event.sy), event.button, event.pressed, event.serial,
          event.time, event.touch
      );
      break;
    case PointerEvent::Type::Axis:
      break;
    }
    if (m_root.layoutDirty()) {
      m_surface->requestLayout();
    } else if (m_root.paintDirty()) {
      m_surface->requestRedraw();
    }
  }

  // The password field: type, Backspace, Enter submits, Escape clears.
  void Greeter::onKey(const KeyboardEvent& event) {
    if (!event.pressed || event.preedit || m_unlocking) {
      return;
    }
    const bool ctrl = (event.modifiers & KeyMod::Ctrl) != 0;
    switch (event.sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
      submit();
      return;
    case XKB_KEY_Escape:
      if (m_awaitingInput) {
        // A second prompt was waiting: give up on this attempt.
        (void)m_greetd.cancelSession();
        m_awaitingInput = false;
        m_error.clear();
      }
      clearPassword();
      dirty();
      return;
    case XKB_KEY_BackSpace:
      if (m_busy) {
        return;
      }
      if (ctrl) {
        clearPassword(); // a password field has no words: Ctrl+Backspace takes it all
      } else {
        // Drop the last code point.
        while (!m_password.empty() && (static_cast<unsigned char>(m_password.back()) & 0xC0U) == 0x80U) {
          m_password.pop_back();
        }
        if (!m_password.empty()) {
          m_password.pop_back();
        }
      }
      dirty();
      return;
    default:
      break;
    }
    if (ctrl && (event.sym == XKB_KEY_u || event.sym == XKB_KEY_U)) {
      clearPassword();
      dirty();
      return;
    }
    if (m_busy || event.utf32 < 0x20 || event.utf32 == 0x7F
        || (event.modifiers & (KeyMod::Ctrl | KeyMod::Alt | KeyMod::Super)) != 0) {
      return;
    }
    m_password += utf8(static_cast<char32_t>(event.utf32));
    dirty();
  }

  void Greeter::clearPassword() {
    ::explicit_bzero(m_password.data(), m_password.size());
    m_password.clear();
  }

  void Greeter::power(const char* what) {
    if (m_preview) {
      return;
    }
    spawnDetached({"sh", "-c", std::string("loginctl ") + what + " || systemctl " + what});
  }

  // greetd

  const User& Greeter::user() const { return m_ui < m_users.size() ? m_users[m_ui] : m_fallbackUser; }

  const Session* Greeter::session() const { return m_si < m_sessions.size() ? &m_sessions[m_si] : nullptr; }

  void Greeter::submit() {
    if (m_busy || m_unlocking || m_password.empty()) {
      return;
    }
    m_error.clear();
    if (m_preview) {
      const Session* s = session();
      m_error = "Preview — would log " + user().name + " into " + (s != nullptr ? s->name : "?");
      clearPassword();
      dirty();
      return;
    }
    m_busy = true;
    bool sent = false;
    if (m_awaitingInput) {
      m_awaitingInput = false;
      sent = m_greetd.postResponse(m_password);
    } else {
      m_answered = false;
      sent = m_greetd.createSession(user().name);
    }
    if (!sent) {
      fail("Lost the connection to greetd");
    }
    dirty();
  }

  void Greeter::fail(const std::string& message) {
    m_busy = false;
    m_awaitingInput = false;
    m_error = message.empty() ? "Wrong password" : message;
    clearPassword();
    ++m_failures; // shakes the pill
    dirty();
  }

  void Greeter::launch() {
    const Session* s = session();
    if (s == nullptr) {
      fail("No session to start");
      (void)m_greetd.cancelSession();
      return;
    }
    m_unlocking = true;
    playExit();
    // Tell the launcher a session was handed over, so it never falls back after a login.
    if (const std::string mark = env("KUSANAGI_GREETER_MARK"); !mark.empty()) {
      const int fd = ::open(mark.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
      if (fd >= 0) {
        ::close(fd);
      }
    }
    // The user's login shell (for their profile: PATH and friends) runs `kusanagi session`, which sets up its
    // own D-Bus, a runtime folder, a log and one retry. Plain dbus-run-session when Kusanagi isn't on PATH.
    const std::string script = "if command -v kusanagi >/dev/null 2>&1; then exec kusanagi session " + s->exec
        + "; else exec dbus-run-session " + s->exec + "; fi";
    std::vector<std::string> cmd = {user().shell, "-l", "-c", script};
    for (auto& word : cmd) {
      word = shQuote(word);
    }
    const std::vector<std::string> envs = {
        "XDG_SESSION_TYPE=wayland", "XDG_CURRENT_DESKTOP=" + s->desktop, "XDG_SESSION_DESKTOP=" + s->id
    };
    kLog.info("starting {} for {}", s->id, user().name);
    if (!m_greetd.startSession(cmd, envs)) {
      m_unlocking = false;
      playEnter();
      fail("Lost the connection to greetd");
    }
  }

  void Greeter::onGreetd(const GreetdClient::Response& r) {
    using Request = GreetdClient::Request;
    switch (r.request) {
    case Request::CancelSession:
      if (m_recreate) {
        m_recreate = false;
        if (!m_greetd.createSession(user().name)) {
          fail("Lost the connection to greetd");
        }
      }
      return;
    case Request::StartSession:
      if (r.type == "success") {
        m_launched = true;
        g_quit = true; // greetd starts the session once we're gone
      } else {
        m_unlocking = false;
        playEnter();
        (void)m_greetd.cancelSession();
        fail(r.description);
      }
      return;
    case Request::CreateSession:
    case Request::PostResponse:
      break;
    }

    if (r.type == "auth_message") {
      if (r.authType == "secret" || r.authType == "visible") {
        if (!m_answered) {
          m_answered = true;
          if (!m_greetd.postResponse(m_password)) {
            fail("Lost the connection to greetd");
          }
        } else {
          // Another question (a verification code, a new password, ...): ask it on the error line.
          m_busy = false;
          m_awaitingInput = true;
          std::string question = trim(r.authMessage);
          m_error = question.empty() ? "Password" : question;
          clearPassword();
          dirty();
        }
      } else if (r.authType == "error") {
        (void)m_greetd.cancelSession();
        fail(trim(r.authMessage));
      } else {
        (void)m_greetd.postResponse(std::nullopt); // info: nothing to answer
      }
    } else if (r.type == "success") {
      launch();
    } else {
      if (r.request == Request::CreateSession && r.errorType == "error"
          && r.description == "a session is already being configured") {
        m_recreate = true;
        (void)m_greetd.cancelSession();
        return;
      }
      (void)m_greetd.cancelSession();
      fail(r.description);
    }
  }

  // A lean main loop

  void Greeter::loop() {
    wl_display* display = m_wayland.display();
    while (!g_quit) {
      for (auto& fn : DeferredCall::takePending()) {
        if (fn) {
          fn();
        }
      }
      while (wl_display_prepare_read(display) != 0) {
        if (wl_display_dispatch_pending(display) < 0) {
          kLog.error("Wayland dispatch failed");
          m_exitCode = m_launched ? 0 : 1;
          return;
        }
      }
      short wlEvents = POLLIN;
      if (wl_display_flush(display) < 0) {
        if (errno != EAGAIN) {
          wl_display_cancel_read(display);
          kLog.error("Wayland connection lost");
          m_exitCode = m_launched ? 0 : 1;
          return;
        }
        wlEvents |= POLLOUT;
      }

      std::vector<pollfd> fds;
      fds.push_back({.fd = wl_display_get_fd(display), .events = wlEvents, .revents = 0});
      const int greetdIdx = m_greetd.connected() ? static_cast<int>(fds.size()) : -1;
      if (greetdIdx >= 0) {
        fds.push_back({.fd = m_greetd.fd(), .events = POLLIN, .revents = 0});
      }
      const int deferredIdx = DeferredCall::wakeFd() >= 0 ? static_cast<int>(fds.size()) : -1;
      if (deferredIdx >= 0) {
        fds.push_back({.fd = DeferredCall::wakeFd(), .events = POLLIN, .revents = 0});
      }

      int timeout = -1;
      auto vote = [&timeout](int t) {
        if (t >= 0 && (timeout < 0 || t < timeout)) {
          timeout = t;
        }
      };
      vote(TimerManager::instance().pollTimeoutMs());
      vote(m_wayland.repeatPollTimeoutMs());
      if (Surface::hasPendingFrameWork() || Surface::hasPendingRenders()) {
        timeout = 0;
      }
      if ((wlEvents & POLLOUT) != 0 && timeout >= 0 && timeout < 16) {
        timeout = 16;
      }

      const int rc = ::poll(fds.data(), fds.size(), timeout);
      if (rc < 0) {
        wl_display_cancel_read(display);
        if (errno == EINTR) {
          continue;
        }
        kLog.error("poll failed");
        m_exitCode = 1;
        return;
      }
      if ((fds[0].revents & POLLIN) != 0) {
        if (wl_display_read_events(display) < 0) {
          kLog.error("Wayland read failed");
          m_exitCode = m_launched ? 0 : 1;
          return;
        }
      } else {
        wl_display_cancel_read(display);
      }
      if ((fds[0].revents & (POLLERR | POLLHUP)) != 0) {
        kLog.error("the compositor went away");
        m_exitCode = m_launched ? 0 : 1;
        return;
      }
      if (wl_display_dispatch_pending(display) < 0) {
        kLog.error("Wayland dispatch failed");
        m_exitCode = m_launched ? 0 : 1;
        return;
      }

      if (greetdIdx >= 0 && fds[static_cast<std::size_t>(greetdIdx)].revents != 0) {
        if (!m_greetd.readAvailable([this](const GreetdClient::Response& r) { onGreetd(r); })) {
          if (!m_launched) {
            // greetd went away and there is nothing left to log in to; let the launcher take over.
            kLog.error("greetd closed the connection");
            m_exitCode = 1;
            return;
          }
        }
      }
      if (deferredIdx >= 0 && fds[static_cast<std::size_t>(deferredIdx)].revents != 0) {
        DeferredCall::drainWakeFd();
      }
      TimerManager::instance().tick();
      m_wayland.repeatTick();
      Surface::drainPendingFrameWork();
      Surface::drainPendingRenders();
    }
    (void)wl_display_flush(display);
    m_exitCode = 0;
  }

  int run(int argc, char* argv[]) {
    bool preview = env("GREETD_SOCK").empty();
    for (int i = 1; i < argc; ++i) {
      if (std::strcmp(argv[i], "--preview") == 0) {
        preview = true;
      } else if (std::strcmp(argv[i], "--probe") == 0) {
        // `kusanagi greeter sync` asks before copying a binary over; older ones don't know --greeter.
        std::puts("kusanagi-shell --greeter");
        return 0;
      }
    }
    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);
    std::signal(SIGPIPE, SIG_IGN);
    {
      const std::string home = env("HOME");
      if (!home.empty()) {
        (void)::chdir(home.c_str());
      }
    }
    try {
      Greeter greeter(preview);
      return greeter.run();
    } catch (const std::exception& e) {
      kLog.error("fatal: {}", e.what());
      return 1;
    }
  }

} // namespace kusanagi::greeter
