#include "shell/kusanagi/panel/cp_home.h"

#include "core/deferred_call.h"
#include "core/process/process.h"
#include "cursor-shape-v1-client-protocol.h"
#include "dbus/bluetooth/bluetooth_service.h"
#include "dbus/mpris/mpris_art.h"
#include "dbus/mpris/mpris_service.h"
#include "idle/idle_inhibitor.h"
#include "notification/notification_manager.h"
#include "pipewire/pipewire_service.h"
#include "render/animation/animation_manager.h"
#include "render/core/renderer.h"
#include "shell/control_center/control_center_services.h"
#include "shell/kusanagi/game_mode.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/panel/control_panel.h"
#include "shell/kusanagi/panel/cp_controls.h"
#include "shell/panel/panel_manager.h"
#include "system/brightness_service.h"
#include "system/system_monitor_service.h"
#include "system/weather_service.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <sstream>

namespace kusanagi {

  namespace {

    std::vector<std::string> strings(const char* section, const char* key, std::vector<std::string> fallback) {
      const auto& s = settings();
      const auto sec = s.find(section);
      if (sec == s.end() || !sec->is_object()) return fallback;
      const auto it = sec->find(key);
      if (it == sec->end() || !it->is_array()) return fallback;
      std::vector<std::string> out;
      for (const auto& v : *it) {
        if (v.is_string()) out.push_back(v.get<std::string>());
      }
      return out;
    }

    // Like `pgrep -x <name>`, but walking /proc instead of spawning a process.
    bool processRunning(const std::string& name) {
      DIR* dir = opendir("/proc");
      if (dir == nullptr) return false;
      bool found = false;
      while (const dirent* e = readdir(dir)) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        std::ifstream comm(std::string("/proc/") + e->d_name + "/comm");
        std::string c;
        if (std::getline(comm, c) && c == name) {
          found = true;
          break;
        }
      }
      closedir(dir);
      return found;
    }

    // WMO weather code to a Material Design weather glyph.
    char32_t weatherGlyph(int code, bool day) {
      if (code <= 1) return day ? 0xf0599 : 0xf0594;
      if (code == 2) return day ? 0xf0595 : 0xf0f31;
      if (code == 3) return 0xf0590;
      if (code == 45 || code == 48) return 0xf0591;
      if (code >= 95) return 0xf0593;
      if ((code >= 71 && code <= 77) || code == 85 || code == 86 || code == 56 || code == 57 || code == 66 || code == 67)
        return 0xf0598;
      if (code == 65 || code == 82 || code == 81) return 0xf0596;
      return 0xf0597;
    }

