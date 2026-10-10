#include "shell/notification/kusanagi_notification_card.h"

#include "core/deferred_call.h"
#include "core/log.h"
#include "net/uri.h"
#include "notification/notification_display_name.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/core/render_styles.h"
#include "render/core/renderer.h"
#include "render/core/texture_manager.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/notification/kusanagi_styled_text.h"
#include "system/icon_resolver.h"
#include "ui/builders.h"
#include "ui/palette.h"
#include "util/string_utils.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <glib.h>
#include <linux/input-event-codes.h>
#include <unistd.h>

namespace kusanagi {

  namespace {

    constexpr Logger kLog("notification");

    constexpr float kShadowOffsetY = 6.0F;
    constexpr float kShadowBlur = 22.0F;
    constexpr float kShadowAlpha = 0.38F;
    constexpr float kDragThreshold = 8.0F;
    constexpr float kChipHeight = 28.0F;
    constexpr const char* kCloseGlyph = "\U000F0156"; // nf-md-close

    // Easing curves; outBack takes an explicit overshoot.
    float outBack(float t, float s) {
      t -= 1.0F;
      return t * t * ((s + 1.0F) * t + s) + 1.0F;
    }

    float inCubic(float t) { return t * t * t; }

    std::string upper(const std::string& s) {
      gchar* up = g_utf8_strup(s.c_str(), static_cast<gssize>(s.size()));
      std::string out = up != nullptr ? up : s;
      g_free(up);
      return out;
    }

    std::string escape(const std::string& s) {
      gchar* e = g_markup_escape_text(s.c_str(), static_cast<gssize>(s.size()));
      std::string out = e != nullptr ? e : s;
      g_free(e);
      return out;
    }

    // The body's first line with any tags stripped.
    std::string firstLine(const std::string& body) {
      std::string out;
      bool inTag = false;
      for (const char c : body) {
        if (c == '<') {
          inTag = true;
        } else if (c == '>' && inTag) {
          inTag = false;
        } else if (!inTag) {
          if (c == '\n') break;
          out += c;
        }
      }
      return out;
    }

    // The app's buttons, without the invisible "default" one.
    std::vector<std::pair<std::string, std::string>> chipActions(const std::vector<std::string>& actions) {
      std::vector<std::pair<std::string, std::string>> out;
      for (std::size_t i = 0; i + 1 < actions.size(); i += 2) {
        if (actions[i].empty() || actions[i] == "default") continue;
        out.emplace_back(actions[i], StringUtils::isBlank(actions[i + 1]) ? actions[i] : actions[i + 1]);
      }
      return out;
    }

    // A one-line label with its cap band centred in its box, for the minimal pill.
    std::unique_ptr<Label> text(const std::string& s, float size, ColorSpec color, bool bold = false) {
      return ui::label({
          .text = s,
          .fontSize = size,
          .fontWeight = bold ? FontWeight::Bold : FontWeight::Normal,
          .fontFamily = font(),
          .color = color,
          .maxLines = 1,
          .baselineMode = LabelBaselineMode::TextFixedHeight,
      });
    }

    // Matches the classic look: the first baseline sits round(ascent) below the top and lines are
    // round(ascent) + round(descent) apart. Pango rounds both up (a 13 px line is 18 px, not 17), which
    // would change the cards' rhythm and height.
    struct LineMetrics {
      float ascent = 0.0F;
      float height = 0.0F;
      float pangoHeight = 0.0F;
    };

    LineMetrics lineMetrics(Renderer& renderer, float size, FontWeight weight) {
      const auto fm = renderer.measureFont(size, weight, font());
      const float ascent = std::round(-fm.top);
      return {ascent, ascent + std::round(fm.bottom), std::ceil(-fm.top) + std::ceil(fm.bottom)};
    }

    // A text block at (x, top) on those metrics: wraps to maxWidth, at most `lines` lines (elided).
    struct TextBlock {
      std::unique_ptr<Label> label;
      float height = 0.0F;
    };

