#include "shell/bar/widgets/kusanagi_taskbar.h"

#include "compositors/compositor_platform.h"
#include "core/deferred_call.h"
#include "core/files/resource_paths.h"
#include "render/animation/animation_manager.h"
#include "render/core/renderer.h"
#include "render/scene/node.h"
#include "shell/bar/widgets/kusanagi_module_widget.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "system/app_identity.h"
#include "system/desktop_entry.h"
#include "system/desktop_entry_launch.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "wayland/wayland_connection.h"

#include <linux/input-event-codes.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

using json = nlohmann::json;

namespace {

  std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
  }

  // The first `n` characters, not bytes.
  std::string prefix(const std::string& s, std::size_t n) {
    std::size_t i = 0;
    for (std::size_t k = 0; i < s.size() && k < n; ++k) {
      ++i;
      while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    }
    return s.substr(0, i);
  }
  std::size_t chars(const std::string& s) {
    return static_cast<std::size_t>(std::ranges::count_if(s, [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
  }

  std::string escape(const std::string& t) {
    std::string out;
    for (char c : t) out += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : std::string(1, c);
    return out;
  }

  ColorSpec tok(const json& colors, const char* key, const char* fallback) {
    const auto it = colors.find(key);
    const std::string t = it != colors.end() && it->is_string() && !it->get<std::string>().empty() ? it->get<std::string>() : fallback;
    return KusanagiModuleWidget::tokenColor(t, kusanagi::color(fallback));
  }

  const DesktopEntry* entryById(const std::string& id) {
    for (const auto& e : desktopEntries()) {
      if (e.id == id) return &e;
    }
    return nullptr;
  }

} // namespace

KusanagiTaskbar::KusanagiTaskbar(CompositorPlatform* platform) : m_platform(platform) {}

KusanagiTaskbar::~KusanagiTaskbar() = default;

std::unique_ptr<Node> KusanagiTaskbar::create() {
  auto root = std::make_unique<Node>();
  m_root = root.get();
  return root;
}

std::string KusanagiTaskbar::keyOf(const std::string& appId) {
  if (const auto it = m_keyCache.find(appId); it != m_keyCache.end()) return it->second;
  // Exact id first ("foot" is foot.desktop), the heuristic only when that fails.
  std::string key = appId;
  const std::string low = lower(appId);
  const DesktopEntry* found = entryById(appId);
  if (found == nullptr) {
    for (const auto& e : desktopEntries()) {
      if (e.idLower == low) {
        found = &e;
        break;
      }
    }
  }
  if (found != nullptr) {
    key = found->id;
  } else if (!appId.empty()) {
    if (const auto e = app_identity::findDesktopEntry(appId, desktopEntries()); e.has_value()) key = e->id;
  }
  m_keyCache[appId] = key;
  return key;
}

std::string KusanagiTaskbar::nameOf(const std::string& app) const {
  if (app == "org.quickshell") return "Kusanagi";
  const DesktopEntry* e = entryById(app);
  return e != nullptr ? e->name : app;
}

std::string KusanagiTaskbar::iconOf(const std::string& app, int size) {
  if (app.empty()) return {};
  if (const auto it = m_iconCache.find(app); it != m_iconCache.end()) return it->second;
  std::string found;
  if (app == "org.quickshell") {
    found = paths::assetPath("kusanagi-logo.svg").string(); // Kusanagi's own windows
  } else {
    // An icon that really exists: the entry's (name or absolute path), then the id in a few spellings, then a
    // generic app icon. Empty means none, and the item shows a letter tile.
    const DesktopEntry* e = entryById(app);
    const std::string last = app.substr(app.rfind('.') == std::string::npos ? 0 : app.rfind('.') + 1);
    for (const std::string& n : {e != nullptr ? e->icon : std::string(), app, lower(app), last, lower(last), std::string("application-x-executable")}) {
      if (n.empty()) continue;
      if (n.front() == '/') {
        std::error_code ec;
        if (std::filesystem::exists(n, ec)) found = n;
      } else {
        found = m_icons.resolve(n, size);
      }
      if (!found.empty()) break;
    }
  }
  m_iconCache[app] = found;
  return found;
}

std::vector<const KusanagiTaskbar::Window*> KusanagiTaskbar::windowsOf(const std::string& app) const {
  std::vector<const Window*> out;
  for (const auto& w : m_windows) {
    const auto it = m_keyCache.find(w.appId);
    if ((it != m_keyCache.end() ? it->second : w.appId) == app) out.push_back(&w);
  }
  return out;
}

void KusanagiTaskbar::rebuild(const std::vector<std::string>& keys) {
  while (!m_root->children().empty()) (void)m_root->removeChild(m_root->children().back().get());
  m_items.clear();
  m_items.reserve(keys.size());
  for (const auto& key : keys) {
    Item item;
    item.key = key;
    item.isApp = key.starts_with("a:");
    if (item.isApp) item.app = key.substr(2);
    auto node = std::make_unique<Node>();
    item.node = node.get();
    item.bg = static_cast<Box*>(node->addChild(ui::box({.radius = 6.0F})));
    item.tile = static_cast<Box*>(node->addChild(ui::box({.fill = kusanagi::color("accent/0.3")})));
    item.letter = static_cast<Label*>(node->addChild(ui::label({.fontWeight = FontWeight::Bold, .fontFamily = kusanagi::font(), .maxLines = 1})));
    item.icon = static_cast<Image*>(node->addChild(ui::image({.fit = ImageFit::Contain})));
    item.title = static_cast<Label*>(node->addChild(ui::label({.fontWeight = FontWeight::Normal, .maxLines = 1})));
    item.title->setUseMarkup(true);
    item.line = static_cast<Box*>(node->addChild(ui::box({.radius = 1.0F})));
    for (int i = 0; i < 3; ++i) item.dots.push_back(static_cast<Box*>(node->addChild(ui::box({.radius = 2.0F}))));
    m_root->addChild(std::move(node));
    m_items.push_back(std::move(item));
  }
}

bool KusanagiTaskbar::sync(Renderer& renderer, const Look& look, AnimationManager* animations) {
  if (m_root == nullptr) return false;
  const json& o = look.eff;
  const float s = look.scale;
  if (const auto v = desktopEntriesVersion(); v != m_entriesVersion) {
    m_entriesVersion = v;
    m_keyCache.clear();
    m_iconCache.clear();
  }

  // Open windows, in the order they appeared.
  m_windows.clear();
  if (m_platform != nullptr) {
    m_platform->wayland().visitWlrToplevels([this](const WlrToplevelSnapshot& t) {
      m_windows.push_back(Window{.handle = t.handle, .title = t.title, .appId = t.appId, .activated = t.activated, .order = t.order});
    });
  }
  std::ranges::sort(m_windows, {}, &Window::order);
  for (const auto& w : m_windows) (void)keyOf(w.appId);

  // Options, with defaults from the dock settings (the translator passes them as _dock).
  const json dock = o.value("_dock", json::object());
  const bool titles = o.value("titles", json()) == json(true) && !look.vertical;
  const bool grouped = o.contains("grouped") && o["grouped"].is_boolean() ? o["grouped"].get<bool>()
                       : titles ? false
                                : dock.value("grouped", true);
  const float iconSize = o.value("iconSize", json()).is_number() && o["iconSize"].get<float>() > 0 ? o["iconSize"].get<float>() : 16.0F;
  const std::size_t tw = o.value("titleWidth", json()).is_number() && o["titleWidth"].get<int>() > 0 ? o["titleWidth"].get<std::size_t>() : 18;
  std::string indicator = o.value("indicator", json()).is_string() ? o["indicator"].get<std::string>() : std::string();
  if (indicator.empty()) indicator = dock.value("indicator", std::string("dot"));
  const json colors = o.value("colors", json::object()).is_object() ? o["colors"] : json::object();
  const float spacing = (o.value("spacing", json()).is_number() ? o["spacing"].get<float>() : 4.0F) * s;
  const json pinsJson = o.value("pinned", json()).is_array() ? o["pinned"] : dock.value("pinned", json::array());
  m_pins.clear();
  for (const auto& p : pinsJson) {
    if (p.is_string()) m_pins.push_back(p.get<std::string>());
  }

  // What to show, as stable keys: pinned apps, then the open windows (one per app when grouped).
  std::vector<std::string> keys;
  std::vector<std::string> seen;
  for (const auto& id : m_pins) {
    keys.push_back("a:" + id);
    seen.push_back(id);
  }
  for (std::size_t i = 0; i < m_windows.size(); ++i) {
    const std::string k = keyOf(m_windows[i].appId);
    if (grouped) {
      if (std::ranges::find(seen, k) == seen.end()) {
        seen.push_back(k);
        keys.push_back("a:" + k);
      }
    } else {
      keys.push_back("w:" + std::to_string(i));
    }
  }
  // Ungrouped, pinned apps that have windows show as their windows instead.
  if (!grouped) {
    std::erase_if(keys, [this](const std::string& k) { return k.starts_with("a:") && !windowsOf(k.substr(2)).empty(); });
  }
  bool changed = false;
  if (keys.size() != m_items.size() || !std::ranges::equal(keys, m_items, {}, {}, &Item::key)) {
    rebuild(keys);
    changed = true;
  }

  const float cross = look.cross;
  const float isz = std::round(iconSize * s);
  const int iconPx = std::max(1, static_cast<int>(std::lround(iconSize * s)));
  float at = 0.0F;
  for (auto& it : m_items) {
    const Window* tl = nullptr;
    if (!it.isApp) {
      const std::size_t i = static_cast<std::size_t>(std::stoul(it.key.substr(2)));
      tl = i < m_windows.size() ? &m_windows[i] : nullptr;
      it.app = tl != nullptr ? keyOf(tl->appId) : std::string();
      it.window = tl != nullptr ? tl->handle : nullptr;
    }
    const auto wins = it.isApp ? windowsOf(it.app) : (tl != nullptr ? std::vector<const Window*>{tl} : std::vector<const Window*>{});
    const bool running = !wins.empty();
    const bool active = std::ranges::any_of(wins, [](const Window* w) { return w->activated; });
    const std::string title = tl != nullptr ? (!tl->title.empty() ? tl->title : tl->appId) : nameOf(it.app);

    // The icon, or the app's first letter on a tile.
    const std::string path = iconOf(it.app, iconPx);
    if (path != it.iconPath) {
      it.iconPath = path;
      if (!path.empty()) (void)it.icon->setSourceFile(renderer, path, iconPx * 2, true);
    }
    const bool hasIcon = !it.iconPath.empty();
    it.icon->setVisible(hasIcon);
    it.tile->setVisible(!hasIcon);
    it.letter->setVisible(!hasIcon);
    it.icon->setOpacity(running || !titles ? 1.0F : 0.75F);
    if (!hasIcon) {
      const std::string name = nameOf(it.app).empty() ? (it.app.empty() ? std::string("?") : it.app) : nameOf(it.app);
      std::string first = prefix(name, 1);
      if (first.size() == 1) first[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(first[0])));
      it.letter->setText(first);
      it.letter->setFontSize(std::round(iconSize * 0.55F) * look.fontScale);
      it.letter->setColor(kusanagi::color("text"));
      it.letter->measure(renderer);
      it.tile->setRadius(isz / 4.0F);
    }

    // The title (titles: true), cut to titleWidth characters.
    float textW = 0.0F;
    it.title->setVisible(titles);
    if (titles) {
      const std::string text = escape(chars(title) > tw ? prefix(title, tw - 1) + "…" : title);
      if (!look.font.empty()) it.title->setFontFamily(look.font);
      it.title->setFontSize(look.baseFont * look.fontScale);
      it.title->setFontWeight(static_cast<FontWeight>(look.fontWeight));
      // Titles keep their unhinted width (see m_probe): measure the text at 10x, where hinting is lost in the
      // noise, and spread the difference between the letters.
      const std::string cacheKey = text + "\x1f" + look.font + std::to_string(look.baseFont * look.fontScale) + std::to_string(look.fontWeight);
      auto cached = m_titleMarkup.find(cacheKey);
      if (cached == m_titleMarkup.end()) {
        if (m_probe == nullptr) {
          m_probe = ui::label({.maxLines = 1});
          m_probe->setUseMarkup(true);
        }
        // Advances only: measuring the text twice over minus once cancels the label's constant side bearings.
        auto advance = [&renderer](Label& l, const std::string& t) {
          l.setText(t + t);
          l.measure(renderer);
          const float twice = l.width();
          l.setText(t);
          l.measure(renderer);
          return twice - l.width();
        };
        const float hinted = advance(*it.title, text);
        if (!look.font.empty()) m_probe->setFontFamily(look.font);
        m_probe->setFontWeight(static_cast<FontWeight>(look.fontWeight));
        m_probe->setFontSize(look.baseFont * look.fontScale * 10.0F);
        const float unhinted = advance(*m_probe, text) / 10.0F;
        const float n = static_cast<float>(std::max<std::size_t>(1, chars(title) > tw ? tw : chars(title)));
        const float extra = (unhinted - hinted) / n;
        const long ls = std::lround(extra * 1024.0F * renderer.renderScale());
        if (m_titleMarkup.size() > 256) m_titleMarkup.clear();
        cached = m_titleMarkup.emplace(cacheKey, Title{ls != 0 ? "<span letter_spacing=\"" + std::to_string(ls) + "\">" + text + "</span>" : text, unhinted}).first;
      }
      it.title->setText(cached->second.markup);
      it.title->setColor(tok(colors, "text", "text"));
      it.title->setOpacity(active ? 1.0F : 0.7F);
      it.title->measure(renderer);
      textW = cached->second.width;
    }
    const float rowW = isz + (titles ? 6.0F * s + textW : 0.0F);
    it.w = look.vertical ? cross : rowW + 12.0F * s;
    it.h = look.vertical ? isz + 12.0F * s : cross;
    it.x = look.vertical ? 0.0F : at;
    it.y = look.vertical ? at : 0.0F;
    at += (look.vertical ? it.h : it.w) + spacing;
    it.node->setPosition(std::round(it.x), std::round(it.y));
    it.node->setSize(it.w, it.h);

    // The active window's backdrop, with titles only.
    it.bg->setPosition(2.0F * s, 2.0F * s);
    it.bg->setSize(std::max(0.0F, it.w - 4.0F * s), std::max(0.0F, it.h - 4.0F * s));
    it.bg->setRadius(6.0F * s);
    it.bg->setVisible(active && titles);
    if (active && titles) it.bg->setFill(tok(colors, "active", "text/0.14"));

    const float rx = std::round((it.w - rowW) / 2.0F);
    const float iy = std::round((it.h - isz) / 2.0F);
    it.icon->setPosition(rx, iy);
    it.icon->setSize(isz, isz);
    it.tile->setPosition(rx, iy);
    it.tile->setSize(isz, isz);
    it.letter->setPosition(std::round(rx + (isz - it.letter->width()) / 2.0F), std::round(iy + (isz - it.letter->height()) / 2.0F));
    if (titles) it.title->setPosition(rx + isz + 6.0F * s, std::round((it.h - it.title->height()) / 2.0F));

    // Running: dots (one per window, up to 3) or a line, under the icon or on a side bar's outer side.
    const bool side = look.vertical;
    const bool left = look.edge == "left";
    const ColorSpec on = tok(colors, "indicator", "accent");
    const std::size_t n = std::min<std::size_t>(3, wins.size());
    const bool dots = indicator == "dot" && running;
    const float dotsW = static_cast<float>(n) * 4.0F * s + static_cast<float>(n > 0 ? n - 1 : 0) * 3.0F * s;
    for (std::size_t d = 0; d < it.dots.size(); ++d) {
      Box* dot = it.dots[d];
      dot->setVisible(dots && d < n);
      if (!dots || d >= n) continue;
      const float x0 = side ? (left ? 1.0F * s : it.w - 5.0F * s) : std::round((it.w - dotsW) / 2.0F);
      const float y0 = side ? std::round((it.h - 4.0F * s) / 2.0F) : it.h - 5.0F * s;
      dot->setPosition(x0 + static_cast<float>(d) * 7.0F * s, y0);
      dot->setSize(4.0F * s, 4.0F * s);
      dot->setRadius(2.0F * s);
      dot->setFill(active ? on : tok(colors, "text", "text/0.6"));
    }
    const bool line = indicator == "line" && running;
    it.line->setVisible(line);
    if (line) {
      it.line->setFill(active ? on : tok(colors, "text", "text/0.5"));
      it.line->setRadius(1.0F * s);
      Box* box = it.line;
      const float w = it.w, h = it.h;
      if (side) {
        const float len = active ? h - 10.0F * s : 10.0F * s;
        box->setSize(2.0F * s, len);
        box->setPosition(left ? 0.0F : w - 2.0F * s, std::round((h - len) / 2.0F));
        it.lineLen = len;
      } else {
        const float to = active ? w - 10.0F * s : 10.0F * s;
        auto place = [box, w, h, s](float len) {
          box->setSize(len, 2.0F * s);
          box->setPosition(std::round((w - len) / 2.0F), h - 2.0F * s);
        };
        if (it.lineLen < 0.0F || animations == nullptr) {
          place(to);
        } else if (std::abs(to - it.lineLen) >= 0.5F) {
          animations->cancelForOwner(box);
          const float from = box->width();
          animations->animate(0.0F, 1.0F, static_cast<float>(kusanagi::ms(200)), Easing::EaseOutCubic,
                              [place, from, to](float t) { place(from + (to - from) * t); }, {}, box);
        } else if (std::abs(box->width() - to) < 0.5F) {
          place(to); // settled: follow the item's size
        }
        it.lineLen = to;
      }
    }
  }
  // Matches the classic look: the row is one spacing longer than its items, like the bar's sections.
  const float along = m_items.empty() ? 0.0F : at;
  const float w = look.vertical ? cross : along;
  const float h = look.vertical ? along : cross;
  changed = changed || w != m_width || h != m_height;
  m_width = w;
  m_height = h;
  m_root->setSize(w, h);
  return changed;
}

bool KusanagiTaskbar::click(float x, float y, std::uint32_t button) {
  for (const auto& it : m_items) {
    if (x < it.x || y < it.y || x >= it.x + it.w || y >= it.y + it.h) continue;
    const std::string app = it.app;
    if (button == BTN_RIGHT) {
      togglePin(app);
    } else if (button == BTN_MIDDLE) {
      launch(app);
    } else if (it.window != nullptr) {
      // The window may have closed since the last sync.
      if (m_platform != nullptr && m_platform->containsWlrToplevelHandle(it.window)) m_platform->activateToplevel(it.window);
    } else {
      activateApp(app);
    }
    return true;
  }
  return false;
}

void KusanagiTaskbar::launch(const std::string& app) {
  const DesktopEntry* e = entryById(app);
  if (e == nullptr || e->exec.empty() || m_platform == nullptr) return;
  CompositorPlatform* platform = m_platform;
  DeferredCall::callLater([platform, entry = *e]() {
    std::string token;
    if (platform->hasXdgActivation()) token = platform->requestActivationToken(nullptr);
    (void)desktop_entry_launch::launchEntry(
        entry, desktop_entry_launch::LaunchOptions{.activationToken = std::move(token), .dbusActivatable = entry.dbusActivatable, .dbusAppId = entry.id}
    );
  });
}

// A click on an app starts it, focuses it, or steps through its windows.
void KusanagiTaskbar::activateApp(const std::string& app) {
  const auto wins = windowsOf(app);
  if (wins.empty()) {
    launch(app);
    return;
  }
  const auto active = std::ranges::find_if(wins, [](const Window* w) { return w->activated; });
  const std::size_t i = active == wins.end() ? 0 : (static_cast<std::size_t>(active - wins.begin()) + 1) % wins.size();
  if (m_platform != nullptr && m_platform->containsWlrToplevelHandle(wins[i]->handle)) m_platform->activateToplevel(wins[i]->handle);
}

// Right click pins or unpins in dock.pinned, whatever the module's own "pinned" says.
void KusanagiTaskbar::togglePin(const std::string& app) {
  if (app.empty()) return;
  json pinned = kusanagi::opt<json>("dock", "pinned", json::array());
  if (!pinned.is_array()) pinned = json::array();
  json next = json::array();
  bool was = false;
  for (const auto& p : pinned) {
    if (p == json(app)) was = true;
    else next.push_back(p);
  }
  if (!was) {
    next = pinned;
    next.push_back(app);
  }
  (void)kusanagi::setOption("dock", "pinned", next);
}