    std::string shortDay(const std::string& iso) {
      std::tm tm{};
      if (iso.size() < 10 || std::sscanf(iso.c_str(), "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) != 3) return "";
      tm.tm_year -= 1900;
      tm.tm_mon -= 1;
      tm.tm_hour = 12;
      std::mktime(&tm);
      static const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
      return days[tm.tm_wday];
    }

    std::string recorderElapsed(double since) {
      if (since <= 0.0) return "";
      const long s = std::max(0L, static_cast<long>(std::time(nullptr) - static_cast<long>(since)));
      const long h = s / 3600;
      const long m = (s / 60) % 60;
      char buf[32];
      if (h > 0) {
        std::snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", h, m, s % 60);
      } else {
        std::snprintf(buf, sizeof(buf), "%ld:%02ld", m, s % 60);
      }
      return buf;
    }

  } // namespace

  CpHome::CpHome(ControlPanel& panel, const ControlCenterServices& services) : CpPage(panel, services) {
    m_order = strings("panel", "order", {"tiles", "sliders", "media", "weather", "stats"});
    m_tileIds = strings("panel", "tiles", {"nightlight", "dnd", "mic", "gamemode", "screenshot", "record", "colorpicker", "wallpaper"});
    m_tileStyle = opt<std::string>("panel", "tileStyle", "cards");
    // Re-checked every time the panel opens.
    m_nightlight = processRunning("gammastep");
    readRecorder();

    for (const auto& section : m_order) {
      if (section == "tiles") buildTiles();
      else if (section == "sliders") buildSliders();
      else if (section == "weather") buildWeather();
      else if (section == "media") buildMedia();
      else if (section == "stats") buildStats();
    }
  }

  CpHome::~CpHome() {
    if (m_retainedSensors && m_services.sysmon != nullptr) {
      m_services.sysmon->releaseCpuTemp();
      m_services.sysmon->releaseGpuUsage();
    }
  }

  // Tiles

  void CpHome::readRecorder() {
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    std::ifstream f(std::string(rt != nullptr ? rt : "/tmp") + "/kusanagi/record.json");
    m_recordMode = "off";
    m_recordSince = 0.0;
    if (!f) return;
    try {
      const auto j = nlohmann::json::parse(f);
      m_recordMode = j.value("mode", std::string("off"));
      m_recordSince = j.value("since", 0.0);
    } catch (...) {
    }
  }

  CpHome::TileState CpHome::tile(const std::string& id) const {
    const AudioNode* source = m_services.audio != nullptr ? m_services.audio->defaultSource() : nullptr;
    const int replay = opt<int>("recorder", "replay", 30);
    if (id == "nightlight") {
      return {0xf0594, "Night light", m_nightlight ? std::to_string(opt<int>("display", "nightTemp", 4500)) + "K" : "Off",
              m_nightlight};
    }
    if (id == "dnd") {
      const bool dnd = m_services.notifications != nullptr && m_services.notifications->doNotDisturb();
      return {dnd ? char32_t{0xf009b} : char32_t{0xf009a}, "Do not disturb", dnd ? "Silenced" : "Off", dnd};
    }
    if (id == "mic") {
      const bool muted = source != nullptr && source->muted;
      return {muted ? char32_t{0xf036d} : char32_t{0xf036c}, "Microphone", muted ? "Muted" : "Live", source != nullptr && !muted};
    }
    if (id == "gamemode") {
      const auto* gm = GameModeService::instance();
      const bool active = gm != nullptr && gm->active();
      const bool autoOn = opt<bool>("gamemode", "auto", true);
      return {0xf0297, "Game mode",
              active ? (gm->manual() ? "On" : "On · auto") : (autoOn ? "Auto" : "Off"), active};
    }
    if (id == "screenshot") return {0xf0e51, "Screenshot", "Region", false};
    if (id == "record") {
      const bool rec = m_recordMode == "record";
      return {0xf044a, "Record",
              rec ? "Recording " + recorderElapsed(m_recordSince) : m_recordMode == "stream" ? "Streaming" : "Start",
              rec || m_recordMode == "stream"};
    }
    if (id == "replay") {
      const bool on = m_recordMode == "replay";
      return {0xf0450, "Replay", on ? "Last " + std::to_string(replay) + " s" : "Off", on};
    }
    if (id == "clip") {
      return {0xf0fd8, "Save clip", m_recordMode == "replay" ? "Last " + std::to_string(replay) + " s" : "Replay is off", false};
    }
    if (id == "updates") return {0xf06b0, "Updates", "Check", false};
    if (id == "colorpicker") return {0xf020a, "Colour picker", "Copy hex", false};
    if (id == "wallpaper") return {0xf0e09, "Wallpaper", "Pick & theme", false};
    if (id == "clipboard") return {0xf0147, "Clipboard", "History", false};
    if (id == "lock") return {0xf033e, "Lock", "Lock screen", false};
    if (id == "settings") return {0xf0493, "Settings", "Kusanagi", false};
    if (id == "launcher") return {0xf003b, "Apps", "Launcher", false};
    if (id == "caffeine") {
      const bool on = m_services.idleInhibitor != nullptr && m_services.idleInhibitor->enabled();
      return {0xf0176, "Caffeine", on ? "Staying awake" : "Off", on};
    }
    if (id == "bluetooth") {
      const BluetoothState* st = m_services.bluetooth != nullptr ? &m_services.bluetooth->state() : nullptr;
      const bool on = st != nullptr && st->powered;
      std::string first;
      int connected = 0;
      if (m_services.bluetooth != nullptr) {
        for (const auto& d : m_services.bluetooth->devices()) {
          if (!d.connected) continue;
          if (first.empty()) first = d.alias;
          ++connected;
        }
      }
      const std::string summary = st == nullptr || !st->adapterPresent ? "No adapter"
                                  : !on                                 ? "Off"
                                  : connected == 1                      ? first
                                  : connected > 1                       ? std::to_string(connected) + " connected"
                                                                        : "On";
      return {!on ? char32_t{0xf00b2} : connected > 0 ? char32_t{0xf00b1} : char32_t{0xf00af}, "Bluetooth", summary, on};
    }
    return {0, id, "", false};
  }

  void CpHome::press(const std::string& id) {
    const auto recorder = [](const std::string& what) {
      const std::string folder = opt<std::string>("recorder", "folder", "~/Videos");
      const char* home = std::getenv("HOME");
      std::vector<std::string> cmd{
          "env",
          "KR_FOLDER=" + (folder.starts_with("~") ? std::string(home != nullptr ? home : "") + folder.substr(1) : folder),
          "KR_FPS=" + std::to_string(opt<int>("recorder", "fps", 60)),
          "KR_QUALITY=" + opt<std::string>("recorder", "quality", "very_high"),
          "KR_REPLAY=" + std::to_string(opt<int>("recorder", "replay", 30)),
          "KR_AUDIO=" + opt<std::string>("recorder", "audio", "desktop"),
          "KR_CAPTURE=" + opt<std::string>("recorder", "capture", "screen"),
          "KR_CODEC=" + opt<std::string>("recorder", "codec", "auto"),
          "KR_STREAM_URL=" + opt<std::string>("recorder", "streamUrl", ""),
          "kusanagi", "record", what,
      };
      (void)process::runAsync(cmd);
    };

    if (id == "nightlight") {
      const std::string temp = std::to_string(opt<int>("display", "nightTemp", 4500));
      (void)process::runAsync(m_nightlight ? std::string("pkill -x gammastep")
                                           : "setsid -f gammastep -O " + temp + " >/dev/null 2>&1");
      m_nightlight = !m_nightlight;
    } else if (id == "dnd") {
      if (m_services.notifications != nullptr) (void)m_services.notifications->toggleDoNotDisturb();
    } else if (id == "mic") {
      if (m_services.audio != nullptr && m_services.audio->defaultSource() != nullptr) {
        m_services.audio->setMicMuted(!m_services.audio->defaultSource()->muted);
      }
    } else if (id == "gamemode") {
      if (auto* gm = GameModeService::instance(); gm != nullptr) gm->toggle();
    } else if (id == "screenshot") {
      m_panel.runClosed({"kusanagi", "screenshot", "region"});
    } else if (id == "record") {
      recorder("record");
    } else if (id == "replay") {
      recorder("replay");
    } else if (id == "clip") {
      recorder(m_recordMode == "replay" ? "save" : "replay");
    } else if (id == "updates") {
      m_panel.runClosed({"kusanagi", "updates", "upgrade"});
    } else if (id == "colorpicker") {
      m_panel.runClosed({"kusanagi", "colorpick"});
    } else if (id == "wallpaper") {
      m_panel.ipcClosed("panel-toggle wallpaper");
    } else if (id == "clipboard") {
      m_panel.ipcClosed("panel-toggle clipboard");
    } else if (id == "lock") {
      m_panel.ipcClosed("session lock");
    } else if (id == "settings") {
      m_panel.ipcClosed("settings-open");
    } else if (id == "launcher") {
      m_panel.ipcClosed("panel-toggle launcher");
    } else if (id == "caffeine") {
      if (m_services.idleInhibitor != nullptr) m_services.idleInhibitor->toggle();
    } else if (id == "bluetooth") {
      if (m_services.bluetooth != nullptr && m_services.bluetooth->state().adapterPresent) {
        m_services.bluetooth->setPowered(!m_services.bluetooth->state().powered);
      }
    }
    PanelManager::instance().refresh();
  }

  void CpHome::buildTiles() {
    if (m_tileIds.empty()) return;
    m_tilesSec = addChild(ui::node({}));
    for (const auto& id : m_tileIds) {
      auto t = std::make_unique<cp::Tile>(m_tileStyle);
      t->setOnActivate([this, id]() { press(id); });
      m_tiles.push_back(static_cast<cp::Tile*>(m_tilesSec->addChild(std::move(t))));
    }
  }

  // Sliders

  void CpHome::buildSliders() {
    m_slidersSec = addChild(ui::node({}));
    const bool slim = opt<std::string>("panel", "sliderStyle", "thick") == "slim";
    auto out = std::make_unique<cp::Slider>(slim);
    out->setOnMoved([this](float v) {
      if (m_services.audio != nullptr && m_services.audio->defaultSink() != nullptr) m_services.audio->setVolume(v);
    });
    out->setOnIconClicked([this]() {
      if (m_services.audio != nullptr && m_services.audio->defaultSink() != nullptr) {
        m_services.audio->setMuted(!m_services.audio->defaultSink()->muted);
      }
    });
    m_output = static_cast<cp::Slider*>(m_slidersSec->addChild(std::move(out)));

    auto mic = std::make_unique<cp::Slider>(slim);
    mic->setLabel("Microphone");
    mic->setOnMoved([this](float v) {
      if (m_services.audio != nullptr && m_services.audio->defaultSource() != nullptr) m_services.audio->setMicVolume(v);
    });
    mic->setOnIconClicked([this]() {
      if (m_services.audio != nullptr && m_services.audio->defaultSource() != nullptr) {
        m_services.audio->setMicMuted(!m_services.audio->defaultSource()->muted);
      }
    });
    m_mic = static_cast<cp::Slider*>(m_slidersSec->addChild(std::move(mic)));

    // Sets every screen that can be dimmed at once.
    auto bright = std::make_unique<cp::Slider>(slim);
    bright->setLabel("Brightness");
    bright->setOnMoved([this](float v) {
      if (m_services.brightness != nullptr) m_services.brightness->setAllBrightness(v);
    });
    m_brightness = static_cast<cp::Slider*>(m_slidersSec->addChild(std::move(bright)));
  }

  // Weather

  void CpHome::buildWeather() {
    auto card = std::make_unique<cp::Card>(true, 0.07F);
    card->setOnClick([this](const InputArea::PointerData&) {
      if (m_services.weather != nullptr) m_services.weather->requestRefresh();
    });
    m_weather = static_cast<cp::Card*>(addChild(std::move(card)));
    m_wIcon = static_cast<Label*>(m_weather->addChild(cp::icon(0xf0590, 38.0F, cp::accent())));
    m_wTemp = static_cast<Label*>(m_weather->addChild(cp::text("—", 24.0F, true)));
    m_wFeels = static_cast<Label*>(m_weather->addChild(cp::text("", 11.0F, false, cp::dim())));
    m_wDesc = static_cast<Label*>(m_weather->addChild(cp::text("", 11.0F, false, cp::dim())));
    m_wDesc->setMaxLines(1);
    for (auto& d : m_days) {
      d.day = static_cast<Label*>(m_weather->addChild(cp::text("", 10.0F, true, cp::dim())));
      d.icon = static_cast<Label*>(m_weather->addChild(cp::icon(0xf0599, 18.0F)));
      d.range = static_cast<Label*>(m_weather->addChild(cp::text("", 10.0F, false, cp::dim())));
    }
  }

  bool CpHome::syncWeather() {
    if (m_weather == nullptr) return false;
    WeatherService* w = m_services.weather;
    const bool shown = opt<bool>("panel", "showWeather", true) && w != nullptr
        && (w->hasData() || w->loading() || !w->error().empty());
    bool changed = shown != m_weatherShown;
    m_weatherShown = shown;
    m_weather->setVisible(shown);
    if (!shown) return changed;
    const auto set = [&changed](Label* l, const std::string& s) { changed = l->setText(s) || changed; };
    if (w->hasData()) {
      const auto& snap = w->snapshot();
      const auto& cur = snap.current;
      set(m_wIcon, cp::utf8(weatherGlyph(cur.weatherCode, cur.isDay)));
      set(m_wTemp, std::to_string(static_cast<int>(std::lround(w->displayTemperature(cur.temperatureC))))
                       + w->displayTemperatureUnit());
      set(m_wFeels, "feels "
                        + std::to_string(static_cast<int>(std::lround(
                            w->displayTemperature(cur.apparentTemperatureC.value_or(cur.temperatureC)))))
                        + "°");
      std::string desc = WeatherService::shortDescriptionForCode(cur.weatherCode);
      if (!snap.locationName.empty()) desc += (desc.empty() ? "" : "  ·  ") + snap.locationName;
      set(m_wDesc, desc);
      for (std::size_t i = 0; i < m_days.size(); ++i) {
        const bool has = i < snap.forecastDays.size();
        m_days[i].day->setVisible(has);
        m_days[i].icon->setVisible(has);
        m_days[i].range->setVisible(has);
        if (!has) continue;
        const auto& d = snap.forecastDays[i];
        set(m_days[i].day, i == 0 ? std::string("Today") : shortDay(d.dateIso));
        set(m_days[i].icon, cp::utf8(weatherGlyph(d.weatherCode, true)));
        set(m_days[i].range, std::to_string(static_cast<int>(std::lround(w->displayTemperature(d.temperatureMaxC)))) + "° "
                                 + std::to_string(static_cast<int>(std::lround(w->displayTemperature(d.temperatureMinC))))
                                 + "°");
      }
    } else {
      set(m_wTemp, "—");
      set(m_wFeels, "");
      set(m_wDesc, !w->error().empty() ? w->error() : "Loading…");
      for (auto& d : m_days) {
        d.day->setVisible(false);
        d.icon->setVisible(false);
        d.range->setVisible(false);
      }
    }
    return changed;
  }

  // Media

  void CpHome::buildMedia() {
    // Take a fresh position sample when the page opens, since MPRIS doesn't push it.
    if (m_services.mpris != nullptr) {
      DeferredCall::callLater([mpris = m_services.mpris, alive = std::weak_ptr<int>(m_alive)]() {
        if (alive.expired()) return;
        mpris->refreshPlayers();
        PanelManager::instance().refresh();
      });
    }
    m_media = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    m_artBg = static_cast<Box*>(m_media->addChild(ui::box({})));
    m_artBg->setFill(cp::textA(0.08F));
    m_artIcon = static_cast<Label*>(m_media->addChild(cp::icon(0xf075a, 26.0F, cp::dim())));
    m_art = static_cast<Image*>(m_media->addChild(ui::image({.fit = ImageFit::Cover})));
    m_title = static_cast<Label*>(m_media->addChild(cp::text("", 13.0F, true)));
    m_title->setMaxLines(1);
    m_artist = static_cast<Label*>(m_media->addChild(cp::text("", 11.0F, false, cp::dim())));
    m_artist->setMaxLines(1);
    m_progress = static_cast<Box*>(m_media->addChild(ui::box({})));
    m_progress->setFill(cp::textA(0.1F));
    m_progress->setRadius(2.0F);
    m_progressFill = static_cast<Box*>(m_media->addChild(ui::box({})));
    m_progressFill->setFill(cp::accent());
    m_progressFill->setRadius(2.0F);
    // Progress bar, click to seek.
    m_seek = static_cast<InputArea*>(m_media->addChild(ui::inputArea({
        .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
        .onClick =
            [this](const InputArea::PointerData& d) {
              if (m_services.mpris == nullptr) return;
              const auto p = m_services.mpris->activePlayer();
              if (!p || !p->canSeek || p->lengthUs <= 0) return;
              const float frac = std::clamp((d.localX - 6.0F) / std::max(1.0F, m_progress->width()), 0.0F, 1.0F);
              (void)m_services.mpris->setPositionActive(static_cast<int64_t>(static_cast<double>(p->lengthUs) * frac));
              setMediaFrac(frac, false);
            },
    })));

    auto prev = std::make_unique<cp::IconButton>(0xf04ae);
    prev->setOnActivate([this]() {
      if (m_services.mpris != nullptr) (void)m_services.mpris->previousActive();
    });
    m_prev = static_cast<cp::IconButton*>(m_media->addChild(std::move(prev)));
    auto play = std::make_unique<cp::IconButton>(0xf040a, 18.0F, true);
    play->setOnActivate([this]() {
      if (m_services.mpris != nullptr) (void)m_services.mpris->playPauseActive();
    });
    m_play = static_cast<cp::IconButton*>(m_media->addChild(std::move(play)));
    auto next = std::make_unique<cp::IconButton>(0xf04ad);
    next->setOnActivate([this]() {
      if (m_services.mpris != nullptr) (void)m_services.mpris->nextActive();
    });
    m_next = static_cast<cp::IconButton*>(m_media->addChild(std::move(next)));
  }

  void CpHome::setMediaFrac(float frac, bool animate) {
    frac = std::clamp(frac, 0.0F, 1.0F);
    m_mediaFrac = frac;
    AnimationManager* anims = animationManager();
    const float w = m_progress->width();
    if (!animate || anims == nullptr || w <= 0.0F) {
      if (anims != nullptr) anims->cancelForOwner(m_progressFill);
      m_progressFill->setSize(std::round(w * frac), 4.0F);
      return;
    }
    anims->cancelForOwner(m_progressFill);
    const float from = m_progressFill->width();
    const float to = std::round(w * frac);
    anims->animate(0.0F, 1.0F, 900.0F, Easing::Linear,
                   [this, from, to](float t) { m_progressFill->setSize(from + (to - from) * t, 4.0F); }, {}, m_progressFill);
  }

  bool CpHome::syncMedia(Renderer& renderer) {
    if (m_media == nullptr) return false;
    const auto player = m_services.mpris != nullptr ? m_services.mpris->activePlayer() : std::nullopt;
    const bool shown = opt<bool>("panel", "showMedia", true) && player.has_value();
    bool changed = shown != m_mediaShown;
    m_mediaShown = shown;
    m_media->setVisible(shown);
    if (!shown) return changed;

    changed = m_title->setText(player->title.empty() ? std::string("Unknown") : player->title) || changed;
    const std::string artists = mpris::joinArtists(player->artists);
    changed = m_artist->setText(!artists.empty() ? artists : player->identity) || changed;
    m_play->setIcon(player->playbackStatus == "Playing" ? 0xf03e4 : 0xf040a);
    m_seek->setEnabled(player->canSeek);

    const std::int64_t position = m_services.mpris->positionActive().value_or(player->positionUs);
    const float frac = player->lengthUs > 0
        ? std::clamp(static_cast<float>(static_cast<double>(position) / static_cast<double>(player->lengthUs)), 0.0F, 1.0F)
        : 0.0F;
    const std::string key = player->busName + "|" + player->trackId + "|" + player->title;
    setMediaFrac(frac, key == m_trackKey);
    m_trackKey = key;

    // Remote cover art is cached in the background, which then refreshes the panel.
    const std::string url = mpris::effectiveArtUrl(*player);
    if (url != m_artUrl || (!url.empty() && !m_art->hasImage())) {
      const std::string path = mpris::resolveArtworkSource(
          m_services.httpClient, m_pendingArt, url, []() { PanelManager::instance().refresh(); }, m_alive
      );
      if (url.empty() || path.empty()) {
        if (url.empty()) m_art->clear(renderer);
      } else if (m_art->setSourceFile(renderer, path, 144, true, true)) {
        m_artUrl = url;
      }
      if (url.empty()) m_artUrl.clear();
    }
    m_artIcon->setVisible(!m_art->hasImage());
    m_art->setVisible(m_art->hasImage());
    return changed;
  }

  // Stats

  void CpHome::buildStats() {
    m_statsSec = addChild(ui::node({}));
    static constexpr char32_t icons[] = {0xf0ee0, 0xf035b, 0xf08ae, 0xf050f};
    static constexpr const char* labels[] = {"CPU", "RAM", "GPU", "TEMP"};
    for (std::size_t i = 0; i < m_stats.size(); ++i) {
      Stat& s = m_stats[i];
      auto card = std::make_unique<cp::Card>(true, 0.08F);
      card->setOnClick([this](const InputArea::PointerData&) { m_panel.selectTab(ControlPanel::System); });
      s.card = static_cast<cp::Card*>(m_statsSec->addChild(std::move(card)));
      s.icon = static_cast<Label*>(s.card->addChild(cp::icon(icons[i], 14.0F, cp::dim())));
      s.label = static_cast<Label*>(s.card->addChild(cp::text("", 10.0F, true, cp::dim())));
      cp::setSpacedText(*s.label, labels[i], 1.0F);
      s.value = static_cast<Label*>(s.card->addChild(cp::text("", 13.0F, true)));
      s.track = static_cast<Box*>(s.card->addChild(ui::box({})));
      s.track->setFill(cp::textA(0.1F));
      s.track->setRadius(2.0F);
      s.fill = static_cast<Box*>(s.card->addChild(ui::box({})));
      s.fill->setFill(cp::accent());
      s.fill->setRadius(2.0F);
    }
    if (m_services.sysmon != nullptr) {
      // Detailed sensors are only polled while the panel shows them.
      m_services.sysmon->retainCpuTemp();
      m_services.sysmon->retainGpuUsage();
      m_retainedSensors = true;
    }
  }

  bool CpHome::syncStats() {
    if (m_statsSec == nullptr) return false;
    const bool shown = opt<bool>("panel", "showStats", true);
    bool changed = shown != m_statsSec->visible();
    m_statsSec->setVisible(shown);
    if (!shown || m_services.sysmon == nullptr) return changed;
    const SystemStats st = m_services.sysmon->latest();
    const int cpu = static_cast<int>(std::lround(st.cpuUsagePercent));
    const int ram = static_cast<int>(std::lround(st.ramUsagePercent));
    const int gpu = st.gpuUsagePercent ? static_cast<int>(std::lround(*st.gpuUsagePercent)) : 0;
    const int temp = st.cpuTempC ? static_cast<int>(std::lround(*st.cpuTempC)) : 0;
    const int values[] = {cpu, ram, gpu, temp};
    for (std::size_t i = 0; i < m_stats.size(); ++i) {
      Stat& s = m_stats[i];
      const std::string text = std::to_string(values[i]) + (i == 3 ? "°" : "%");
      if (text != s.text) {
        s.text = text;
        s.value->setText(text);
        changed = true;
      }
      if (i == 3) s.fill->setFill(temp >= 85 ? cp::danger() : temp >= 70 ? kusanagi::color("warn") : cp::accent());
      const float frac = static_cast<float>(std::clamp(values[i], 0, 100)) / 100.0F;
      if (frac != s.frac) {
        const float w = s.track->width();
        const float from = s.fill->width();
        s.frac = frac;
        if (AnimationManager* anims = animationManager(); anims != nullptr && w > 0.0F) {
          anims->cancelForOwner(s.fill);
          Box* fill = s.fill;
          anims->animate(0.0F, 1.0F, 600.0F, Easing::EaseOutCubic,
                         [fill, from, to = w * frac](float t) { fill->setSize(from + (to - from) * t, 4.0F); }, {}, fill);
        }
      }
    }
    return changed;
  }

  // Sync and layout

  bool CpHome::sync(Renderer& renderer) {
    bool changed = false;
    for (std::size_t i = 0; i < m_tiles.size(); ++i) {
      const TileState t = tile(m_tileIds[i]);
      changed = m_tiles[i]->setContent(t.icon, t.label, t.sub, t.on) || changed;
    }

    if (m_slidersSec != nullptr) {
      const AudioNode* sink = m_services.audio != nullptr ? m_services.audio->defaultSink() : nullptr;
      const AudioNode* source = m_services.audio != nullptr ? m_services.audio->defaultSource() : nullptr;
      m_output->setIcon(sink == nullptr || sink->muted ? 0xf0581 : 0xf057e);
      changed = m_output->setLabel(sink != nullptr ? audioDeviceLabel(*sink) : "Output") || changed;
      m_output->setValue(sink != nullptr ? sink->volume : 0.0F);
      m_output->setMuted(sink != nullptr && sink->muted);
      m_mic->setIcon(source == nullptr || source->muted ? 0xf036d : 0xf036c);
      m_mic->setValue(source != nullptr ? source->volume : 0.0F);
      m_mic->setMuted(source != nullptr && source->muted);

      double sum = 0.0;
      int n = 0;
      if (m_services.brightness != nullptr) {
        for (const auto& d : m_services.brightness->displays()) {
          if (!d.controllable) continue;
          sum += d.brightness;
          ++n;
        }
      }
      const bool bright = n > 0;
      if (bright != m_brightness->visible()) changed = true;
      m_brightness->setVisible(bright);
      if (bright) {
        const float level = static_cast<float>(sum / n);
        m_brightness->setIcon(level < 0.34F ? 0xf00dd : level < 0.67F ? 0xf00de : 0xf00df);
        m_brightness->setValue(level);
      }
    }
    changed = syncWeather() || changed;
    changed = syncMedia(renderer) || changed;
    changed = syncStats() || changed;
    return changed;
  }

  void CpHome::tick() {
    if (std::ranges::find(m_tileIds, "record") != m_tileIds.end() || std::ranges::find(m_tileIds, "replay") != m_tileIds.end()
        || std::ranges::find(m_tileIds, "clip") != m_tileIds.end()) {
      readRecorder();
    }
  }

  float CpHome::layout(Renderer& renderer, float width) {
    float y = 0.0F;
    bool any = false;
    const auto place = [&](Node* section, float h) {
      if (section == nullptr || !section->visible()) return;
      if (any) y += 12.0F;
      section->setPosition(0.0F, y);
      y += h;
      any = true;
    };

    for (const auto& name : m_order) {
      if (name == "tiles" && m_tilesSec != nullptr) {
        const int columns = std::max(1, opt<int>("panel", "tileColumns", 4));
        const float cell = (width - static_cast<float>(columns - 1) * 8.0F) / static_cast<float>(columns);
        const float th = cp::Tile::heightFor(m_tileStyle);
        for (std::size_t i = 0; i < m_tiles.size(); ++i) {
          const auto col = static_cast<float>(static_cast<int>(i) % columns);
          const auto row = static_cast<float>(static_cast<int>(i) / columns);
          m_tiles[i]->setPosition(std::round(col * (cell + 8.0F)), row * (th + 8.0F));
          m_tiles[i]->setSize(std::round(cell), th);
          m_tiles[i]->layout(renderer);
        }
        const int rows = (static_cast<int>(m_tiles.size()) + columns - 1) / columns;
        const float h = static_cast<float>(rows) * th + static_cast<float>(std::max(0, rows - 1)) * 8.0F;
        m_tilesSec->setSize(width, h);
        place(m_tilesSec, h);
      } else if (name == "sliders" && m_slidersSec != nullptr) {
        float sy = 0.0F;
        for (cp::Slider* s : {m_output, m_mic, m_brightness}) {
          if (!s->visible()) continue;
          if (sy > 0.0F) sy += 12.0F;
          s->setPosition(0.0F, sy);
          s->setSize(width, 36.0F);
          s->layout(renderer);
          sy += 36.0F;
        }
        m_slidersSec->setSize(width, sy);
        place(m_slidersSec, sy);
      } else if (name == "weather" && m_weather != nullptr) {
        const float h = 84.0F;
        m_weather->setSize(width, h);
        // Forecast for today and the next two days, right-aligned.
        float fx = width - 14.0F;
        for (auto it = m_days.rbegin(); it != m_days.rend(); ++it) {
          if (!it->day->visible()) continue;
          it->day->measure(renderer);
          it->icon->measure(renderer);
          it->range->measure(renderer);
          const float cw = std::max({it->day->width(), it->icon->width(), it->range->width()});
          const float ch = it->day->height() + it->icon->height() + it->range->height() + 4.0F;
          const float cx = fx - cw;
          float cy = std::round((h - ch) / 2.0F);
          for (Label* l : {it->day, it->icon, it->range}) {
            l->setPosition(std::round(cx + (cw - l->width()) / 2.0F), cy);
            cy += l->height() + 2.0F;
          }
          fx = cx - 14.0F;
        }
        m_wIcon->measure(renderer);
        m_wIcon->setPosition(16.0F, std::round((h - m_wIcon->height()) / 2.0F));
        const float tx = 16.0F + m_wIcon->width() + 14.0F;
        m_wTemp->measure(renderer);
        m_wFeels->measure(renderer);
        m_wDesc->setMaxWidth(std::max(80.0F, fx - tx));
        m_wDesc->measure(renderer);
        const float rowH = std::max(m_wTemp->height(), m_wFeels->height() + 11.0F);
        const float colH = rowH + 1.0F + m_wDesc->height();
        const float ty = std::round((h - colH) / 2.0F);
        m_wTemp->setPosition(tx, ty);
        m_wFeels->setPosition(tx + m_wTemp->width() + 8.0F, ty + 11.0F);
        m_wDesc->setPosition(tx, ty + rowH + 1.0F);
        place(m_weather, h);
      } else if (name == "media" && m_media != nullptr) {
        const float h = 96.0F;
        m_media->setSize(width, h);
        const float artR = std::max(6.0F, kusanagi::radius() - 10.0F);
        m_artBg->setPosition(12.0F, 12.0F);
        m_artBg->setSize(72.0F, 72.0F);
        m_artBg->setRadius(artR);
        m_art->setPosition(12.0F, 12.0F);
        m_art->setSize(72.0F, 72.0F);
        m_art->setRadius(artR);
        m_artIcon->measure(renderer);
        cp::centerIn(*m_artIcon, 12.0F, 12.0F, 72.0F, 72.0F);
        const float cx = width - 10.0F - (34.0F + 2.0F + 40.0F + 2.0F + 34.0F);
        m_prev->setPosition(cx, std::round((h - 34.0F) / 2.0F));
        m_prev->setSize(34.0F, 34.0F);
        m_play->setPosition(cx + 36.0F, std::round((h - 40.0F) / 2.0F));
        m_play->setSize(40.0F, 40.0F);
        m_next->setPosition(cx + 78.0F, std::round((h - 34.0F) / 2.0F));
        m_next->setSize(34.0F, 34.0F);
        for (cp::IconButton* b : {m_prev, m_play, m_next}) b->layout(renderer);
        const float tx = 12.0F + 72.0F + 14.0F;
        const float tw = std::max(1.0F, cx - 8.0F - tx);
        m_title->setMaxWidth(tw);
        m_artist->setMaxWidth(tw);
        m_title->measure(renderer);
        m_artist->measure(renderer);
        const float colH = m_title->height() + 2.0F + m_artist->height() + 2.0F + 8.0F + 2.0F + 4.0F;
        float ty = std::round((h - colH) / 2.0F);
        m_title->setPosition(tx, ty);
        ty += m_title->height() + 2.0F;
        m_artist->setPosition(tx, ty);
        ty += m_artist->height() + 2.0F + 8.0F + 2.0F;
        m_progress->setPosition(tx, ty);
        m_progress->setSize(tw, 4.0F);
        m_progressFill->setPosition(tx, ty);
        m_progressFill->setSize(std::round(tw * m_mediaFrac), 4.0F);
        m_seek->setPosition(tx - 6.0F, ty - 6.0F);
        m_seek->setSize(tw + 12.0F, 16.0F);
        place(m_media, h);
      } else if (name == "stats" && m_statsSec != nullptr) {
        const float cell = (width - 3.0F * 8.0F) / 4.0F;
        const float h = 54.0F;
        for (std::size_t i = 0; i < m_stats.size(); ++i) {
          Stat& s = m_stats[i];
          s.card->setPosition(std::round(static_cast<float>(i) * (cell + 8.0F)), 0.0F);
          const float cw = std::round(cell);
          s.card->setSize(cw, h);
          s.icon->measure(renderer);
          s.icon->setPosition(12.0F, 10.0F);
          s.label->measure(renderer);
          s.label->setPosition(32.0F, 9.0F);
          s.value->measure(renderer);
          s.value->setPosition(cw - 12.0F - s.value->width(), 7.0F);
          s.track->setPosition(12.0F, h - 12.0F - 4.0F);
          s.track->setSize(cw - 24.0F, 4.0F);
          s.fill->setPosition(12.0F, h - 12.0F - 4.0F);
          if (animationManager() == nullptr || !animationManager()->hasActive()) s.fill->setSize((cw - 24.0F) * s.frac, 4.0F);
        }
        m_statsSec->setSize(width, h);
        place(m_statsSec, h);
      }
    }
    return y;
  }

} // namespace kusanagi