    TextBlock textBlock(
        Renderer& renderer, const std::string& plain, float size, ColorSpec color, bool bold, int lines, float maxWidth,
        float x, float top, const std::string& spanAttrs = {}
    ) {
      const FontWeight weight = bold ? FontWeight::Bold : FontWeight::Normal;
      const LineMetrics q = lineMetrics(renderer, size, weight);
      // line_height (absolute, 1/1024 device px) brings Pango's line pitch down to the rounded one.
      const std::string markup = "<span line_height=\""
          + std::to_string(std::lround(q.height * 1024.0F * renderer.renderScale())) + "\"" + spanAttrs
          + ">" + escape(plain) + "</span>";
      auto label = ui::label({
          .text = markup,
          .fontSize = size,
          .fontWeight = weight,
          .fontFamily = font(),
          .color = color,
          .maxLines = lines,
          .baselineMode = LabelBaselineMode::FontLine,
      });
      label->setUseMarkup(true);
      if (maxWidth > 0.0F) label->setMaxWidth(maxWidth);
      label->measure(renderer);
      const auto m = renderer.measureText(markup, size, weight, maxWidth, lines, TextAlign::Start, font(), TextEllipsize::End, true);
      label->setPosition(x, top + q.ascent - label->baselineOffset());
      return {std::move(label), static_cast<float>(std::max(1, m.lineCount)) * q.height};
    }

    // A styled text block (text colour at `alpha`) at (x, top), laid out like textBlock: same first
    // baseline, line pitch per run size, at most `lines` lines over all its paragraphs (the last one
    // elided when it wraps on). One label per paragraph; nothing when there is no text.
    struct StyledBlock {
      std::unique_ptr<Node> node;
      float height = 0.0F;
    };

    StyledBlock styledBlock(
        Renderer& renderer, const std::string& styled, float size, float alpha, int lines, float maxWidth, float x,
        float top
    ) {
      const float scale = renderer.renderScale();
      const LineMetrics q = lineMetrics(renderer, size, FontWeight::Normal);
      const StyledMarkup sm = styledTextToPango(styled, alpha, [&renderer, size, scale](float s, bool bold) {
        const LineMetrics l = lineMetrics(renderer, size * s, bold ? FontWeight::Bold : FontWeight::Normal);
        return std::lround(l.height * 1024.0F * scale);
      });
      bool any = false;
      for (const auto& p : sm.paragraphs) any = any || !p.markup.empty();
      if (!any) return {};
      // Coloured runs keep their own colour at full alpha: the labels are opaque and the rest carries `alpha`.
      const ColorSpec color = colorSpecFromRole(ColorRole::OnSurface, sm.colored ? 1.0F : alpha);
      auto block = ui::node({});
      block->setPosition(x, top);
      float y = 0.0F;
      int left = lines;
      for (const auto& p : sm.paragraphs) {
        if (left <= 0) break;
        if (p.markup.empty()) { // an empty line
          y += q.height;
          --left;
          continue;
        }
        auto label = ui::label({
            .text = p.markup,
            .fontSize = size,
            .fontFamily = font(),
            .color = color,
            .maxLines = left,
            .baselineMode = LabelBaselineMode::FontLine,
        });
        label->setUseMarkup(true);
        if (maxWidth > 0.0F) label->setMaxWidth(maxWidth);
        label->measure(renderer);
        const auto m = renderer.measureText(
            p.markup, size, FontWeight::Normal, maxWidth, left, TextAlign::Start, font(), TextEllipsize::End, true
        );
        const int count = std::max(1, m.lineCount);
        if (!p.scaled) {
          label->setPosition(0.0F, y + q.ascent - label->baselineOffset());
          y += static_cast<float>(count) * q.height;
        } else {
          // Other sizes (<font size>, <hN>): the runs already set their own line pitch, and the block's
          // first baseline sits its tallest run's ascent below the top.
          label->setPosition(0.0F, y - m.top - label->baselineOffset());
          y += std::round(m.bottom - m.top);
        }
        left -= count;
        block->addChild(std::move(label));
      }
      return {std::move(block), y};
    }

  } // namespace

  NotificationCardStyle NotificationCardStyle::fromSettings(bool popup) {
    NotificationCardStyle s;
    s.style = opt<std::string>("notifications", "style", "comfortable");
    s.images = opt<bool>("notifications", "images", true);
    s.progress = opt<bool>("notifications", "progress", true);
    s.popup = popup;
    return s;
  }

  std::string notificationAge(const Notification& n) {
    if (!n.receivedWallClock.has_value()) return "";
    const auto s = std::chrono::duration_cast<std::chrono::seconds>(WallClock::now() - *n.receivedWallClock).count();
    if (s < 60) return "now";
    if (s < 3600) return std::to_string(s / 60) + "m";
    if (s < 86400) return std::to_string(s / 3600) + "h";
    return std::to_string(s / 86400) + "d";
  }

