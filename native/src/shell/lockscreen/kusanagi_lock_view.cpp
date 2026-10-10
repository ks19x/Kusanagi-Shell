#include "shell/lockscreen/kusanagi_lock_view.h"

#include "cursor-shape-v1-client-protocol.h"
#include "dbus/mpris/mpris_service.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "render/scene/node.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"
#include "ui/palette.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <linux/input-event-codes.h>
#include <numbers>

namespace kusanagi {

  namespace {

    // Nerd Font codepoints
    constexpr char32_t kLockIcon = 0xF033E;    // nf-md-lock
    constexpr char32_t kBusyIcon = 0xF0450;    // nf-md-loading
    constexpr char32_t kMusicIcon = 0xF075A;   // nf-md-music_note
    constexpr char32_t kPrevIcon = 0xF04AE;    // nf-md-skip_previous
    constexpr char32_t kNextIcon = 0xF04AD;    // nf-md-skip_next
    constexpr char32_t kPauseIcon = 0xF03E4;   // nf-md-pause
    constexpr char32_t kPlayIcon = 0xF040A;    // nf-md-play

    constexpr float kPillWidth = 320.0F;
    constexpr float kPillHeight = 48.0F;
    constexpr float kAvatarSize = 84.0F;
    constexpr float kLoginSpacing = 16.0F;
    constexpr float kErrorHeight = 16.0F;
    constexpr float kDotSize = 8.0F;
    constexpr float kDotSpacing = 7.0F;
    constexpr std::size_t kMaxDots = 24;
    constexpr const char* kBannerText = "TEST MODE — Esc unlocks, and it unlocks by itself after 30 s";

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

    // Easing curves
    float outBack(float t, float s) {
      t -= 1.0F;
      return t * t * ((s + 1.0F) * t + s) + 1.0F;
    }
    float outCubic(float t) { return 1.0F - std::pow(1.0F - t, 3.0F); }
    float outQuint(float t) { return 1.0F - std::pow(1.0F - t, 5.0F); }

    // Formats a time with Qt-style patterns ("HH:mm", "dddd, d MMMM", "h:mm AP"), with English names.
    std::string qtFormat(const std::string& fmt, const std::tm& t) {
      static const char* days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
      static const char* months[] = {"January", "February", "March",     "April",   "May",      "June",
                                     "July",    "August",   "September", "October", "November", "December"};
      const bool twelve = fmt.find("AP") != std::string::npos || fmt.find("ap") != std::string::npos;
      auto pad = [](int v) { return (v < 10 ? "0" : "") + std::to_string(v); };
      std::string out;
      for (std::size_t i = 0; i < fmt.size();) {
        const char c = fmt[i];
        if (c == '\'') {
          const auto end = fmt.find('\'', i + 1);
          out += fmt.substr(i + 1, end == std::string::npos ? std::string::npos : end - i - 1);
          i = end == std::string::npos ? fmt.size() : end + 1;
          continue;
        }
        std::size_t n = 1;
        while (i + n < fmt.size() && fmt[i + n] == c) {
          ++n;
        }
        const int hour12 = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
        switch (c) {
        case 'H':
          out += n >= 2 ? pad(t.tm_hour) : std::to_string(t.tm_hour);
          break;
        case 'h': {
          const int h = twelve ? hour12 : t.tm_hour;
          out += n >= 2 ? pad(h) : std::to_string(h);
          break;
        }
        case 'm':
          out += n >= 2 ? pad(t.tm_min) : std::to_string(t.tm_min);
          break;
        case 's':
          out += n >= 2 ? pad(t.tm_sec) : std::to_string(t.tm_sec);
          break;
        case 'd':
          if (n >= 4) {
            out += days[t.tm_wday];
          } else if (n == 3) {
            out += std::string(days[t.tm_wday]).substr(0, 3);
          } else {
            out += n == 2 ? pad(t.tm_mday) : std::to_string(t.tm_mday);
          }
          break;
        case 'M':
          if (n >= 4) {
            out += months[t.tm_mon];
          } else if (n == 3) {
            out += std::string(months[t.tm_mon]).substr(0, 3);
          } else {
            out += n == 2 ? pad(t.tm_mon + 1) : std::to_string(t.tm_mon + 1);
          }
          break;
        case 'y':
          out += n >= 4 ? std::to_string(t.tm_year + 1900) : pad(t.tm_year % 100);
          break;
        case 'A':
        case 'a':
          if (n == 1 && i + 1 < fmt.size() && (fmt[i + 1] == 'P' || fmt[i + 1] == 'p')) {
            const bool upper = c == 'A';
            out += t.tm_hour < 12 ? (upper ? "AM" : "am") : (upper ? "PM" : "pm");
            n = 2;
          } else {
            out.append(n, c);
          }
          break;
        default:
          out.append(n, c);
          break;
        }
        i += n;
      }
      return out;
    }

