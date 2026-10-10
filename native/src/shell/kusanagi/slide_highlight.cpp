#include "shell/kusanagi/slide_highlight.h"

#include "render/scene/node.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/flex.h"
#include "ui/controls/scroll_view.h"

#include <algorithm>
#include <cmath>

namespace kusanagi {

  SlideHighlight::~SlideHighlight() { detach(); }

  void SlideHighlight::attach(ScrollView& scroll, AnimationManager* animations) {
    detach();
    m_scroll = &scroll;
    m_animations = animations;
    auto box = ui::box({});
    box->setParticipatesInLayout(false);
    box->setVisible(false);
    m_box = static_cast<Box*>(scroll.content()->insertChildAt(0, std::move(box)));
    m_placed = false;
  }

  void SlideHighlight::detach() {
    if (m_animations != nullptr && m_anim != 0) m_animations->cancel(m_anim);
    m_anim = 0;
    m_box = nullptr;
    m_scroll = nullptr;
    m_placed = false;
  }

  void SlideHighlight::setMotion(Motion move, Motion resize) {
    m_move = move;
    m_resize = resize;
  }

  void SlideHighlight::setVisible(bool visible) {
    if (m_box != nullptr) m_box->setVisible(visible && m_placed);
  }

  void SlideHighlight::Axis::jump(double target) {
    value = target;
    to = target;
    trackVelocity = 0.0;
    running = false;
  }

  // Plans the motion: accelerate, cruise, decelerate, arriving after tf seconds.
  void SlideHighlight::Axis::restart(double target, const Motion& motion) {
    to = target;
    if (motion.durationMs <= 0.0F) {
      jump(target);
      return;
    }
    double initialVelocity = trackVelocity;
    initial = value;
    if (to == initial) {
      trackVelocity = 0.0;
      running = false;
      return;
    }
    // Heading back the other way: keep the speed, now pointing away from the new target.
    if (trackVelocity != 0.0 && (!invert) == ((initial - to) > 0.0)) initialVelocity = -trackVelocity;
    trackVelocity = initialVelocity;
    invert = to < initial;

    s = (invert ? -1.0 : 1.0) * (to - initial);
    vi = initialVelocity;
    const double duration = motion.durationMs / 1000.0;
    tf = motion.velocity > 0.0F ? std::min(s / motion.velocity, duration) : duration;
    if (tf <= 1e-6) {
      jump(target);
      return;
    }
    const double c1 = 0.25 * tf * tf;
    const double c2 = 0.5 * vi * tf - s;
    const double c3 = -0.25 * vi * vi;
    const double a1 = (-c2 + std::sqrt(c2 * c2 - 4.0 * c1 * c3)) / (2.0 * c1);
    if (!(a1 > 0.0)) {
      jump(target);
      return;
    }
    const double tp1 = 0.5 * tf - 0.5 * vi / a1;
    const double vp1 = a1 * tp1 + vi;
    const double sp1 = 0.5 * a1 * tp1 * tp1 + vi * tp1;
    a = d = a1;
    tp = td = tp1;
    vp = vp1;
    sp = sd = sp1;
    running = true;
  }

  // Position `seconds` after the restart.
  void SlideHighlight::Axis::advance(double seconds) {
    if (!running) return;
    double v = 0.0;
    double t = seconds;
    if (t < tp) {
      trackVelocity = vi + t * a;
      v = 0.5 * a * t * t + vi * t;
    } else if (t < td) {
      t -= tp;
      trackVelocity = vp;
      v = sp + t * vp;
    } else if (t < tf) {
      t -= td;
      trackVelocity = vp - t * a;
      v = sd - 0.5 * d * t * t + vp * t;
    } else {
      trackVelocity = 0.0;
      v = s;
      running = false;
    }
    value = initial + v * (invert ? -1.0 : 1.0);
  }

  void SlideHighlight::moveTo(const Rect& to, bool animate) {
    if (m_box == nullptr) return;
    m_target = to;
    const std::array<double, 4> targets{to.x, to.y, to.w, to.h};
    if (!animate || !m_placed || m_animations == nullptr) {
      for (std::size_t i = 0; i < 4; ++i) m_axes[i].jump(targets[i]);
      if (m_anim != 0 && m_animations != nullptr) m_animations->cancel(m_anim);
      m_anim = 0;
      m_placed = true;
      m_box->setVisible(true);
      apply();
      return;
    }
    for (std::size_t i = 0; i < 4; ++i) m_axes[i].restart(targets[i], i < 2 ? m_move : m_resize);
    if (m_anim != 0) m_animations->cancel(m_anim);
    m_anim = 0;
    double longest = 0.0;
    for (const auto& axis : m_axes) {
      if (axis.running) longest = std::max(longest, axis.tf);
    }
    apply();
    if (longest <= 0.0) return;
    const auto durationMs = static_cast<float>(longest * 1000.0);
    // Real time: the durations are already scaled by the animation speed setting.
    m_anim = m_animations->animateTimer(
        0.0F, durationMs, durationMs, Easing::Linear, [this](float ms) { tick(ms); }, [this]() { m_anim = 0; },
        m_box
    );
    m_box->markPaintDirty();
  }

  void SlideHighlight::tick(double elapsedMs) {
    for (auto& axis : m_axes) axis.advance(elapsedMs / 1000.0);
    apply();
  }

  void SlideHighlight::apply() {
    if (m_box == nullptr) return;
    m_box->setPosition(static_cast<float>(m_axes[0].value), static_cast<float>(m_axes[1].value));
    m_box->setSize(std::max(0.0F, static_cast<float>(m_axes[2].value)), std::max(0.0F, static_cast<float>(m_axes[3].value)));
    follow();
  }

  // Scrolls just enough to keep the moving highlight and its target in sight.
  void SlideHighlight::follow() {
    if (m_scroll == nullptr) return;
    const float size = m_scroll->contentViewportHeight();
    if (size <= 0.0F) return;
    const float viewPos = m_scroll->scrollOffset();
    const auto trackedPos = static_cast<float>(m_axes[1].value);
    const auto trackedSize = static_cast<float>(m_axes[3].value);
    const float trackedEnd = trackedPos + trackedSize;
    const float toPos = m_target.y;
    const float toEnd = m_target.y + m_target.h;
    float pos = viewPos;
    if (trackedPos < viewPos && toPos < viewPos) {
      pos = std::max(trackedPos, toPos);
    } else if (trackedEnd >= viewPos + size && toEnd >= viewPos + size) {
      if (trackedEnd <= toEnd) {
        pos = trackedSize > size ? trackedPos : trackedEnd - size;
      } else {
        pos = m_target.h > size ? toPos : toEnd - size;
      }
    }
    if (std::abs(pos - viewPos) > 0.01F) m_scroll->setScrollOffset(pos);
  }

} // namespace kusanagi
