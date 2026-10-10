// Settings > About: version and memory use, system info, and maintenance actions.

#include "compositors/compositor_detect.h"
#include "core/deferred_call.h"
#include "core/files/resource_paths.h"
#include "core/process/process.h"
#include "render/core/render_styles.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>

#ifndef KUSANAGI_VERSION
#define KUSANAGI_VERSION "dev"
#endif

namespace kusanagi::sp {

  namespace {

    std::string readFile(const char* path) {
      std::ifstream in(path);
      std::stringstream ss;
      ss << in.rdbuf();
      return ss.str();
    }

    std::string firstLine(const std::string& s) { return s.substr(0, s.find('\n')); }

    std::string env(const char* k) {
      const char* v = std::getenv(k);
      return v != nullptr ? v : "";
    }

    // Filled in by a worker (lspci, compositor --version) and the page's poll.
    struct Info {
      std::string distro = "…";
      std::string cpu = "…";
      std::string gpu = "…";
      std::string wm = "…";
      std::string kernel;
      std::string host;
      int threads = 0;
      double ramUsed = 0.0;
      double ramTotal = 0.0;
      std::string uptime;
      int shellMb = 0;
    };

    long kb(const std::string& meminfo, const char* key) {
      const auto at = meminfo.find(std::string(key) + ":");
      if (at == std::string::npos) return 0;
      return std::strtol(meminfo.c_str() + at + std::strlen(key) + 1, nullptr, 10);
    }

    void pollStats(Info& info) {
      const std::string m = readFile("/proc/meminfo");
      const long t = kb(m, "MemTotal");
      if (t > 0) {
        info.ramTotal = static_cast<double>(t) / 1048576.0;
        info.ramUsed = static_cast<double>(t - kb(m, "MemAvailable")) / 1048576.0;
      }
      const long s = static_cast<long>(std::strtod(readFile("/proc/uptime").c_str(), nullptr));
      const long d = s / 86400, h = s % 86400 / 3600, mi = s % 3600 / 60;
      info.uptime = (d != 0 ? std::to_string(d) + "d " : "") + (d != 0 || h != 0 ? std::to_string(h) + "h " : "") +
                    std::to_string(mi) + "m";
      // The shell's own proportional set size (PSS).
      const std::string sm = readFile("/proc/self/smaps_rollup");
      const auto at = sm.find("\nPss:");
      info.shellMb = at == std::string::npos ? 0 : static_cast<int>(std::lround(std::strtod(sm.c_str() + at + 5, nullptr) / 1024.0));
    }

    std::string fmt1(double v) {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%.1f", v);
      return buf;
    }

    class Hero : public Item {
    public:
      explicit Hero(std::shared_ptr<Info> info) : m_info(std::move(info)) {
        m_bg = static_cast<Box*>(addChild(ui::box({})));
        m_kanji = static_cast<Label*>(addChild(makeText("草薙", 72.0F, true, textA(0.12F))));
        m_logo = static_cast<Image*>(addChild(ui::image({.fit = ImageFit::Contain})));
        m_name = static_cast<Label*>(addChild(makeText("", 30.0F, true)));
        setSpacedText(*m_name, "KUSANAGI", 8.0F);
        m_who = static_cast<Label*>(addChild(makeText("", 12.0F, false, textA(0.75F))));
        m_mem = static_cast<Label*>(addChild(makeText("", 11.0F, false, accent())));
        sync();
      }

      void sync() override {
        bool changed = m_who->setText(env("USER") + "'s kusanagi-shell  ·  " + KUSANAGI_VERSION);
        changed = m_mem->setText("using " + std::to_string(m_info->shellMb) + " MB right now") || changed;
        if (changed) requestLayout();
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float h = 150.0F;
        setSize(width, h);
        m_bg->setSize(width, h);
        RoundedRectStyle st;
        st.fillMode = FillMode::LinearGradient;
        st.gradientDirection = GradientDirection::Horizontal;
        Color c0 = resolved(accent());
        c0.a *= 0.28F;
        Color c1 = resolved(accent2());
        c1.a *= 0.08F;
        st.gradientStops = {GradientStop{0.0F, c0}, GradientStop{1.0F, c1}, GradientStop{1.0F, c1}, GradientStop{1.0F, c1}};
        st.fill = c0;
        st.border = resolved(textA(0.08F));
        st.borderWidth = 1.0F;
        const float r = kusanagi::radius();
        st.radius = Radii(r, r, r, r);
        m_bg->setStyle(st);
        m_kanji->measure(renderer);
        m_kanji->setPosition(width - 28.0F - m_kanji->width(), std::round((h - m_kanji->height()) / 2.0F));
        if (!m_logoLoaded) {
          m_logoLoaded = true;
          (void)m_logo->setSourceFile(renderer, paths::assetPath("kusanagi-logo.svg").string(), 208, true);
        }
        m_logo->setSize(104.0F, 104.0F);
        m_logo->setPosition(22.0F, std::round((h - 104.0F) / 2.0F));
        m_name->measure(renderer);
        m_who->measure(renderer);
        m_mem->measure(renderer);
        const float colH = m_name->height() + 4.0F + m_who->height() + 4.0F + m_mem->height();
        float y = std::round((h - colH) / 2.0F);
        const float x = 22.0F + 104.0F + 20.0F;
        m_name->setPosition(x, y);
        y += m_name->height() + 4.0F;
        m_who->setPosition(x, y);
        y += m_who->height() + 4.0F;
        m_mem->setPosition(x, y);
        return h;
      }

    private:
      std::shared_ptr<Info> m_info;
      Box* m_bg = nullptr;
      Label* m_kanji = nullptr;
      Image* m_logo = nullptr;
      bool m_logoLoaded = false;
      Label* m_name = nullptr;
      Label* m_who = nullptr;
      Label* m_mem = nullptr;
    };