  NotificationCardData notificationCardData(const Notification& n, IconResolver& icons) {
    NotificationCardData d{
        .id = n.id,
        .appName = notificationDisplayAppName(n),
        .summary = n.summary,
        .body = n.body,
        .bodyMarkup = n.bodyMarkup,
        .actions = n.actions,
        .image = n.imageData.has_value() ? &*n.imageData : nullptr,
        .critical = n.urgency == Urgency::Critical,
        .age = notificationAge(n),
    };
    // The app's own icon (app_icon) goes next to its name, picture or not. Without one, use `icon`, but not
    // when there is a picture, since `icon` may then be the image-path picture.
    const bool sentAppIcon = !n.appIcon.empty();
    if (!sentAppIcon && (!n.icon.has_value() || n.icon->empty())) return d;
    const std::string& icon = sentAppIcon ? n.appIcon : *n.icon;
    constexpr std::string_view kGlyphPrefix = "kusanagi-glyph:";
    if (icon.starts_with(kGlyphPrefix)) {
      d.appGlyph = icon.substr(kGlyphPrefix.size());
      return d;
    }
    if (icon.starts_with('/') || icon.starts_with("file:") || icon.starts_with("~/")) {
      std::string path = uri::normalizeFileUrl(icon);
      if (path.starts_with("~/")) {
        const char* home = std::getenv("HOME");
        path = std::string(home != nullptr ? home : "") + path.substr(1);
      }
      if (path.empty() || access(path.c_str(), R_OK) != 0) return d;
      // With a picture the notification server folds image-path into the icon; that is the picture, not the app.
      if (!sentAppIcon && n.imageData.has_value()) return d;
      d.appIconPath = path;
      return d;
    }
    if (uri::isRemoteUrl(icon)) return d;
    d.appIconPath = icons.resolve(icon, 28);
    return d;
  }

  NotificationCard::NotificationCard(
      Renderer& renderer, AnimationManager* animations, NotificationCardData data, NotificationCardStyle style,
      NotificationCardCallbacks callbacks
  )
      : m_animations(animations), m_data(std::move(data)), m_style(std::move(style)),
        m_callbacks(std::move(callbacks)) {
    if (m_animations != nullptr) setAnimationManager(m_animations);
    build(renderer);
  }

  NotificationCard::~NotificationCard() {
    *m_alive = false;
    if (m_animations != nullptr) {
      m_animations->cancel(m_swipeAnim);
      m_animations->cancel(m_fadeAnim);
      m_animations->cancel(m_closeAnim);
    }
  }

  float NotificationCard::surfaceRadius() const {
    const bool minimal = m_style.popup && m_style.style == "minimal";
    return minimal ? m_height / 2.0F : std::max(8.0F, radius() - 2.0F);
  }

