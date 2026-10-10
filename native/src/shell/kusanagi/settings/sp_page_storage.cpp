// Settings > Storage: disk usage from df, and package manager cleanup (cache, orphans, old kernels) from
// `kusanagi distro storage`. Cleanups run in a terminal.

#include "core/timer_manager.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/kusanagi_ipc.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace kusanagi::sp {

  namespace {

    struct Disk {
      std::string src;
      std::string fs;
      double size = 0.0;
      double used = 0.0;
      std::string mount;
    };

    struct State {
      std::vector<Disk> disks;
      std::map<std::string, std::string> st; // cache, cacheSize, cacheClean, orphans, orphansRemove, kernels, kernelsRemove
      Timer refresh;                           // Checks again 6 s after a cleanup.
    };

    bool has(const State& s, const char* key) {
      const auto it = s.st.find(key);
      return it != s.st.end() && !it->second.empty();
    }
    std::string stv(const State& s, const char* key) {
      const auto it = s.st.find(key);
      return it != s.st.end() ? it->second : std::string();
    }
    // The check has answered once it printed `cache`.
    bool ready(const State& s) { return s.st.contains("cache"); }

    std::string gib(double b) {
      char buf[32];
      if (b >= 1099511627776.0) std::snprintf(buf, sizeof(buf), "%.2f TiB", b / 1099511627776.0);
      else std::snprintf(buf, sizeof(buf), "%.1f GiB", b / 1073741824.0);
      return buf;
    }

    void readDisks(const std::weak_ptr<State>& weak) {
      run({"df", "-B1", "--output=source,fstype,size,used,target", "-x", "tmpfs", "-x", "devtmpfs", "-x", "efivarfs", "-x", "overlay"},
          [weak](const std::string& out, int) {
            auto s = weak.lock();
            if (!s) return;
            std::vector<Disk> disks;
            std::set<std::string> seen;
            std::istringstream in(out);
            std::string line;
            std::getline(in, line); // Skip the header.
            while (std::getline(in, line)) {
              std::istringstream ls(line);
              std::vector<std::string> f;
              for (std::string w; ls >> w;) f.push_back(w);
              if (f.size() < 5 || f[1].starts_with("fuse.") || seen.contains(f[0])) continue;
              seen.insert(f[0]);
              Disk d{.src = f[0], .fs = f[1], .size = std::strtod(f[2].c_str(), nullptr), .used = std::strtod(f[3].c_str(), nullptr)};
              for (std::size_t i = 4; i < f.size(); ++i) d.mount += (i > 4 ? " " : "") + f[i];
              disks.push_back(std::move(d));
            }
            s->disks = std::move(disks);
            refresh();
          });
    }

    void readHousekeeping(const std::weak_ptr<State>& weak) {
      run({cliCommand(), "distro", "storage"}, [weak](const std::string& out, int) {
        auto s = weak.lock();
        if (!s) return;
        std::map<std::string, std::string> o;
        std::istringstream in(out);
        for (std::string l; std::getline(in, l);) {
          const auto i = l.find('=');
          if (i != std::string::npos && i > 0) o[l.substr(0, i)] = l.substr(i + 1);
        }
        s->st = std::move(o);
        refresh();
      });
    }

    // One disk: mount point, device and filesystem, used / size, and a usage bar underneath.
    class DiskLine : public Item {
    public:
      DiskLine(std::shared_ptr<State> state, std::string src) : m_state(std::move(state)), m_src(std::move(src)) {
        m_mount = static_cast<Label*>(addChild(makeText("", 12.0F, true)));
        m_mount->setMaxLines(1);
        m_dev = static_cast<Label*>(addChild(makeText("", 10.0F, false, dim())));
        m_dev->setMaxLines(1);
        m_use = static_cast<Label*>(addChild(makeText("", 11.0F)));
        m_use->setMaxLines(1);
        m_track = static_cast<Box*>(addChild(ui::box({})));
        m_fill = static_cast<Box*>(addChild(ui::box({})));
        sync();
      }

      void sync() override {
        const Disk* d = disk();
        if (d == nullptr) return;
        bool changed = m_mount->setText(d->mount);
        changed = m_dev->setText(d->src + "  ·  " + d->fs) || changed;
        changed = m_use->setText(gib(d->used) + " / " + gib(d->size)) || changed;
        m_frac = d->size > 0.0 ? static_cast<float>(d->used / d->size) : 0.0F;
        if (changed) requestLayout();
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        m_mount->measure(renderer);
        m_dev->measure(renderer);
        m_use->measure(renderer);
        m_mount->setPosition(0.0F, 0.0F);
        m_dev->setPosition(120.0F, 0.0F);
        m_use->setPosition(width - m_use->width(), 0.0F);
        const float barY = 18.0F + 6.0F;
        m_track->setPosition(0.0F, barY);
        m_track->setSize(width, 6.0F);
        m_track->setRadius(3.0F);
        m_track->setFill(textA(0.1F));
        m_fill->setPosition(0.0F, barY);
        m_fill->setSize(width * m_frac, 6.0F);
        m_fill->setRadius(3.0F);
        m_fill->setFill(m_frac > 0.9F ? danger() : m_frac > 0.75F ? fixedColorSpec(Color{0xe8 / 255.0F, 0xbe / 255.0F, 0x62 / 255.0F, 1.0F})
                                                                   : accent());
        const float h = barY + 6.0F;
        setSize(width, h);
        return h;
      }

    private:
      [[nodiscard]] const Disk* disk() const {
        for (const auto& d : m_state->disks) {
          if (d.src == m_src) return &d;
        }
        return nullptr;
      }
      std::shared_ptr<State> m_state;
      std::string m_src;
      Label* m_mount = nullptr;
      Label* m_dev = nullptr;
      Label* m_use = nullptr;
      Box* m_track = nullptr;
      Box* m_fill = nullptr;
      float m_frac = 0.0F;
    };

  } // namespace

  void buildStorage(Column& page) {
    auto state = std::make_shared<State>();
    std::weak_ptr<State> weak = state;
    readDisks(weak);
    readHousekeeping(weak);

    // Runs a cleanup in a terminal and checks again 6 s later.
    const auto term = [weak](const std::string& key) {
      auto s = weak.lock();
      if (!s) return;
      spawn({get<std::string>("launcher.terminal", "foot"), "-e", "sh", "-c",
             stv(*s, key.c_str()) + "; echo; printf 'done — Enter to close '; read _"});
      s->refresh.start(std::chrono::milliseconds(6000), [weak]() {
        readDisks(weak);
        readHousekeeping(weak);
      });
    };

    {
      auto* g = page.add<Group>("Disks");
      g->add<Repeater>(
          [state]() {
            std::vector<std::string> keys;
            for (const auto& d : state->disks) keys.push_back(d.src);
            return keys;
          },
          [state](const std::string& src) -> std::unique_ptr<Item> { return std::make_unique<DiskLine>(state, src); }
      );
    }

    {
      auto* g = page.add<Group>("Clean up", "Each runs in a terminal and asks for your password.");
      g->showIf([state]() {
        return ready(*state) && (has(*state, "cacheClean") || has(*state, "orphansRemove") || has(*state, "kernelsRemove"));
      });

      auto cache = std::make_unique<Chip>("Clean", 0xf00e3);
      cache->onClick([term]() { term("cacheClean"); });
      g->add<Row>("Package cache", std::string{}, std::move(cache))
          ->bindHint([state]() {
            const std::string size = stv(*state, "cacheSize");
            return (size.empty() ? "?" : size) + " in " + stv(*state, "cache");
          })
          ->showIf([state]() { return has(*state, "cacheClean"); });

      auto orphans = std::make_unique<Chip>("Remove", 0xf0a7a);
      orphans->onClick([term]() { term("orphansRemove"); });
      orphans->enabledIf([state]() { return stv(*state, "orphans") != "0"; });
      g->add<Row>("Unneeded packages", std::string{}, std::move(orphans))
          ->bindHint([state]() -> std::string {
            const std::string o = stv(*state, "orphans");
            if (o.empty()) return "installed as dependencies that nothing needs any more";
            if (o == "0") return "none";
            return o + " installed as dependencies that nothing needs";
          })
          ->showIf([state]() { return has(*state, "orphansRemove"); });

      auto kernels = std::make_unique<Chip>("Remove", 0xf0a7a);
      kernels->onClick([term]() { term("kernelsRemove"); });
      kernels->enabledIf([state]() { return stv(*state, "kernels") != "0"; });
      g->add<Row>("Old kernels", std::string{}, std::move(kernels))
          ->bindHint([state]() -> std::string {
            const std::string k = stv(*state, "kernels");
            return k == "0" ? std::string("none to remove") : k + " removable";
          })
          ->showIf([state]() { return has(*state, "kernelsRemove"); });
    }
  }

} // namespace kusanagi::sp
