#pragma once

// Sliding selection highlight for the launcher and clipboard lists: one box under the rows that glides to the
// current row (even acceleration, then even deceleration; retargeting mid-way keeps the current speed) and
// scrolls the view along so it stays in sight. The caller styles the box. Since the highlight does the
// scrolling, the list must not also scroll to its selection by itself.

#include "render/animation/animation_manager.h"

#include <array>

class Box;
class ScrollView;

namespace kusanagi {

  class SlideHighlight {
  public:
    // A move takes `durationMs` (already scaled by the animation speed setting), or less when `velocity`
    // (px/s) gets there sooner.
    struct Motion {
      float durationMs = 0.0F; // 0 = jump
      float velocity = -1.0F;  // <= 0 = no speed limit
    };
    struct Rect {
      float x = 0.0F;
      float y = 0.0F;
      float w = 0.0F;
      float h = 0.0F;
    };

    SlideHighlight() = default;
    ~SlideHighlight();
    SlideHighlight(const SlideHighlight&) = delete;
    SlideHighlight& operator=(const SlideHighlight&) = delete;

    // Puts the box first in `scroll`'s content, under the rows. Call again for a new list, and detach() when
    // the list goes away.
    void attach(ScrollView& scroll, AnimationManager* animations);
    void detach();
    [[nodiscard]] bool attached() const noexcept { return m_box != nullptr; }
    [[nodiscard]] Box* box() const noexcept { return m_box; }

    // `move` drives x and y, `resize` drives width and height.
    void setMotion(Motion move, Motion resize);
    // Without `animate` the box jumps there (new contents, first placement).
    void moveTo(const Rect& to, bool animate);
    void setVisible(bool visible);
    [[nodiscard]] const Rect& target() const noexcept { return m_target; }

  private:
    // Eased motion of one property (x, y, w or h).
    struct Axis {
      double value = 0.0;
      double to = 0.0;
      double initial = 0.0;
      double trackVelocity = 0.0;
      bool invert = false;
      bool running = false;
      double s = 0.0, vi = 0.0, a = 0.0, d = 0.0, tp = 0.0, td = 0.0, tf = 0.0, vp = 0.0, sp = 0.0, sd = 0.0;

      void restart(double target, const Motion& motion);
      void advance(double seconds);
      void jump(double target);
    };

    void tick(double elapsedMs);
    void apply();
    void follow();

    ScrollView* m_scroll = nullptr;
    AnimationManager* m_animations = nullptr;
    Box* m_box = nullptr;
    Motion m_move;
    Motion m_resize;
    Rect m_target;
    bool m_placed = false;
    std::array<Axis, 4> m_axes; // x, y, w, h
    AnimationManager::Id m_anim = 0;
  };

} // namespace kusanagi