  void NotificationCard::build(Renderer& renderer) {
    const bool minimal = m_style.popup && m_style.style == "minimal";
    const float w = m_style.width;

    // The part that moves on swipe and fly-off; the shadow follows it.
    auto surface = ui::node({.width = w, .clipChildren = true});
    m_surface = surface.get();
    m_background = static_cast<Box*>(m_surface->addChild(ui::box({})));

    if (minimal) {
      m_height = 46.0F;
      buildMinimal(renderer);
    } else {
      buildFull(renderer);
    }
    setSize(w, m_height);
    m_surface->setSize(w, m_height);
    m_background->setSize(w, m_height);
    setTransformOrigin(w / 2.0F, m_height / 2.0F);

    const float r = surfaceRadius();
    m_background->setRadius(r);
    m_background->setFill(
        m_style.popup ? colorSpecFromRole(
                            ColorRole::Surface, std::max(0.88F, static_cast<float>(opt<double>("panel", "opacity", 0.95)))
                        )
                      : colorSpecFromRole(ColorRole::OnSurface, 0.045F)
    );

    // Popups show the time left as a hairline along the bottom, paused while hovered.
    if (m_style.popup && !m_data.critical && m_style.progress) {
      m_hairlineX = minimal ? m_height / 2.0F : r;
      m_hairlineWidth = std::max(0.0F, w - 2.0F * m_hairlineX);
      auto hairline = ui::box({
          .fill = colorSpecFromRole(ColorRole::Primary, 0.8F),
          .radius = 1.0F,
          .width = m_hairlineWidth,
          .height = 2.0F,
      });
      hairline->setPosition(m_hairlineX, m_height - 2.0F);
      m_hairline = static_cast<Box*>(m_surface->addChild(std::move(hairline)));
    }

    if (m_style.popup && shadows()) {
      RoundedRectStyle shadow{
          .fill = rgba(0.0F, 0.0F, 0.0F, kShadowAlpha),
          .radius = Radii{r, r, r, r},
          .softness = kShadowBlur * 0.5F,
          .outerShadow = true,
          .shadowCutoutOffsetX = 0.0F,
          .shadowCutoutOffsetY = kShadowOffsetY,
      };
      auto shadowBox = ui::box({.width = w, .height = m_height});
      shadowBox->setStyle(shadow);
      shadowBox->setPosition(0.0F, kShadowOffsetY);
      m_shadow = static_cast<Box*>(addChild(std::move(shadowBox)));
    }
    addChild(std::move(surface));
    applyHover();

    // Input: a click runs the default action; a right click or a swipe right dismisses.
    setAcceptedButtons(InputArea::buttonMask({BTN_LEFT, BTN_RIGHT}));
    setOnEnter([this](const PointerData&) { hoverRef(1); });
    setOnLeave([this]() { hoverRef(-1); });
    setOnPress([this](const PointerData& p) {
      if (m_leaving) return;
      if (p.pressed) {
        m_leftDown = p.button == BTN_LEFT;
        m_pressX = p.sceneX;
        m_dragging = false;
        m_dragged = false;
        return;
      }
      if (p.button != BTN_LEFT) return;
      m_leftDown = false;
      if (m_dragging) {
        m_dragging = false;
        if (m_swipe > m_style.width * 0.3F) {
          dismiss();
        } else {
          springBack();
        }
      }
    });
    setOnMotion([this](const PointerData& p) {
      if (!m_leftDown || !pressed() || m_leaving) return;
      float sx = 0.0F;
      float sy = 0.0F;
      Node::mapToScene(this, p.localX, p.localY, sx, sy);
      const float dx = sx - m_pressX;
      if (!m_dragging && std::abs(dx) > kDragThreshold) {
        m_dragging = true;
        m_dragged = true;
        if (m_animations != nullptr) m_animations->cancel(m_swipeAnim);
        m_swipeAnim = 0;
      }
      if (m_dragging) setSwipe(std::max(-20.0F, dx));
    });
    setOnCancel([this]() {
      m_leftDown = false;
      if (m_dragging) {
        m_dragging = false;
        springBack();
      }
    });
    setOnClick([this](const PointerData& p) {
      if (m_dragged || m_leaving) return;
      if (p.button == BTN_RIGHT) {
        dismiss();
      } else if (p.button == BTN_LEFT && m_callbacks.onActivate) {
        m_callbacks.onActivate();
      }
    });
  }

  void NotificationCard::buildMinimal(Renderer& renderer) {
    // Icon, summary and the first line of the body in one pill.
    const float rowW = m_style.width - 32.0F;
    float x = 16.0F;
    const float h = m_height;

    const bool picture = m_data.image != nullptr || !m_data.imagePath.empty();
    if (!m_data.appIconPath.empty()) {
      auto img = ui::image({.fit = ImageFit::Contain, .width = 20.0F, .height = 20.0F});
      if (img->setSourceFile(renderer, m_data.appIconPath, 40)) {
        img->setPosition(x, std::round((h - 20.0F) / 2.0F));
        m_surface->addChild(std::move(img));
        x += 20.0F + 10.0F;
      }
    } else if (!m_data.appGlyph.empty()) {
      auto g = ui::glyph({.glyph = m_data.appGlyph, .glyphSize = 18.0F, .color = colorSpecFromRole(ColorRole::OnSurface)});
      g->measure(renderer);
      g->setPosition(x + std::round((20.0F - g->width()) / 2.0F), std::round((h - g->height()) / 2.0F));
      m_surface->addChild(std::move(g));
      x += 20.0F + 10.0F;
    } else if (m_style.images && picture) {
      auto img = ui::image({.fit = ImageFit::Cover, .radius = 4.0F, .width = 20.0F, .height = 20.0F});
      bool ok = false;
      if (m_data.image != nullptr) {
        const auto& d = *m_data.image;
        ok = d.width > 0
            && d.height > 0
            && img->setSourceRaw(
                renderer, d.data.data(), d.data.size(), d.width, d.height, d.rowStride,
                d.channels == 3 ? PixmapFormat::RGB : PixmapFormat::RGBA, true
            );
      } else {
        ok = img->setSourceFile(renderer, m_data.imagePath, 40);
      }
      if (ok) {
        img->setPosition(x, std::round((h - 20.0F) / 2.0F));
        m_surface->addChild(std::move(img));
        x += 20.0F + 10.0F;
      }
    }

    const std::string sumText = !m_data.summary.empty() ? m_data.summary : m_data.appName;
    auto sum = text(
        sumText, 12.0F, m_data.critical ? colorSpecFromRole(ColorRole::Error) : colorSpecFromRole(ColorRole::OnSurface),
        true
    );
    sum->setMaxWidth(rowW * 0.6F);
    sum->measure(renderer);
    sum->setPosition(x, std::round((h - sum->height()) / 2.0F));
    const float sumW = sum->width();
    x += sumW + 10.0F;
    m_surface->addChild(std::move(sum));

    const float bodyW = rowW - sumW - 40.0F;
    const std::string line = m_data.bodyMarkup.empty() ? firstLine(m_data.body) : styledFirstLine(m_data.bodyMarkup);
    if (bodyW > 4.0F && !line.empty()) {
      auto body = text(line, 12.0F, colorSpecFromRole(ColorRole::OnSurfaceVariant));
      body->setMaxWidth(bodyW);
      body->measure(renderer);
      body->setPosition(x, std::round((h - body->height()) / 2.0F));
      m_surface->addChild(std::move(body));
    }
  }