    std::string readFirstLine(const char* path) {
      std::ifstream in(path);
      std::string line;
      std::getline(in, line);
      return line;
    }

    // Pango letter_spacing is in 1/1024 pt (at 96 dpi); the setting is in px.
    std::string spaced(const std::string& text, float letterSpacingPx) {
      if (letterSpacingPx == 0.0F) {
        return text;
      }
      const int units = static_cast<int>(std::lround(letterSpacingPx * 0.75F * 1024.0F));
      return "<span letter_spacing=\"" + std::to_string(units) + "\">" + text + "</span>";
    }

  } // namespace

  bool lockLookEnabled() {
    const auto& s = settings();
    return s.is_object() && !s.empty();
  }

  LockView::LockView(
      Node& parent, AnimationManager& animations, std::function<void()> requestUpdate,
      std::function<void()> requestLayout, std::function<void()> requestRedraw
  )
      : m_parent(parent), m_animations(animations), m_requestUpdate(std::move(requestUpdate)),
        m_requestLayout(std::move(requestLayout)), m_requestRedraw(std::move(requestRedraw)) {
    m_host = readFirstLine("/proc/sys/kernel/hostname");
    m_kernel = readFirstLine("/proc/sys/kernel/osrelease");
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
      m_avatarPath = std::string(home) + "/.face";
    }
    readStyle();
    build();
    setContentProgress(0.0F, 0.96F);

