// Animation.hpp — Behaviour-style animated values. `v.to(target, ms, easing)` retargets from the
// current value (like a QML Behavior); `v.set(x)` jumps. Running values register with their
// Clock (one per surface) which the surface ticks every frame while anything is moving.
#pragma once
#include "renderer/Renderer.hpp"

#include <functional>
#include <set>

namespace ks::anim {

enum class Ease { Linear, InCubic, OutCubic, InOutCubic, OutQuint, OutBack, Bezier };

struct Easing {
    Ease type = Ease::Linear;
    float overshoot = 1.70158f;
    float x1 = 0, y1 = 0, x2 = 1, y2 = 1;
    float operator()(float t) const;

    static Easing linear() { return {}; }
    static Easing outCubic() { return {Ease::OutCubic}; }
    static Easing inCubic() { return {Ease::InCubic}; }
    static Easing outQuint() { return {Ease::OutQuint}; }
    static Easing outBack(float s = 1.70158f) { return {Ease::OutBack, s}; }
    static Easing bezier(float x1, float y1, float x2, float y2) { return {Ease::Bezier, 0, x1, y1, x2, y2}; }
};

double nowMs();

class Clock;

class Base {
public:
    virtual ~Base();
    virtual bool tick(double now) = 0;   // false when finished
    void attach(Clock* c) { m_clock = c; }
    Clock* clock() const { return m_clock; }

protected:
    void start();
    Clock* m_clock = nullptr;
    bool m_running = false;
    friend class Clock;
};

class Clock {
public:
    ~Clock();
    std::function<void()> wake;          // something started: request a frame
    bool tick(double now);               // true = still animating
    void add(Base* a);
    void remove(Base* a) { m_active.erase(a); }
    bool idle() const { return m_active.empty(); }

private:
    std::set<Base*> m_active;
};

inline float mix(float a, float b, float t) { return a + (b - a) * t; }
inline gfx::Color mix(const gfx::Color& a, const gfx::Color& b, float t) { return gfx::lerp(a, b, t); }

template <class T> class Value : public Base {
public:
    Value(T v = T{}) : m_value(v), m_from(v), m_to(v) {}
    Value& operator=(const Value&) = delete;

    const T& get() const { return m_value; }
    operator const T&() const { return m_value; }
    const T& target() const { return m_to; }
    bool running() const { return m_running; }

    void set(const T& v) {
        m_value = m_from = m_to = v;
        if (m_running && m_clock) m_clock->remove(this);
        m_running = false;
    }
    // animate to v (no-op if already heading there). ms <= 1 or no clock = jump
    void to(const T& v, float ms, Easing e = {}) {
        if (v == m_to && (m_running || m_value == v)) return;
        if (ms <= 1 || !m_clock) { set(v); return; }
        m_from = m_value;
        m_to = v;
        m_dur = ms;
        m_ease = e;
        m_start = nowMs();
        start();
    }
    bool tick(double now) override {
        float t = m_dur > 0 ? float((now - m_start) / m_dur) : 1.f;
        if (t >= 1) {
            m_value = m_to;
            return false;
        }
        m_value = mix(m_from, m_to, m_ease(std::max(0.f, t)));
        return true;
    }

private:
    T m_value, m_from, m_to;
    double m_start = 0;
    float m_dur = 0;
    Easing m_ease;
};

// QML Config.ms() / Config.bounce() — global motion settings
void setSpeed(double animSpeed, double bounce);
float ms(float base);          // max(1, base × anim_speed); 0 speed = instant
float bounce(float base);      // base × look.bounce

} // namespace ks::anim