  void NotificationCard::buildFull(Renderer& renderer) {
    const bool compact = m_style.style == "compact";
    const bool accent = m_style.style == "accent";
    const float pad = compact ? 10.0F : 14.0F;
    const float stripe = accent ? 8.0F : 0.0F;
    const float r = std::max(8.0F, radius() - 2.0F);
    const float bodyX = pad + stripe;
    const float bodyW = m_style.width - 2.0F * pad - stripe;

    // A big picture (album art, avatar, ...) when the app sent one.
    float colX = bodyX;
    float colW = bodyW;
    float imageH = 0.0F;
    if (m_style.images && (m_data.image != nullptr || !m_data.imagePath.empty())) {
      const float size = compact ? 36.0F : 48.0F;
      const float imageRadius = std::max(6.0F, r - 6.0F);
      auto img = ui::image({.fit = ImageFit::Cover, .radius = imageRadius, .width = size, .height = size});
      bool ok = false;
      if (m_data.image != nullptr) {
        const auto& d = *m_data.image;
        const bool valid = d.width > 0
            && d.height > 0
            && d.bitsPerSample == 8
            && ((d.channels == 4 && d.hasAlpha) || (d.channels == 3 && !d.hasAlpha));
        ok = valid
            && img->setSourceRaw(
                renderer, d.data.data(), d.data.size(), d.width, d.height, d.rowStride,
                d.channels == 3 ? PixmapFormat::RGB : PixmapFormat::RGBA, true
            );
      } else {
        ok = img->setSourceFile(renderer, m_data.imagePath, 96);
      }
      if (ok) {
        auto frame = ui::box({
            .fill = colorSpecFromRole(ColorRole::OnSurface, 0.06F),
            .radius = imageRadius,
            .width = size,
            .height = size,
        });
        frame->setPosition(bodyX, pad);
        img->setPosition(bodyX, pad);
        m_surface->addChild(std::move(frame));
        m_surface->addChild(std::move(img));
        colX = bodyX + size + 12.0F;
        colW = bodyW - (compact ? 48.0F : 60.0F);
        imageH = size;
      } else {
        kLog.debug("kusanagi card: no picture for #{}", m_data.id);
      }
    }

    float y = pad;

    // Header row: app, age and the close button.
    {
      constexpr float rowH = 16.0F;
      float x = colX;
      if (!m_data.appIconPath.empty()) {
        auto icon = ui::image({.fit = ImageFit::Contain, .width = 14.0F, .height = 14.0F});
        if (icon->setSourceFile(renderer, m_data.appIconPath, 28)) {
          icon->setPosition(x, y + 1.0F);
          m_surface->addChild(std::move(icon));
          x += 14.0F + 6.0F;
        }
      } else if (!m_data.appGlyph.empty()) {
        auto g = ui::glyph({
            .glyph = m_data.appGlyph,
            .glyphSize = 13.0F,
            .color = m_data.critical ? colorSpecFromRole(ColorRole::Error) : colorSpecFromRole(ColorRole::Primary),
        });
        g->measure(renderer);
        g->setPosition(x + std::round((14.0F - g->width()) / 2.0F), y + std::round((rowH - g->height()) / 2.0F));
        m_surface->addChild(std::move(g));
        x += 14.0F + 6.0F;
      }

      const std::string app = m_data.appName.empty() ? "Notification" : m_data.appName;
      // Centred in the 16 px line; an odd leftover pixel goes below the text.
      const float textTop = y + std::floor((rowH - lineMetrics(renderer, 10.0F, FontWeight::Bold).height) / 2.0F);
      // 1 px letter spacing (Pango counts 1/1024 of a device pixel).
      const int spacing = static_cast<int>(std::lround(1024.0F * renderer.renderScale()));
      auto name = textBlock(
          renderer, upper(app), 10.0F,
          m_data.critical ? colorSpecFromRole(ColorRole::Error) : colorSpecFromRole(ColorRole::Primary), true, 1,
          colW - 60.0F, x, textTop, " letter_spacing=\"" + std::to_string(spacing) + "\""
      );
      x += name.label->width() + 1.0F + 6.0F; // the letter spacing counts after the last letter too
      m_surface->addChild(std::move(name.label));

      auto age = ui::label({
          .text = m_data.age.empty() ? "" : "·  " + m_data.age,
          .fontSize = 10.0F,
          .fontFamily = font(),
          .color = colorSpecFromRole(ColorRole::OnSurfaceVariant),
          .maxLines = 1,
          .baselineMode = LabelBaselineMode::FontLine,
      });
      age->measure(renderer);
      age->setPosition(x, textTop + lineMetrics(renderer, 10.0F, FontWeight::Normal).ascent - age->baselineOffset());
      m_age = static_cast<Label*>(m_surface->addChild(std::move(age)));

      // Close button, shown on hover.
      auto close = ui::inputArea({
          .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
          .width = 22.0F,
          .height = 22.0F,
          .opacity = 0.0F,
          .onEnter =
              [this](const InputArea::PointerData&) {
                hoverRef(1);
                if (m_closeBg != nullptr) m_closeBg->setFill(colorSpecFromRole(ColorRole::OnSurface, 0.1F));
                if (m_closeGlyph != nullptr) m_closeGlyph->setColor(colorSpecFromRole(ColorRole::OnSurface));
              },
          .onLeave =
              [this]() {
                hoverRef(-1);
                if (m_closeBg != nullptr) m_closeBg->setFill(clearColorSpec());
                if (m_closeGlyph != nullptr) m_closeGlyph->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
              },
          .onClick = [this](const InputArea::PointerData&) { dismiss(); },
      });
      m_closeBg = static_cast<Box*>(
          close->addChild(ui::box({.fill = clearColorSpec(), .radius = 11.0F, .width = 22.0F, .height = 22.0F}))
      );
      auto glyph = text(kCloseGlyph, 13.0F, colorSpecFromRole(ColorRole::OnSurfaceVariant));
      glyph->measure(renderer);
      glyph->setPosition(std::round((22.0F - glyph->width()) / 2.0F), std::round((22.0F - glyph->height()) / 2.0F));
      m_closeGlyph = static_cast<Label*>(close->addChild(std::move(glyph)));
      close->setPosition(colX + colW - 22.0F, y + (rowH - 22.0F) / 2.0F);
      m_close = m_surface->addChild(std::move(close));
      y += rowH + 3.0F;
    }

    auto summary = textBlock(renderer, m_data.summary, 13.0F, colorSpecFromRole(ColorRole::OnSurface), true, 2, colW, colX, y);
    y += summary.height;
    m_surface->addChild(std::move(summary.label));

    // The body: styled text as the app sent it (bold, italic, links, <br>, ...), else the plain text.
    const bool styled = !m_data.bodyMarkup.empty();
    if (styled ? !StringUtils::isBlank(m_data.bodyMarkup) : !StringUtils::isBlank(m_data.body)) {
      const int lines = compact ? (m_style.popup ? 1 : 2) : (m_style.popup ? 3 : 6);
      if (styled) {
        auto body = styledBlock(renderer, m_data.bodyMarkup, 12.0F, 0.72F, lines, colW, colX, y + 3.0F);
        if (body.node != nullptr) {
          y += 3.0F + body.height;
          m_surface->addChild(std::move(body.node));
        }
      } else {
        auto body = textBlock(
            renderer, StringUtils::trimLeadingBlankLines(m_data.body), 12.0F,
            colorSpecFromRole(ColorRole::OnSurface, 0.72F), false, lines, colW, colX, y + 3.0F
        );
        y += 3.0F + body.height;
        m_surface->addChild(std::move(body.label));
      }
    }

    // The app's buttons, or the inline reply field once "Reply" was picked.
    const auto chips = chipActions(m_data.actions);
    if (m_data.replyMode) {
      y += 3.0F + 6.0F;
      std::string placeholder;
      for (const auto& [key, label] : chips) {
        if (key == "inline-reply") placeholder = label;
      }
      auto input = ui::input({
          .out = &m_replyInput,
          .placeholder = placeholder.empty() ? "Reply" : placeholder,
          .fontSize = 11.0F,
          .controlHeight = kChipHeight,
          .horizontalPadding = 12.0F,
          .frameVisible = true,
          .width = colW,
          .height = kChipHeight,
          .onSubmit =
              [this](const std::string& t) {
                if (m_callbacks.onReply && !StringUtils::isBlank(t)) m_callbacks.onReply(t);
              },
          .configure =
              [this](Input& in) {
                in.inputArea()->setOnFocusGain([this]() {
                  if (m_callbacks.onReplyFocusChanged) m_callbacks.onReplyFocusChanged(true);
                });
                in.inputArea()->setOnFocusLoss([this]() {
                  if (m_callbacks.onReplyFocusChanged) m_callbacks.onReplyFocusChanged(false);
                });
                in.inputArea()->setOnEnter([this](const InputArea::PointerData&) { hoverRef(1); });
                in.inputArea()->setOnLeave([this]() { hoverRef(-1); });
              },
      });
      input->setPosition(colX, y);
      input->layout(renderer);
      y += kChipHeight;
      m_surface->addChild(std::move(input));
    } else if (!chips.empty()) {
      // Chips wrap onto new rows as needed.
      y += 3.0F + 6.0F;
      float x = colX;
      float rowY = y;
      const LineMetrics chipLine = lineMetrics(renderer, 11.0F, FontWeight::Normal);
      for (const auto& [key, label] : chips) {
        auto caption = textBlock(
            renderer, label, 11.0F, colorSpecFromRole(ColorRole::OnSurfaceVariant), false, 1, 0.0F, 0.0F,
            std::floor((kChipHeight - chipLine.height) / 2.0F)
        );
        const float labelW = caption.label->width();
        const float cw = std::ceil(labelW) + 24.0F;
        if (x > colX && x + cw > colX + colW) {
          x = colX;
          rowY += kChipHeight + 6.0F;
        }
        caption.label->setPosition(std::round((cw - labelW) / 2.0F), caption.label->y());

        auto chip = ui::inputArea({
            .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
            .width = cw,
            .height = kChipHeight,
        });
        auto bgBox = ui::box({
            .fill = colorSpecFromRole(ColorRole::OnSurface, 0.05F),
            .border = colorSpecFromRole(ColorRole::OnSurface, 0.06F),
            .borderWidth = 1.0F,
            .radius = kChipHeight / 2.0F,
            .width = cw,
            .height = kChipHeight,
        });
        Box* bg = bgBox.get();
        chip->addChild(std::move(bgBox));
        chip->addChild(std::move(caption.label));
        chip->setOnEnter([this, bg](const InputArea::PointerData&) {
          hoverRef(1);
          bg->setFill(colorSpecFromRole(ColorRole::OnSurface, 0.09F));
        });
        chip->setOnLeave([this, bg]() {
          hoverRef(-1);
          bg->setFill(colorSpecFromRole(ColorRole::OnSurface, 0.05F));
        });
        chip->setOnClick([this, key](const InputArea::PointerData&) {
          if (m_leaving) return;
          if (key == "inline-reply") {
            if (m_callbacks.onReplyRequested) m_callbacks.onReplyRequested();
            return;
          }
          if (m_callbacks.onAction) m_callbacks.onAction(key);
        });
        chip->setPosition(x, rowY);
        m_surface->addChild(std::move(chip));
        x += cw + 6.0F;
      }
      y = rowY + kChipHeight;
    }

    m_height = std::round(std::max(y, pad + imageH) + pad);

    // A red edge when critical; the accent style puts an accent edge on every card.
    if (m_data.critical || accent) {
      const float sw = accent ? 4.0F : 3.0F;
      const float sh = m_height - (accent ? 16.0F : 20.0F);
      auto edge = ui::box({
          .fill = m_data.critical ? colorSpecFromRole(ColorRole::Error) : colorSpecFromRole(ColorRole::Primary),
          .radius = sw / 2.0F,
          .width = sw,
          .height = sh,
      });
      edge->setPosition(accent ? 8.0F : 6.0F, std::round((m_height - sh) / 2.0F));
      m_surface->insertChildAt(1, std::move(edge));
    }
  }