    // Key on the left, value in a column to the right, elided.
    class InfoLine : public Item {
    public:
      InfoLine(const std::string& key, std::function<std::string()> value) : m_value(std::move(value)) {
        m_k = static_cast<Label*>(addChild(makeText(key, 11.0F, false, dim())));
        m_v = static_cast<Label*>(addChild(makeText(m_value(), 12.0F)));
        m_v->setMaxLines(1);
        m_v->setEllipsize(TextEllipsize::End);
      }
      void sync() override {
        if (m_v->setText(m_value())) requestLayout();
      }
      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        setSize(width, 22.0F);
        m_k->measure(renderer);
        m_k->setPosition(0.0F, 0.0F); // Both texts are top-aligned.
        m_v->setMaxWidth(std::max(1.0F, width - 130.0F));
        m_v->measure(renderer);
        m_v->setPosition(130.0F, 0.0F);
        return 22.0F;
      }

    private:
      std::function<std::string()> m_value;
      Label* m_k = nullptr;
      Label* m_v = nullptr;
    };

    std::string compositorCommand() {
      if (!env("MANGO_INSTANCE_SIGNATURE").empty() || !env("MANGOWC_SOCKET").empty()) return "mango -v 2>&1 | head -1";
      if (!env("NIRI_SOCKET").empty()) return "niri --version | head -1";
      if (!env("HYPRLAND_INSTANCE_SIGNATURE").empty()) {
        return "hyprctl version -j 2>/dev/null | grep -m1 '\"tag\"' | cut -d'\"' -f4 | sed 's/^/Hyprland /'";
      }
      if (!env("SWAYSOCK").empty()) return "sway --version | head -1";
      if (!env("LABWC_PID").empty()) return "labwc --version | head -1";
      if (compositors::isKde()) return "kwin_wayland --version 2>/dev/null | sed 's/^kwin_wayland/KWin/' | grep -m1 . || echo 'KDE Plasma'";
      if (compositors::isDwl()) return "dwl -v 2>&1 | grep -m1 . || echo dwl";
      return "echo Wayland";
    }

  } // namespace

  void buildAbout(Column& page) {
    auto info = std::make_shared<Info>();
    info->kernel = firstLine(readFile("/proc/sys/kernel/osrelease"));
    info->host = firstLine(readFile("/proc/sys/kernel/hostname"));
    info->threads = static_cast<int>(std::thread::hardware_concurrency());
    pollStats(*info);

    // The slow parts (lspci, the compositor's --version) run off the UI thread.
    {
      std::weak_ptr<Info> weak = info;
      const std::string script = ". /etc/os-release; echo \"$PRETTY_NAME\";"
                                 "grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//';"
                                 "lspci -mm 2>/dev/null | grep -iE 'vga|3d' | head -1 | cut -d'\"' -f6 | sed 's/.*\\[\\(.*\\)\\].*/\\1/';"
                                 + compositorCommand() + "; echo";
      std::thread([weak, script]() {
        const auto result = process::runSync(std::vector<std::string>{"sh", "-c", script});
        std::string out = result.out;
        DeferredCall::callLater([weak, out]() {
          auto i = weak.lock();
          if (!i) return;
          std::vector<std::string> lines;
          std::istringstream in(out);
          for (std::string l; std::getline(in, l);) lines.push_back(l);
          const auto at = [&lines](std::size_t n) { return n < lines.size() && !lines[n].empty() ? lines[n] : std::string("?"); };
          i->distro = at(0);
          i->cpu = at(1);
          i->gpu = at(2);
          i->wm = at(3);
          refresh();
        });
      }).detach();
    }

    page.add<Poll>(3000, [info]() {
      pollStats(*info);
      refresh();
    });
    page.add<Hero>(info);

    {
      auto* g = page.add<Group>("This machine");
      g->add<InfoLine>("Host", [info]() { return env("USER") + "@" + info->host; });
      g->add<InfoLine>("System", [info]() { return info->distro; });
      g->add<InfoLine>("Kernel", [info]() { return info->kernel; });
      g->add<InfoLine>("Compositor", [info]() { return info->wm; });
      g->add<InfoLine>("Processor", [info]() { return info->cpu + "  (" + std::to_string(info->threads) + " threads)"; });
      g->add<InfoLine>("Graphics", [info]() { return info->gpu; });
      g->add<InfoLine>("Memory", [info]() { return fmt1(info->ramUsed) + " of " + fmt1(info->ramTotal) + " GiB in use"; });
      g->add<InfoLine>("Uptime", [info]() { return info->uptime; });
    }

    {
      auto* g = page.add<Group>("Kusanagi");
      auto* flow = g->add<Flow>(8.0F);
      flow->add<Chip>("Run setup again", 0xf0493)->onClick([]() { spawn({"kusanagi", "msg", "setup", "open"}); });
      flow->add<Chip>("Check my system", 0xf04d9)->onClick([]() {
        spawn({get<std::string>("launcher.terminal", "foot"), "-e", "sh", "-c", "kusanagi doctor; echo; echo 'press Enter'; read x"});
      });
      flow->add<Chip>("Open config folder", 0xf024b)->onClick([]() { spawn({"xdg-open", expandHome("~/.config/kusanagi")}); });
      flow->add<Chip>("Edit settings.json", 0xf107b)->onClick([]() {
        spawn({"xdg-open", expandHome("~/.config/kusanagi/settings.json")});
      });
      flow->add<Chip>("Reset everything", 0xf0709)->onClick([]() { reset(); });
      flow->add<Chip>("Restart Kusanagi", 0xf0450)->onClick([]() { spawn({"kusanagi", "restart"}); });
    }
  }

} // namespace kusanagi::sp
