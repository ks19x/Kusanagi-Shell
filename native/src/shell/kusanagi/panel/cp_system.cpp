#include "render/animation/animation_manager.h"
#include "render/core/renderer.h"
#include "shell/control_center/control_center_services.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/panel/control_panel.h"
#include "shell/kusanagi/panel/cp_controls.h"
#include "shell/kusanagi/panel/cp_pages.h"
#include "system/system_monitor_service.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/countdown_ring.h"
#include "ui/controls/graph.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <thread>
#include <unistd.h>

namespace kusanagi {

  namespace {
    constexpr float kGauge = 92.0F;
    constexpr float kStroke = 7.0F;
    constexpr float kArcR = kGauge / 2.0F - 5.0F;
    constexpr float kStartDeg = 135.0F;                      // bottom left, clockwise
    constexpr float kSweepDeg = 270.0F;
    constexpr int kHistory = 60;                             // samples, one a second

    std::string readFirstLine(const char* path) {
      std::ifstream f(path);
      std::string s;
      std::getline(f, s);
      return s;
    }

    // Bytes per second with a decimal unit.
    std::string rate(double bytes) {
      static const char* units[] = {"B", "kB", "MB", "GB"};
      int i = 0;
      while (bytes >= 1000.0 && i < 3) {
        bytes /= 1000.0;
        ++i;
      }
      char buf[32];
      if (i == 0) {
        std::snprintf(buf, sizeof(buf), "%d%s/s", static_cast<int>(std::lround(bytes)), units[i]);
      } else {
        std::snprintf(buf, sizeof(buf), "%.1f%s/s", bytes, units[i]);
      }
      return buf;
    }

    std::string uptimeText() {
      const long s = std::atol(readFirstLine("/proc/uptime").c_str());
      const long d = s / 86400;
      const long h = s % 86400 / 3600;
      const long m = s % 3600 / 60;
      std::string out;
      if (d > 0) out += std::to_string(d) + "d ";
      if (d > 0 || h > 0) out += std::to_string(h) + "h ";
      return out + std::to_string(m) + "m";
    }

    std::string loadText() {
      std::string l = readFirstLine("/proc/loadavg");
      std::string out;
      int fields = 0;
      std::size_t i = 0;
      while (fields < 3 && i < l.size()) {
        const auto sp = l.find(' ', i);
        out += (fields ? "  " : "") + l.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
        ++fields;
        if (sp == std::string::npos) break;
        i = sp + 1;
      }
      return out;
    }

    // The default route's interface, empty when offline.
    std::string defaultInterface() {
      std::ifstream f("/proc/net/route");
      std::string line;
      std::getline(f, line);
      while (std::getline(f, line)) {
        char iface[64] = {};
        char dest[16] = {};
        unsigned flags = 0;
        char gw[16] = {};
        if (std::sscanf(line.c_str(), "%63s %15s %15s %x", iface, dest, gw, &flags) == 4
            && std::string(dest) == "00000000" && (flags & 1U) != 0) {
          return iface;
        }
      }
      return "";
    }

    void setBar(Box* track, Box* fill, float frac) {
      fill->setSize(std::max(0.0F, track->width() * std::clamp(frac, 0.0F, 1.0F)), track->height());
    }
  } // namespace