  void NotificationCard::setRemaining(float remaining) {
    if (m_hairline == nullptr) return;
    const float w = std::round(m_hairlineWidth * std::clamp(remaining, 0.0F, 1.0F));
    m_hairline->setVisible(w >= 1.0F);
    m_hairline->setSize(std::max(1.0F, w), 2.0F);
  }

  void NotificationCard::setAge(const std::string& age) {
    if (m_age == nullptr || m_data.age == age) return;
    m_data.age = age;
    m_age->setText(age.empty() ? "" : "·  " + age);
  }

  void NotificationCard::setSwipe(float x) {
    m_swipe = x;
    const float opacity = 1.0F - std::max(0.0F, x) / (m_style.width * 1.2F);
    m_surface->setPosition(x, 0.0F);
    m_surface->setOpacity(opacity);
    if (m_shadow != nullptr) {
      m_shadow->setPosition(x, kShadowOffsetY);
      m_shadow->setOpacity(opacity);
    }
  }

  // Springs back when a swipe isn't far enough.
  void NotificationCard::springBack() {
    if (m_animations == nullptr) {
      setSwipe(0.0F);
      return;
    }
    const float from = m_swipe;
    const float overshoot = bounce(1.2F);
    m_animations->cancel(m_swipeAnim);
    m_swipeAnim = m_animations->animate(
        0.0F, 1.0F, 320.0F, Easing::Linear,
        [this, from, overshoot](float t) { setSwipe(from * (1.0F - outBack(t, overshoot))); },
        [this]() { m_swipeAnim = 0; }, this
    );
    markPaintDirty();
  }