    // The clock ticks every second for "ss" formats and the terminal header; checking that often is cheap.
    m_clockTimer.startRepeating(std::chrono::seconds(1), [this]() {
      if (m_requestUpdate) {
        m_requestUpdate();
      }
    });
    // The terminal style's blinking block cursor.
    m_cursorTimer.startRepeating(std::chrono::milliseconds(530), [this]() {
      if (m_style != "terminal" || m_termCursor == nullptr) {
        return;
      }
      m_cursorOn = !m_cursorOn;
      m_termCursor->setOpacity(m_cursorOn ? 1.0F : 0.0F);
      if (m_requestRedraw) {
        m_requestRedraw();
      }
    });
  }

  LockView::~LockView() {
    *m_alive = false;
    m_clockTimer.stop();
    m_cursorTimer.stop();
    m_animations.cancel(m_shakeAnim);
    m_animations.cancel(m_spinAnim);
    m_animations.cancel(m_contentAnim);
  }

  float LockView::lineHeight(Renderer& renderer, float size, FontWeight weight) const {
    const TextMetrics m = renderer.measureFont(size, weight, m_font);
    return std::round(m.bottom - m.top);
  }

  float LockView::dim() const { return std::clamp(static_cast<float>(opt<double>("lock", "dim", 0.0)), 0.0F, 1.0F); }

  void LockView::readStyle() {
    m_style = opt<std::string>("lock", "style", "center");
    m_clockFormat = opt<std::string>("lock", "clock", "HH:mm");
    if (m_clockFormat.empty()) {
      m_clockFormat = "HH:mm";
    }
    m_font = font();
    m_avatarOn = opt<bool>("lock", "avatar", true);
    m_mediaOn = opt<bool>("lock", "media", true);
  }

  void LockView::build() {
    auto text = [this](float size, const ColorSpec& color, bool bold = false) {
      return ui::label({
          .fontSize = size,
          .fontWeight = bold ? FontWeight::Bold : FontWeight::Normal,
          .fontFamily = m_font,
          .color = color,
          .maxLines = 1,
          .baselineMode = LabelBaselineMode::TextFixedHeight,
      });
    };

    auto content = ui::node({.zIndex = 3});
    m_content = m_parent.addChild(std::move(content));

    // Backgrounds of the card and split styles
    m_splitPanel = static_cast<Box*>(m_content->addChild(ui::box({.fill = color("panel/0.62")})));
    m_splitLine = static_cast<Box*>(m_content->addChild(ui::box({.fill = color("accent/0.35")})));
    m_card = static_cast<Box*>(m_content->addChild(ui::box({
        .fill = color("panel/0.55"),
        .border = color("text/0.1"),
        .borderWidth = 1.0F,
    })));

    // Clock
    m_time = static_cast<Label*>(m_content->addChild(text(128.0F, color("text"), true)));
    m_minutes = static_cast<Label*>(m_content->addChild(text(200.0F, color("text"), true)));
    m_date = static_cast<Label*>(m_content->addChild(text(20.0F, color("text/0.75"))));
    m_time->setUseMarkup(true);
    m_minutes->setUseMarkup(true);

    // Terminal style: a login prompt
    m_term = m_content->addChild(ui::node({}));
    m_termHeader = static_cast<Label*>(m_term->addChild(text(15.0F, color("text/0.55"))));
    m_termLogin = static_cast<Label*>(m_term->addChild(text(20.0F, color("text"))));
    m_termPassword = static_cast<Label*>(m_term->addChild(text(20.0F, color("text"))));
    m_termCursor = static_cast<Box*>(m_term->addChild(ui::box({.fill = color("accent"), .width = 11.0F, .height = 22.0F}))
    );
    m_termBusy = static_cast<Label*>(m_term->addChild(text(20.0F, color("dim"))));
    m_termError = static_cast<Label*>(m_term->addChild(text(20.0F, color("danger"))));
    m_termBusy->setText("checking…");
    m_termError->setText("Login incorrect");

    // Avatar, name and password
    m_login = m_content->addChild(ui::node({}));
    m_avatarBg = static_cast<Box*>(m_login->addChild(ui::box({
        .fill = color("text/0.1"),
        .radius = kAvatarSize / 2.0F,
        .width = kAvatarSize,
        .height = kAvatarSize,
    })));
    m_avatar = static_cast<Image*>(m_login->addChild(ui::image({
        .fit = ImageFit::Cover,
        .radius = kAvatarSize / 2.0F,
        .width = kAvatarSize,
        .height = kAvatarSize,
        .visible = false,
    })));
    m_avatarRing = static_cast<Box*>(m_login->addChild(ui::box({
        .fill = clearColorSpec(),
        .border = color("accent/0.8"),
        .borderWidth = 2.0F,
        .radius = kAvatarSize / 2.0F,
        .width = kAvatarSize,
        .height = kAvatarSize,
    })));
    m_name = static_cast<Label*>(m_login->addChild(text(16.0F, color("text"), true)));

    m_pill = m_login->addChild(ui::node({.width = kPillWidth, .height = kPillHeight}));
    m_pillBg = static_cast<Box*>(m_pill->addChild(ui::box({
        .fill = color("panel/0.6"),
        .border = color("text/0.12"),
        .borderWidth = 1.0F,
        .radius = kPillHeight / 2.0F,
        .width = kPillWidth,
        .height = kPillHeight,
    })));
    m_pillIcon = static_cast<Label*>(m_pill->addChild(text(16.0F, color("dim"))));
    m_dotRow = m_pill->addChild(ui::node({}));
    m_placeholder = static_cast<Label*>(m_pill->addChild(text(13.0F, color("dim"))));
    m_error = static_cast<Label*>(m_login->addChild(text(12.0F, color("danger"))));

    // Now playing
    m_media = m_content->addChild(ui::node({.visible = false}));
    m_mediaBg = static_cast<Box*>(m_media->addChild(ui::box({
        .fill = color("panel/0.55"),
        .border = color("text/0.08"),
        .borderWidth = 1.0F,
        .radius = 26.0F,
    })));
    m_mediaIcon = static_cast<Label*>(m_media->addChild(text(16.0F, color("accent"))));
    m_mediaIcon->setText(utf8(kMusicIcon));
    m_mediaText = static_cast<Label*>(m_media->addChild(text(12.0F, color("text"))));
    m_mediaText->setMaxWidth(260.0F);
    m_mediaText->setEllipsize(TextEllipsize::End);

    auto makeButton = [this, &text](MediaButton& button, char32_t icon, bool filled, std::function<void()> onClick) {
      button.filled = filled;
      button.bg = static_cast<Box*>(m_media->addChild(ui::box({.radius = 14.0F, .width = 28.0F, .height = 28.0F})));
      button.icon = static_cast<Label*>(m_media->addChild(text(16.0F, color("dim"))));
      button.icon->setText(utf8(icon));
      button.area = static_cast<InputArea*>(m_media->addChild(ui::inputArea({
          .acceptedButtons = InputArea::buttonMask({BTN_LEFT}),
          .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
          .width = 28.0F,
          .height = 28.0F,
          .onEnter =
              [this, &button](const InputArea::PointerData&) {
                button.hovered = true;
                applyButtonLook(button);
                if (m_requestRedraw) {
                  m_requestRedraw();
                }
              },
          .onLeave =
              [this, &button]() {
                button.hovered = false;
                applyButtonLook(button);
                if (m_requestRedraw) {
                  m_requestRedraw();
                }
              },
          .onClick = [onClick = std::move(onClick)](const InputArea::PointerData&) { onClick(); },
      })));
      applyButtonLook(button);
    };
    makeButton(m_prev, kPrevIcon, false, [this]() {
      if (m_mpris != nullptr) {
        (void)m_mpris->previousActive();
      }
    });
    makeButton(m_play, kPlayIcon, true, [this]() {
      if (m_mpris != nullptr) {
        (void)m_mpris->playPauseActive();
      }
    });
    makeButton(m_next, kNextIcon, false, [this]() {
      if (m_mpris != nullptr) {
        (void)m_mpris->nextActive();
      }
    });

    // Test-mode banner
    m_banner = m_content->addChild(ui::node({.visible = false}));
    m_bannerBg = static_cast<Box*>(m_banner->addChild(ui::box({.fill = color("#e8be62/0.9"), .radius = 17.0F})));
    m_bannerText = static_cast<Label*>(m_banner->addChild(text(12.0F, color("#1a1408"), true)));
    m_bannerText->setText(kBannerText);

    syncIcon();
  }

  void LockView::applyButtonLook(MediaButton& button) {
    if (button.bg == nullptr || button.icon == nullptr) {
      return;
    }
    button.bg->setFill(button.filled ? color("accent") : color(button.hovered ? "text/0.1" : "text/0"));
    button.icon->setColor(button.filled ? color("panel") : color(button.hovered ? "text" : "dim"));
  }

  void LockView::setAvatarPath(std::string path) {
    if (path == m_avatarPath) {
      return;
    }
    m_avatarPath = std::move(path);
    m_avatarLoaded = false;
    if (m_requestUpdate) {
      m_requestUpdate();
    }
  }

  void LockView::setHidden(bool hidden) {
    m_hidden = hidden;
    if (m_content != nullptr) {
      m_content->setVisible(!hidden);
    }
  }

  void LockView::setPrompt(const Prompt& prompt) {
    const bool busyChanged = prompt.busy != m_prompt.busy;
    const bool failed = prompt.failures > m_seenFailures;
    m_seenFailures = prompt.failures;
    m_prompt = prompt;
    if (busyChanged) {
      syncIcon();
    }
    if (failed) {
      startShake();
    }
  }

  void LockView::syncIcon() {
    if (m_pillIcon == nullptr) {
      return;
    }
    m_pillIcon->setText(utf8(m_prompt.busy ? kBusyIcon : kLockIcon));
    if (m_prompt.busy) {
      startSpin();
    } else {
      m_animations.cancel(m_spinAnim);
      m_spinAnim = 0;
      m_spin = 0.0F;
      m_pillIcon->setRotation(0.0F);
    }
  }

  void LockView::startSpin() {
    m_animations.cancel(m_spinAnim);
    std::weak_ptr<bool> alive = m_alive;
    // One turn every 900 ms, looping while busy.
    m_spinAnim = m_animations.animateTimer(
        0.0F, 1.0F, 900.0F, Easing::Linear,
        [this](float t) {
          m_spin = t;
          m_pillIcon->setRotation(t * 2.0F * std::numbers::pi_v<float>);
          if (m_requestRedraw) {
            m_requestRedraw();
          }
        },
        [this, alive]() {
          if (alive.expired() || !*alive.lock()) {
            return;
          }
          m_spinAnim = 0;
          if (m_prompt.busy) {
            startSpin();
          }
        },
        m_pillIcon
    );
  }

  void LockView::startShake() {
    // Linear segments to each offset in turn.
    struct Key {
      float to;
      float ms;
    };
    static constexpr Key keys[] = {{-12.0F, 50.0F}, {10.0F, 70.0F}, {-6.0F, 70.0F}, {3.0F, 60.0F}, {0.0F, 60.0F}};
    float total = 0.0F;
    for (const Key& k : keys) {
      total += k.ms;
    }
    m_animations.cancel(m_shakeAnim);
    m_shakeAnim = m_animations.animateTimer(
        0.0F, total, total, Easing::Linear,
        [this](float elapsed) {
          float from = 0.0F;
          float at = 0.0F;
          float value = 0.0F;
          for (const Key& k : keys) {
            if (elapsed <= at + k.ms) {
              const float t = k.ms > 0.0F ? (elapsed - at) / k.ms : 1.0F;
              value = from + (k.to - from) * t;
              break;
            }
            at += k.ms;
            from = k.to;
            value = k.to;
          }
          m_shake = value;
          if (m_pill != nullptr) {
            m_pill->setPosition(std::round(m_pillBaseX + m_shake), m_pill->y());
          }
          if (m_requestRedraw) {
            m_requestRedraw();
          }
        },
        [this]() {
          m_shakeAnim = 0;
          m_shake = 0.0F;
        },
        m_pill
    );
  }

  void LockView::syncDots() {
    if (m_dotRow == nullptr) {
      return;
    }
    const std::size_t want = std::min(m_prompt.passwordLength, kMaxDots);
    while (m_dots.size() > want) {
      Box* dot = m_dots.back();
      m_dots.pop_back();
      (void)m_dotRow->removeChild(dot);
    }
    const float overshoot = bounce(3.0F);
    while (m_dots.size() < want) {
      auto dot = ui::box({
          .fill = color("text"),
          .radius = kDotSize / 2.0F,
          .width = kDotSize,
          .height = kDotSize,
      });
      Box* raw = static_cast<Box*>(m_dotRow->addChild(std::move(dot)));
      raw->setPosition(static_cast<float>(m_dots.size()) * (kDotSize + kDotSpacing), 0.0F);
      raw->setTransformOrigin(kDotSize / 2.0F, kDotSize / 2.0F);
      raw->setScale(0.0F);
      m_dots.push_back(raw);
      // One dot per character, popping in with an overshoot.
      m_animations.animate(
          0.0F, 1.0F, 180.0F, Easing::Linear,
          [this, raw, overshoot](float t) {
            raw->setScale(std::max(0.0F, outBack(t, overshoot)));
            if (m_requestRedraw) {
              m_requestRedraw();
            }
          },
          {}, raw
      );
    }
  }

  void LockView::sync(Renderer& renderer) {
    readStyle();
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);

    const bool stacked = m_style == "stacked";
    const bool minimal = m_style == "minimal";
    const float letterSpacing = minimal ? 0.0F : -2.0F;
    const std::string time = stacked
        ? qtFormat(m_clockFormat.find("AP") != std::string::npos ? "hh" : "HH", tm)
        : qtFormat(m_clockFormat, tm);
    m_time->setText(spaced(time, letterSpacing));
    m_minutes->setText(spaced(qtFormat("mm", tm), -2.0F));
    m_date->setText(qtFormat("dddd, d MMMM", tm));
    m_termHeader->setText(m_host + " · " + m_kernel + "   " + qtFormat("ddd d MMM  HH:mm:ss", tm));

    // The avatar (~/.face by default), decoded once.
    if (m_avatarOn && !m_avatarLoaded && !m_avatarPath.empty()) {
      m_avatarLoaded = true;
      std::error_code ec;
      if (std::filesystem::is_regular_file(m_avatarPath, ec)) {
        const bool ok = m_avatar->setSourceFile(renderer, m_avatarPath, static_cast<int>(kAvatarSize * 2.0F), true, true);
        m_avatar->setVisible(ok);
      } else if (m_avatar->hasImage()) {
        m_avatar->clear(renderer); // the newly picked user has no picture
        m_avatar->setVisible(false);
      }
    }

    // Now playing: the active player.
    std::string mediaLine;
    bool playing = false;
    if (m_mpris != nullptr) {
      if (const auto player = m_mpris->activePlayer(); player.has_value()) {
        const std::string artist = joinedArtists(player->artists);
        mediaLine = player->title + (artist.empty() ? "" : "  ·  " + artist);
        playing = player->playbackStatus == "Playing";
        m_mediaVisible = true;
      } else {
        m_mediaVisible = false;
      }
    } else {
      m_mediaVisible = false;
    }
    m_mediaText->setText(mediaLine);
    m_play.icon->setText(utf8(playing ? kPauseIcon : kPlayIcon));
  }

  void LockView::setContentProgress(float opacity, float scale) {
    if (m_content == nullptr) {
      return;
    }
    m_content->setOpacity(opacity);
    m_content->setScale(scale);
  }

  void LockView::playEnter() {
    m_animations.cancel(m_contentAnim);
    // Fade in quickly while the scale settles from 0.96 to 1.
    m_contentAnim = m_animations.animate(
        0.0F, 520.0F, 520.0F, Easing::Linear,
        [this](float ms) {
          setContentProgress(outCubic(std::min(1.0F, ms / 380.0F)), 0.96F + 0.04F * outQuint(ms / 520.0F));
          if (m_requestRedraw) {
            m_requestRedraw();
          }
        },
        [this]() {
          m_contentAnim = 0;
          setContentProgress(1.0F, 1.0F);
        },
        m_content
    );
  }

  void LockView::playExit(std::function<void()> done) {
    m_animations.cancel(m_contentAnim);
    const float startOpacity = m_content != nullptr ? m_content->opacity() : 1.0F;
    // Fade out while growing towards 1.06. The unlock itself waits for this to finish.
    m_contentAnim = m_animations.animate(
        0.0F, 420.0F, 420.0F, Easing::Linear,
        [this, startOpacity](float ms) {
          setContentProgress(
              startOpacity * (1.0F - outCubic(std::min(1.0F, ms / 380.0F))), 1.0F + 0.06F * outQuint(ms / 520.0F)
          );
          if (m_requestRedraw) {
            m_requestRedraw();
          }
        },
        [this, done = std::move(done)]() {
          m_contentAnim = 0;
          setContentProgress(0.0F, 1.06F);
          if (done) {
            done();
          }
        },
        m_content
    );
  }

  void LockView::layout(Renderer& renderer, float width, float height) {
    m_width = width;
    m_height = height;
    if (m_content == nullptr) {
      return;
    }
    m_content->setPosition(0.0F, 0.0F);
    m_content->setSize(width, height);
    m_content->setTransformOrigin(width / 2.0F, height / 2.0F);

    const bool term = m_style == "terminal";
    const bool split = m_style == "split";
    const float panelW = std::round(width * 0.38F);

    m_splitPanel->setVisible(split);
    m_splitLine->setVisible(split);
    if (split) {
      m_splitPanel->setPosition(0.0F, 0.0F);
      m_splitPanel->setSize(panelW, height);
      m_splitLine->setPosition(panelW - 1.0F, 0.0F);
      m_splitLine->setSize(1.0F, height);
    }

    m_time->setVisible(!term);
    m_date->setVisible(!term);
    m_minutes->setVisible(!term && m_style == "stacked");
    m_login->setVisible(!term);
    m_term->setVisible(term);
    m_card->setVisible(m_style == "card");

    if (term) {
      layoutTerminal(renderer, width, height);
    } else {
      layoutClock(renderer, width, height);
      layoutLogin(renderer, width, height);
    }
    layoutMedia(renderer, width, height);
    layoutBanner(renderer, width);
  }

  void LockView::layoutClock(Renderer& renderer, float width, float height) {
    const bool card = m_style == "card";
    const bool split = m_style == "split";
    const bool minimal = m_style == "minimal";
    const bool stacked = m_style == "stacked";
    const bool leftAligned = split || minimal || stacked;

    m_time->setFontSize(stacked ? 200.0F : card ? 88.0F : minimal ? 56.0F : split ? 112.0F : 128.0F);
    m_time->setColor(stacked ? color("accent") : color("text"));
    m_date->setFontSize(minimal ? 15.0F : 20.0F);
    m_time->measure(renderer);
    m_minutes->measure(renderer);
    m_date->measure(renderer);

    // The stacked style overlaps hours and minutes and pushes the date down.
    const float spacing = stacked ? -40.0F : 4.0F;
    const float datePad = stacked ? 44.0F : 0.0F;
    float colW = std::max(m_time->width(), m_date->width());
    float colH = m_time->height() + spacing + datePad + m_date->height();
    if (stacked) {
      colW = std::max(colW, m_minutes->width());
      colH += m_minutes->height() + spacing;
    }

    // The card wraps the clock and the login column.
    const float loginH = [&]() {
      float h = 0.0F;
      int items = 0;
      if (m_avatarOn && !minimal) {
        h += kAvatarSize;
        ++items;
      }
      if (!minimal) {
        h += lineHeight(renderer, 16.0F, FontWeight::Bold);
        ++items;
      }
      // The error line only takes room while it says something.
      h += kPillHeight;
      ++items;
      if (!m_prompt.error.empty()) {
        h += kErrorHeight;
        ++items;
      }
      return h + kLoginSpacing * static_cast<float>(items - 1);
    }();
    const float cardW = 440.0F;
    const float cardH = colH + loginH + 92.0F;
    const float cardX = std::round((width - cardW) / 2.0F);
    const float cardY = std::round((height - cardH) / 2.0F);
    if (card) {
      const float r = std::max(18.0F, radius() + 6.0F);
      m_card->setPosition(cardX, cardY);
      m_card->setSize(cardW, cardH);
      m_card->setRadius(r);
    }

    const float x = split ? 80.0F
        : minimal         ? 60.0F
        : stacked         ? std::round(width * 0.1F)
                          : std::round((width - colW) / 2.0F);
    const float y = card ? cardY + 30.0F
        : minimal        ? 54.0F
        : stacked        ? std::round((height - colH) / 2.0F)
                         : std::round(height * (split ? 0.2F : 0.18F));

    auto place = [&](Label* label, float top) {
      const float lx = leftAligned ? x : x + std::round((colW - label->width()) / 2.0F);
      label->setPosition(lx, top);
    };
    float cursor = y;
    place(m_time, cursor);
    cursor += m_time->height() + spacing;
    if (stacked) {
      place(m_minutes, cursor);
      cursor += m_minutes->height() + spacing;
    }
    place(m_date, cursor + datePad);

    m_clockBottom = y + colH;
  }

  void LockView::layoutLogin(Renderer& renderer, float width, float height) {
    const bool card = m_style == "card";
    const bool split = m_style == "split";
    const bool minimal = m_style == "minimal";
    const bool stacked = m_style == "stacked";
    const float panelW = std::round(width * 0.38F);

    const bool showAvatar = m_avatarOn && !minimal;
    m_avatarBg->setVisible(showAvatar);
    m_avatarRing->setVisible(showAvatar);
    m_avatar->setVisible(showAvatar && m_avatar->hasImage());
    m_name->setVisible(!minimal);

    m_name->setText(m_prompt.who);
    m_name->measure(renderer);
    m_error->setText(m_prompt.error);
    m_error->measure(renderer);

    // the pill
    const bool errored = !m_prompt.error.empty();
    m_pillBg->setBorder(
        errored ? color("danger") : m_prompt.focused ? color("accent/0.7") : color("text/0.12"), 1.0F
    );
    m_pillIcon->measure(renderer);
    const float iconW = m_pillIcon->width();
    const float iconH = m_pillIcon->height();
    m_pillIcon->setPosition(18.0F, std::round((kPillHeight - iconH) / 2.0F));
    m_pillIcon->setTransformOrigin(iconW / 2.0F, iconH / 2.0F);

    syncDots();
    const std::size_t dots = m_dots.size();
    const float dotsW = dots > 0 ? static_cast<float>(dots) * kDotSize + static_cast<float>(dots - 1) * kDotSpacing : 0.0F;
    m_dotRow->setPosition(std::round((kPillWidth - dotsW) / 2.0F), std::round((kPillHeight - kDotSize) / 2.0F));
    m_dotRow->setSize(dotsW, kDotSize);

    m_placeholder->setVisible(m_prompt.passwordLength == 0);
    m_placeholder->setText(m_prompt.busy ? "Checking…" : "Password");
    m_placeholder->measure(renderer);
    m_placeholder->setPosition(
        std::round((kPillWidth - m_placeholder->width()) / 2.0F), std::round((kPillHeight - m_placeholder->height()) / 2.0F)
    );

    // Avatar, name, pill and error stacked in a column as wide as its widest item.
    float colW = std::max({kPillWidth, m_name->width(), m_error->width()});
    if (showAvatar) {
      colW = std::max(colW, kAvatarSize);
    }
    // The error line only takes room while it says something.
    const bool showError = !m_prompt.error.empty();
    m_error->setVisible(showError);
    float colH = kPillHeight + (showError ? kErrorHeight + kLoginSpacing : 0.0F);
    if (showAvatar) {
      colH += kAvatarSize + kLoginSpacing;
    }
    const float nameH = lineHeight(renderer, 16.0F, FontWeight::Bold);
    if (!minimal) {
      colH += nameH + kLoginSpacing;
    }

    const float x = split ? std::round((panelW - colW) / 2.0F)
        : stacked         ? std::round(width * 0.62F)
                          : std::round((width - colW) / 2.0F);
    const float y = card ? m_clockBottom + 28.0F
        : minimal || stacked ? std::round((height - colH) / 2.0F)
                             : std::round(height * (split ? 0.6F : 0.58F));
    m_login->setPosition(x, y);
    m_login->setSize(colW, colH);

    float cursor = 0.0F;
    auto centerX = [colW](float w) { return std::round((colW - w) / 2.0F); };
    if (showAvatar) {
      m_avatarBg->setPosition(centerX(kAvatarSize), cursor);
      m_avatar->setPosition(centerX(kAvatarSize), cursor);
      m_avatarRing->setPosition(centerX(kAvatarSize), cursor);
      cursor += kAvatarSize + kLoginSpacing;
    }
    if (!minimal) {
      m_name->setPosition(centerX(m_name->width()), cursor + std::round((nameH - m_name->height()) / 2.0F));
      cursor += nameH + kLoginSpacing;
    }
    m_pillBaseX = centerX(kPillWidth);
    m_pill->setPosition(std::round(m_pillBaseX + m_shake), cursor);
    m_pill->setSize(kPillWidth, kPillHeight);
    cursor += kPillHeight + kLoginSpacing;
    // The error text is vertically centred in its fixed-height line.
    m_error->setPosition(centerX(m_error->width()), cursor + std::round((kErrorHeight - m_error->height()) / 2.0F));
  }

  void LockView::layoutTerminal(Renderer& renderer, float width, float height) {
    const float x = std::round(width * 0.16F);
    const float y = std::round(height * 0.32F);
    const float spacing = 6.0F;
    m_term->setPosition(x, y);
    m_term->setSize(width - x, height - y);

    const std::size_t stars = std::min<std::size_t>(m_prompt.passwordLength, 32);
    m_termLogin->setText(m_host + " login: " + m_prompt.who);
    m_termPassword->setText("Password: " + std::string(stars, '*'));
    m_termBusy->setVisible(m_prompt.busy);
    m_termError->setVisible(!m_prompt.error.empty());

    float cursor = 0.0F;
    m_termHeader->measure(renderer);
    m_termHeader->setPosition(0.0F, cursor);
    cursor += m_termHeader->height() + spacing;
    // A blank line at 10 px.
    cursor += lineHeight(renderer, 10.0F, FontWeight::Normal) + spacing;
    m_termLogin->measure(renderer);
    m_termLogin->setPosition(0.0F, cursor);
    cursor += m_termLogin->height() + spacing;
    m_termPassword->measure(renderer);
    m_termPassword->setPosition(0.0F, cursor);
    // The block cursor sits right after the stars, vertically centred on the line.
    m_termCursor->setPosition(m_termPassword->width(), cursor + std::round((m_termPassword->height() - 22.0F) / 2.0F));
    cursor += std::max(m_termPassword->height(), 22.0F) + spacing;
    if (m_prompt.busy) {
      m_termBusy->measure(renderer);
      m_termBusy->setPosition(0.0F, cursor);
      cursor += m_termBusy->height() + spacing;
    }
    if (!m_prompt.error.empty()) {
      m_termError->measure(renderer);
      m_termError->setPosition(0.0F, cursor);
    }
  }

  void LockView::layoutMedia(Renderer& renderer, float width, float height) {
    const bool show = m_mediaOn && m_mediaVisible && m_style != "terminal";
    m_media->setVisible(show);
    if (!show) {
      return;
    }
    const float spacing = 12.0F;
    m_mediaIcon->measure(renderer);
    m_mediaText->measure(renderer);
    for (MediaButton* b : {&m_prev, &m_play, &m_next}) {
      b->icon->measure(renderer);
      applyButtonLook(*b);
    }
    const float rowW = m_mediaIcon->width() + spacing + m_mediaText->width() + spacing + 3.0F * 28.0F + 2.0F * spacing;
    const float w = std::min(420.0F, rowW + 32.0F);
    const float h = 52.0F;
    const float panelW = std::round(width * 0.38F);
    const float x = m_style == "split" ? std::round((panelW - w) / 2.0F) : std::round((width - w) / 2.0F);
    const float y = height - 48.0F - h;
    m_media->setPosition(x, y);
    m_media->setSize(w, h);
    m_mediaBg->setPosition(0.0F, 0.0F);
    m_mediaBg->setSize(w, h);

    float cursor = std::round((w - rowW) / 2.0F);
    auto vcenter = [h](float itemH) { return std::round((h - itemH) / 2.0F); };
    m_mediaIcon->setPosition(cursor, vcenter(m_mediaIcon->height()));
    cursor += m_mediaIcon->width() + spacing;
    m_mediaText->setPosition(cursor, vcenter(m_mediaText->height()));
    cursor += m_mediaText->width() + spacing;
    for (MediaButton* b : {&m_prev, &m_play, &m_next}) {
      const float by = vcenter(28.0F);
      b->bg->setPosition(cursor, by);
      b->area->setPosition(cursor, by);
      b->area->setSize(28.0F, 28.0F);
      b->icon->setPosition(cursor + std::round((28.0F - b->icon->width()) / 2.0F), by + std::round((28.0F - b->icon->height()) / 2.0F));
      cursor += 28.0F + spacing;
    }
  }

  void LockView::layoutBanner(Renderer& renderer, float width) {
    m_banner->setVisible(m_prompt.testMode);
    if (!m_prompt.testMode) {
      return;
    }
    m_bannerText->measure(renderer);
    const float w = m_bannerText->width() + 32.0F;
    const float h = 34.0F;
    m_banner->setPosition(std::round((width - w) / 2.0F), 24.0F);
    m_banner->setSize(w, h);
    m_bannerBg->setPosition(0.0F, 0.0F);
    m_bannerBg->setSize(w, h);
    m_bannerText->setPosition(16.0F, std::round((h - m_bannerText->height()) / 2.0F));
  }

} // namespace kusanagi
