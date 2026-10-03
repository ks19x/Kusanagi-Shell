#include "anim/Animation.hpp"

#include <chrono>
#include <cmath>
#include <vector>

namespace ks::anim {

namespace {
double g_speed = 1.0, g_bounce = 1.0;

float bezierAt(float t, float p1, float p2) {   // cubic with p0=0, p3=1
    float u = 1 - t;
    return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
}
float solveBezier(float x, float x1, float y1, float x2, float y2) {
    float lo = 0, hi = 1, t = x;
    for (int i = 0; i < 20; i++) {
        float cx = bezierAt(t, x1, x2);
        if (std::fabs(cx - x) < 1e-4f) break;
        if (cx < x) lo = t; else hi = t;
        t = (lo + hi) * 0.5f;
    }
    return bezierAt(t, y1, y2);
}
} // namespace

float Easing::operator()(float t) const {
    switch (type) {
    case Ease::Linear: return t;
    case Ease::InCubic: return t * t * t;
    case Ease::OutCubic: { float u = t - 1; return u * u * u + 1; }
    case Ease::InOutCubic: return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.f) / 2;
    case Ease::OutQuint: { float u = t - 1; return u * u * u * u * u + 1; }
    case Ease::OutBack: { float s = overshoot, u = t - 1; return u * u * ((s + 1) * u + s) + 1; }
    case Ease::Bezier: return solveBezier(t, x1, y1, x2, y2);
    }
    return t;
}

double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Base::~Base() {
    if (m_running && m_clock) m_clock->remove(this);
}

void Base::start() {
    if (!m_clock) return;
    m_running = true;
    m_clock->add(this);
}

Clock::~Clock() {
    for (auto* a : m_active) a->m_running = false;
}

void Clock::add(Base* a) {
    bool wasIdle = m_active.empty();
    m_active.insert(a);
    if (wasIdle && wake) wake();
}

bool Clock::tick(double now) {
    std::vector<Base*> done;
    for (auto* a : m_active)
        if (!a->tick(now)) done.push_back(a);
    for (auto* a : done) {
        a->m_running = false;
        m_active.erase(a);
    }
    return !m_active.empty();
}

void setSpeed(double s, double b) {
    g_speed = s;
    g_bounce = b;
}
float ms(float base) { return std::max(1.f, std::round(base * float(g_speed))); }
float bounce(float base) { return base * float(g_bounce); }

} // namespace ks::anim