  // A swipe, the close button, a right click or the countdown flies the card off to the right.
  void NotificationCard::dismiss() {
    if (m_leaving) return;
    m_leaving = true;
    m_dragging = false;
    if (m_animations == nullptr) {
      if (m_callbacks.onDismissed) m_callbacks.onDismissed();
      return;
    }
    m_animations->cancel(m_swipeAnim);
    m_animations->cancel(m_fadeAnim);
    const float from = m_swipe;
    const float to = m_style.width + 40.0F;
    const float fromOpacity = m_surface->opacity();
    m_swipeAnim = m_animations->animate(
        0.0F, 1.0F, 260.0F, Easing::Linear,
        [this, from, to](float t) {
          const float x = from + (to - from) * inCubic(t);
          m_swipe = x;
          m_surface->setPosition(x, 0.0F);
          if (m_shadow != nullptr) m_shadow->setPosition(x, kShadowOffsetY);
        },
        [this, alive = std::weak_ptr<bool>(m_alive)]() {
          m_swipeAnim = 0;
          // The owner usually destroys the card here, so let the animation tick unwind first.
          DeferredCall::callLater([this, alive]() {
            if (alive.expired()) return;
            if (m_callbacks.onDismissed) m_callbacks.onDismissed();
          });
        },
        this
    );
    m_fadeAnim = m_animations->animate(
        fromOpacity, 0.0F, 240.0F, Easing::Linear,
        [this](float v) {
          m_surface->setOpacity(v);
          if (m_shadow != nullptr) m_shadow->setOpacity(v);
        },
        [this]() { m_fadeAnim = 0; }, this
    );
    markPaintDirty(); // wake the surface: animations only tick on frames
  }