  CpSystem::CpSystem(ControlPanel& panel, const ControlCenterServices& services) : CpPage(panel, services) {
    if (m_services.sysmon != nullptr) {
      // The detailed sensors only run while this page shows them.
      m_services.sysmon->retainCpuTemp();
      m_services.sysmon->retainGpuTemp();
      m_services.sysmon->retainGpuUsage();
      m_services.sysmon->retainGpuVram();
      m_services.sysmon->retainDiskPath("/");
    }
    readHostInfo();

    m_gaugeCard = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    buildGauge(m_gauges[0], "CPU");
    buildGauge(m_gauges[1], "MEMORY");
    buildGauge(m_gauges[2], "GPU");
    buildGauge(m_gauges[3], "TEMP", 70.0F, 85.0F);

    m_cpuCard = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    m_cpuTitle = static_cast<Label*>(m_cpuCard->addChild(cp::caption("CPU LOAD")));
    m_load = static_cast<Label*>(m_cpuCard->addChild(cp::text("", 10.0F, false, cp::dim())));
    m_cpuSpark = static_cast<Graph*>(m_cpuCard->addChild(std::make_unique<Graph>()));
    m_cpuSpark->setColor(cp::accent());
    m_cpuSpark->setLineWidth(1.6F);
    m_cpuSpark->setFillOpacity(0.2F);

    m_netCard = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    m_netTitle = static_cast<Label*>(m_netCard->addChild(cp::caption("NETWORK")));
    m_netIf = static_cast<Label*>(m_netCard->addChild(cp::text("", 10.0F, false, cp::dim())));
    m_downIcon = static_cast<Label*>(m_netCard->addChild(cp::icon(0xf0045, 13.0F, cp::accent())));
    m_down = static_cast<Label*>(m_netCard->addChild(cp::text("", 12.0F, true)));
    m_upIcon = static_cast<Label*>(m_netCard->addChild(cp::icon(0xf005d, 13.0F, cp::dim())));
    m_up = static_cast<Label*>(m_netCard->addChild(cp::text("", 12.0F, true)));
    m_netSpark = static_cast<Graph*>(m_netCard->addChild(std::make_unique<Graph>()));
    m_netSpark->setColor(cp::accent());
    m_netSpark->setLineWidth(1.6F);
    m_netSpark->setFillOpacity(0.2F);

    m_memCard = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    buildUsage(m_vram, m_memCard, "VRAM");
    buildUsage(m_disk, m_memCard, "DISK  /");

    m_footer = static_cast<Label*>(addChild(cp::text("", 10.0F, false, cp::dim())));
  }

  CpSystem::~CpSystem() {
    if (m_services.sysmon != nullptr) {
      m_services.sysmon->releaseCpuTemp();
      m_services.sysmon->releaseGpuTemp();
      m_services.sysmon->releaseGpuUsage();
      m_services.sysmon->releaseGpuVram();
      m_services.sysmon->releaseDiskPath("/");
    }
  }