  // Enter/leave arrive per InputArea (card, close button, chips): settle once the dispatch is over so moving
  // between them doesn't flicker the hover state.
  void NotificationCard::hoverRef(int delta) {
    m_hoverRefs = std::max(0, m_hoverRefs + delta);
    if (m_hoverPending) return;
    m_hoverPending = true;
    DeferredCall::callLater([this, alive = std::weak_ptr<bool>(m_alive)]() {
      if (alive.expired()) return;
      m_hoverPending = false;
      const bool hovered = m_hoverRefs > 0;
      if (hovered == m_hovered) return;
      m_hovered = hovered;
      applyHover();
      if (m_callbacks.onHoverChanged) m_callbacks.onHoverChanged(hovered);
    });
  }

  void NotificationCard::applyHover() {
    if (m_background != nullptr) {
      const ColorSpec border = m_data.critical ? colorSpecFromRole(ColorRole::Error, 0.7F)
          : m_hovered                          ? colorSpecFromRole(ColorRole::OnSurface, 0.14F)
                                               : surfaceBorder();
      m_background->setBorder(border, m_data.critical ? 1.0F : surfaceBorderWidth());
      if (!m_style.popup) {
        m_background->setFill(colorSpecFromRole(ColorRole::OnSurface, m_hovered ? 0.07F : 0.045F));
      }
    }
    if (m_close != nullptr) {
      const float target = m_hovered ? 1.0F : 0.0F;
      if (m_animations == nullptr) {
        m_close->setOpacity(target);
      } else {
        m_animations->cancel(m_closeAnim);
        m_closeAnim = m_animations->animate(
            m_close->opacity(), target, 140.0F, Easing::Linear, [this](float v) { m_close->setOpacity(v); },
            [this]() { m_closeAnim = 0; }, this
        );
        markPaintDirty();
      }
    }
  }

} // namespace kusanagi