  void CpSystem::readHostInfo() {
    m_kernel = readFirstLine("/proc/sys/kernel/osrelease");
    m_host = readFirstLine("/proc/sys/kernel/hostname");
    m_threads = static_cast<int>(std::thread::hardware_concurrency());
    // Find a GPU with VRAM counters (amdgpu) and its hwmon.
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
      const std::string name = e.path().filename().string();
      if (!name.starts_with("card") || name.find('-') != std::string::npos) continue;
      const auto dev = e.path() / "device";
      if (!std::filesystem::exists(dev / "mem_info_vram_total", ec)) continue;
      m_gpuDev = dev.string();
      for (const auto& h : std::filesystem::directory_iterator(dev / "hwmon", ec)) {
        if (std::filesystem::exists(h.path() / "temp1_input", ec)) {
          m_gpuHwmon = h.path().string();
          break;
        }
      }
      break;
    }
    tick();
  }

  void CpSystem::buildGauge(Gauge& g, const std::string& label, float warnAt, float hotAt) {
    g.warnAt = warnAt;
    g.hotAt = hotAt;
    g.node = m_gaugeCard->addChild(ui::node({}));
    const auto ring = [&g](const ColorSpec& color) {
      auto r = std::make_unique<CountdownRing>();
      r->setThickness(kStroke);
      r->setRingSize((kArcR + kStroke / 2.0F) * 2.0F);
      r->setColor(color);
      // The ring starts at 12 o'clock; turn it so the gauge starts at the bottom left.
      r->setRotation((kStartDeg + 90.0F) * std::numbers::pi_v<float> / 180.0F);
      return static_cast<CountdownRing*>(g.node->addChild(std::move(r)));
    };
    g.track = ring(cp::textA(0.08F));
    g.track->setProgress(kSweepDeg / 360.0F);
    g.arc = ring(cp::accent());
    g.arc->setProgress(0.0F);
    // Round caps
    for (std::size_t i = 0; i < g.caps.size(); ++i) {
      Box* cap = static_cast<Box*>(g.node->addChild(ui::box({})));
      cap->setSize(kStroke, kStroke);
      cap->setRadius(kStroke / 2.0F);
      cap->setFill(i < 2 ? cp::textA(0.08F) : cp::accent());
      g.caps[i] = cap;
    }
    g.value = static_cast<Label*>(g.node->addChild(cp::text("0%", 19.0F, true)));
    g.label = static_cast<Label*>(g.node->addChild(cp::text("", 11.0F, true)));
    cp::setSpacedText(*g.label, label, 1.0F);
    g.detail = static_cast<Label*>(g.node->addChild(cp::text("", 10.0F, false, cp::dim())));
  }

  void CpSystem::applyGauge(Gauge& g) {
    const float f = std::clamp(g.shown, 0.0F, 100.0F) / 100.0F;
    g.arc->setProgress(std::max(0.5F / 360.0F, kSweepDeg / 360.0F * f));
    const ColorSpec tint = g.target >= g.hotAt ? cp::danger() : g.target >= g.warnAt ? kusanagi::color("warn") : cp::accent();
    g.arc->setColor(tint);
    g.caps[2]->setFill(tint);
    const auto at = [](float deg) {
      const float a = deg * std::numbers::pi_v<float> / 180.0F;
      return std::pair<float, float>{kGauge / 2.0F + kArcR * std::cos(a), kGauge / 2.0F + kArcR * std::sin(a)};
    };
    const std::array<float, 3> angles{kStartDeg, kStartDeg + kSweepDeg, kStartDeg + std::max(0.5F, kSweepDeg * f)};
    for (std::size_t i = 0; i < 3; ++i) {
      const auto [x, y] = at(angles[i]);
      g.caps[i]->setPosition(x - kStroke / 2.0F, y - kStroke / 2.0F);
    }
  }

  void CpSystem::setGauge(Gauge& g, float value) {
    if (value == g.target) return;
    g.target = value;
    AnimationManager* anims = animationManager();
    if (anims == nullptr) {
      g.shown = value;
      applyGauge(g);
      return;
    }
    anims->cancelForOwner(g.arc);
    const float from = g.shown;
    Gauge* gp = &g;
    anims->animate(0.0F, 1.0F, 700.0F, Easing::EaseOutCubic,
                   [this, gp, from, value](float t) {
                     gp->shown = from + (value - from) * t;
                     applyGauge(*gp);
                   },
                   {}, g.arc);
  }

  void CpSystem::buildUsage(Usage& u, cp::Card* card, const std::string& label) {
    u.label = static_cast<Label*>(card->addChild(cp::caption(label)));
    u.text = static_cast<Label*>(card->addChild(cp::text("—", 11.0F)));
    u.track = static_cast<Box*>(card->addChild(ui::box({})));
    u.track->setFill(cp::textA(0.1F));
    u.track->setRadius(3.0F);
    u.fill = static_cast<Box*>(card->addChild(ui::box({})));
    u.fill->setFill(cp::accent());
    u.fill->setRadius(3.0F);
  }

  bool CpSystem::sync(Renderer& /*renderer*/) {
    if (m_services.sysmon == nullptr) return false;
    bool changed = false;
    const SystemStats st = m_services.sysmon->latest();
    const auto set = [&changed](Label* l, const std::string& s) { changed = l->setText(s) || changed; };
    char buf[64];

    const float cpu = static_cast<float>(st.cpuUsagePercent);
    set(m_gauges[0].value, std::to_string(static_cast<int>(std::lround(cpu))) + "%");
    const double ghz = m_cpuGhz > 0.0 ? m_cpuGhz : st.cpuFreqMhz / 1000.0;
    if (ghz > 0.0) {
      std::snprintf(buf, sizeof(buf), "%.2f GHz", ghz);
      set(m_gauges[0].detail, buf);
    } else {
      set(m_gauges[0].detail, std::to_string(m_threads) + " threads");
    }
    setGauge(m_gauges[0], cpu);

    const float ram = static_cast<float>(st.ramUsagePercent);
    set(m_gauges[1].value, std::to_string(static_cast<int>(std::lround(ram))) + "%");
    std::snprintf(buf, sizeof(buf), "%.1f / %d GiB", static_cast<double>(st.ramUsedMb) / 1024.0,
                  static_cast<int>(std::lround(static_cast<double>(st.ramTotalMb) / 1024.0)));
    set(m_gauges[1].detail, buf);
    setGauge(m_gauges[1], ram);

    const float gpu = st.gpuUsagePercent ? static_cast<float>(*st.gpuUsagePercent) : 0.0F;
    set(m_gauges[2].value, std::to_string(static_cast<int>(std::lround(gpu))) + "%");
    set(m_gauges[2].detail, m_gpuMhz > 0 ? std::to_string(m_gpuMhz) + " MHz" : "—");
    setGauge(m_gauges[2], gpu);

    const int temp = st.cpuTempC ? static_cast<int>(std::lround(*st.cpuTempC)) : 0;
    const int gpuTemp = st.gpuTempC ? static_cast<int>(std::lround(*st.gpuTempC)) : std::max(0, m_gpuTemp);
    set(m_gauges[3].value, std::to_string(temp) + "°");
    set(m_gauges[3].detail, "GPU " + std::to_string(gpuTemp) + "°");
    setGauge(m_gauges[3], static_cast<float>(temp));

    set(m_load, "load " + loadText());

    // Histories: the last minute, newest on the right.
    const auto hist = m_services.sysmon->history(kHistory);
    std::vector<float> cpuH;
    std::vector<float> netH;
    double netMax = 1.0;
    for (const auto& h : hist) netMax = std::max(netMax, h.netRxBytesPerSec + h.netTxBytesPerSec);
    for (const auto& h : hist) {
      cpuH.push_back(static_cast<float>(std::clamp(h.cpuUsagePercent / 100.0, 0.0, 1.0)));
      netH.push_back(static_cast<float>((h.netRxBytesPerSec + h.netTxBytesPerSec) / (netMax * 1.15)));
    }
    m_cpuSpark->setValues(std::move(cpuH));
    m_netSpark->setValues(std::move(netH));

    const std::string iface = defaultInterface();
    set(m_netIf, iface.empty() ? "offline" : iface);
    set(m_down, rate(st.netRxBytesPerSec));
    set(m_up, rate(st.netTxBytesPerSec));

    const double vramUsed = st.gpuVramUsedBytes ? static_cast<double>(*st.gpuVramUsedBytes) / 1073741824.0 : m_vramUsed;
    const double vramTotal = st.gpuVramTotalBytes ? static_cast<double>(*st.gpuVramTotalBytes) / 1073741824.0 : m_vramTotal;
    if (vramTotal > 0.0) {
      std::snprintf(buf, sizeof(buf), "%.1f / %.1f GiB", vramUsed, vramTotal);
      set(m_vram.text, buf);
    } else {
      set(m_vram.text, "—");
    }
    m_vram.frac = vramTotal > 0.0 ? static_cast<float>(vramUsed / vramTotal) : 0.0F;
    const auto disk = m_services.sysmon->diskStats("/");
    if (disk && disk->totalBytes > 0) {
      const double used = static_cast<double>(disk->totalBytes - disk->freeBytes) / 1073741824.0;
      const double total = static_cast<double>(disk->totalBytes) / 1073741824.0;
      std::snprintf(buf, sizeof(buf), "%.1f / %.1f GiB", used, total);
      set(m_disk.text, buf);
      m_disk.frac = static_cast<float>(used / total);
    } else {
      set(m_disk.text, "—");
      m_disk.frac = 0.0F;
    }
    setBar(m_vram.track, m_vram.fill, m_vram.frac);
    setBar(m_disk.track, m_disk.fill, m_disk.frac);

    const char* user = std::getenv("USER");
    set(m_footer, std::string(user != nullptr ? user : "") + "@" + m_host + "   ·   up " + uptimeText() + "   ·   kernel " + m_kernel);
    return changed;
  }

  void CpSystem::tick() {
    // CPU clock: the mean of the "cpu MHz" lines in /proc/cpuinfo.
    {
      std::ifstream f("/proc/cpuinfo");
      double sum = 0.0;
      int n = 0;
      for (std::string line; std::getline(f, line);) {
        if (!line.starts_with("cpu MHz")) continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        sum += std::atof(line.c_str() + colon + 1);
        ++n;
      }
      m_cpuGhz = n > 0 ? sum / n / 1000.0 : 0.0;
    }
    if (m_gpuDev.empty()) return;
    m_vramUsed = std::atof(readFirstLine((m_gpuDev + "/mem_info_vram_used").c_str()).c_str()) / 1073741824.0;
    m_vramTotal = std::atof(readFirstLine((m_gpuDev + "/mem_info_vram_total").c_str()).c_str()) / 1073741824.0;
    m_gpuMhz = 0;
    std::ifstream sclk(m_gpuDev + "/pp_dpm_sclk");
    for (std::string line; std::getline(sclk, line);) {
      if (line.find('*') == std::string::npos) continue;
      const auto colon = line.find(':');
      if (colon != std::string::npos) m_gpuMhz = std::atoi(line.c_str() + colon + 1);
      break;
    }
    if (!m_gpuHwmon.empty()) {
      m_gpuTemp = static_cast<int>(std::lround(std::atof(readFirstLine((m_gpuHwmon + "/temp1_input").c_str()).c_str()) / 1000.0));
    }
  }

  void CpSystem::layoutGauge(Renderer& renderer, Gauge& g) {
    const float ringOff = kGauge / 2.0F - (kArcR + kStroke / 2.0F);
    g.track->layout(renderer);
    g.arc->layout(renderer);
    g.track->setPosition(ringOff, ringOff);
    g.arc->setPosition(ringOff, ringOff);
    applyGauge(g);
    g.value->measure(renderer);
    cp::centerIn(*g.value, 0.0F, 0.0F, kGauge, kGauge);
    g.label->measure(renderer);
    g.detail->measure(renderer);
    const float top = kGauge - 6.0F;
    g.label->setPosition(std::round((kGauge - g.label->width()) / 2.0F), top);
    g.detail->setPosition(std::round((kGauge - g.detail->width()) / 2.0F), top + g.label->height() + 1.0F);
    g.node->setSize(kGauge, kGauge + 34.0F);
  }

  float CpSystem::layout(Renderer& renderer, float width) {
    float y = 0.0F;

    // Gauges: four in a row, centred.
    m_gaugeCard->setPosition(0.0F, y);
    m_gaugeCard->setSize(width, 150.0F);
    const float spacing = (width - 4.0F * kGauge - 24.0F) / 3.0F;
    const float rowW = 4.0F * kGauge + 3.0F * spacing;
    for (std::size_t i = 0; i < m_gauges.size(); ++i) {
      layoutGauge(renderer, m_gauges[i]);
      m_gauges[i].node->setPosition(std::round((width - rowW) / 2.0F + static_cast<float>(i) * (kGauge + spacing)),
                                    std::round((150.0F - (kGauge + 34.0F)) / 2.0F));
    }
    y += 150.0F + 12.0F;

    // CPU history
    m_cpuCard->setPosition(0.0F, y);
    m_cpuCard->setSize(width, 86.0F);
    m_cpuTitle->measure(renderer);
    m_cpuTitle->setPosition(14.0F, 10.0F);
    m_load->measure(renderer);
    m_load->setPosition(width - 14.0F - m_load->width(), 8.0F);
    m_cpuSpark->setPosition(12.0F, 86.0F - 12.0F - 48.0F);
    m_cpuSpark->setSize(width - 24.0F, 48.0F);
    m_cpuSpark->layout(renderer);
    y += 86.0F + 12.0F;

    // Network, and VRAM or disk
    const float half = std::round((width - 12.0F) / 2.0F);
    m_netCard->setPosition(0.0F, y);
    m_netCard->setSize(half, 112.0F);
    m_netTitle->measure(renderer);
    m_netTitle->setPosition(14.0F, 10.0F);
    m_netIf->measure(renderer);
    m_netIf->setPosition(half - 14.0F - m_netIf->width(), 9.0F);
    float x = 14.0F;
    for (Label* l : {m_downIcon, m_down, m_upIcon, m_up}) l->measure(renderer);
    const float rowH = std::max(m_downIcon->height(), m_down->height());
    m_downIcon->setPosition(x, 30.0F);
    x += m_downIcon->width() + 4.0F;
    m_down->setPosition(x, 30.0F);
    x += m_down->width() + 16.0F;
    m_upIcon->setPosition(x, 30.0F);
    x += m_upIcon->width() + 4.0F;
    m_up->setPosition(x, 30.0F);
    (void)rowH;
    m_netSpark->setPosition(12.0F, 112.0F - 12.0F - 40.0F);
    m_netSpark->setSize(half - 24.0F, 40.0F);
    m_netSpark->layout(renderer);

    m_memCard->setPosition(width - half, y);
    m_memCard->setSize(half, 112.0F);
    float uy = 12.0F;
    for (Usage* u : {&m_vram, &m_disk}) {
      u->label->measure(renderer);
      u->text->measure(renderer);
      u->label->setPosition(14.0F, uy);
      u->text->setPosition(half - 14.0F - u->text->width(), uy);
      u->track->setPosition(14.0F, uy + 14.0F + 6.0F);
      u->track->setSize(half - 28.0F, 6.0F);
      u->fill->setPosition(14.0F, uy + 14.0F + 6.0F);
      setBar(u->track, u->fill, u->frac);
      uy += 14.0F + 6.0F + 6.0F + 14.0F;
    }
    y += 112.0F + 12.0F;

    m_footer->measure(renderer);
    m_footer->setPosition(std::round((width - m_footer->width()) / 2.0F), y);
    return y + m_footer->height();
  }

} // namespace kusanagi
