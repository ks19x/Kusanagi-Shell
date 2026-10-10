// Settings > Network: the current connection with throughput history, and every NetworkManager device.

#include "dbus/network/inetwork_service.h"
#include "render/core/renderer.h"
#include "shell/control_center/control_center_services.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "system/system_monitor_service.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/graph.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <sstream>

namespace kusanagi::sp {

  namespace {

    std::string rate(double bytes) {
      static const char* units[] = {"B", "kB", "MB", "GB"};
      int i = 0;
      while (bytes >= 1000.0 && i < 3) {
        bytes /= 1000.0;
        ++i;
      }
      char buf[32];
      if (i == 0) std::snprintf(buf, sizeof(buf), "%d%s/s", static_cast<int>(std::lround(bytes)), units[i]);
      else std::snprintf(buf, sizeof(buf), "%.1f%s/s", bytes, units[i]);
      return buf;
    }

    struct NmDevice {
      std::string dev;
      std::string type;
      std::string state;
      std::string conn;
    };
    using Devices = std::vector<NmDevice>;

    void readDevices(const std::weak_ptr<Devices>& weak) {
      run({"nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device"}, [weak](const std::string& out, int) {
        auto d = weak.lock();
        if (!d) return;
        Devices list;
        std::istringstream in(out);
        for (std::string l; std::getline(in, l);) {
          if (l.empty()) continue;
          std::vector<std::string> f;
          std::size_t at = 0;
          for (int i = 0; i < 3; ++i) {
            const auto c = l.find(':', at);
            f.push_back(l.substr(at, c == std::string::npos ? std::string::npos : c - at));
            at = c == std::string::npos ? l.size() : c + 1;
          }
          f.push_back(at < l.size() ? l.substr(at) : std::string()); // The connection name keeps its colons.
          if (f[1] == "loopback") continue;
          list.push_back(NmDevice{f[0], f[1], f[2], f[3]});
        }
        if (list.size() != d->size()
            || !std::equal(list.begin(), list.end(), d->begin(), [](const NmDevice& a, const NmDevice& b) {
                 return a.dev == b.dev && a.type == b.type && a.state == b.state && a.conn == b.conn;
               })) {
          *d = std::move(list);
          refresh();
        }
      });
    }

    class Connection : public Item {
    public:
      Connection() {
        m_icon = static_cast<Label*>(addChild(makeIcon(0xf0200, 30.0F)));
        m_title = static_cast<Label*>(addChild(makeText("", 14.0F, true)));
        m_detail = static_cast<Label*>(addChild(makeText("", 11.0F, false, dim())));
        m_down = static_cast<Label*>(addChild(makeText("", 13.0F, true)));
        m_up = static_cast<Label*>(addChild(makeText("", 11.0F, false, dim())));
        sync();
      }

      void sync() override {
        const ControlCenterServices& sv = services();
        const NetworkState* st = sv.network != nullptr ? &sv.network->state() : nullptr;
        const bool up = st != nullptr && st->connected;
        const bool wifi = up && st->kind == NetworkConnectivity::Wireless;
        bool changed = m_icon->setText(utf8Of(wifi ? 0xf05a9 : 0xf0200));
        m_icon->setColor(up ? accent() : dim());
        changed = m_title->setText(up ? std::string(wifi ? "Wi-Fi" : "Ethernet") + " · connected" : "Offline") || changed;
        changed = m_detail->setText(up ? st->interfaceName + "   ·   " + (st->ipv4.empty() ? "…" : st->ipv4) : "no default route") || changed;
        double down = 0.0, upRate = 0.0;
        if (sv.sysmon != nullptr) {
          const SystemStats s = sv.sysmon->latest();
          down = s.netRxBytesPerSec;
          upRate = s.netTxBytesPerSec;
        }
        changed = m_down->setText("↓ " + rate(down)) || changed;
        changed = m_up->setText("↑ " + rate(upRate)) || changed;
        if (changed) requestLayout();
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float h = 64.0F;
        m_icon->measure(renderer);
        m_icon->setPosition(0.0F, std::round((h - m_icon->height()) / 2.0F));
        m_title->measure(renderer);
        m_detail->measure(renderer);
        const float colH = m_title->height() + 2.0F + m_detail->height();
        const float x = m_icon->width() + 16.0F;
        const float cy = std::round((h - colH) / 2.0F);
        m_title->setPosition(x, cy);
        m_detail->setPosition(x, cy + m_title->height() + 2.0F);
        m_down->measure(renderer);
        m_up->measure(renderer);
        const float rH = m_down->height() + 2.0F + m_up->height();
        const float ry = std::round((h - rH) / 2.0F);
        m_down->setPosition(width - m_down->width(), ry);
        m_up->setPosition(width - m_up->width(), ry + m_down->height() + 2.0F);
        setSize(width, h);
        return h;
      }

    private:
      Label* m_icon = nullptr;
      Label* m_title = nullptr;
      Label* m_detail = nullptr;
      Label* m_down = nullptr;
      Label* m_up = nullptr;
    };

    // Download history sparkline, like the control panel's Net tab.
    class Spark : public Item {
    public:
      Spark() {
        m_graph = static_cast<Graph*>(addChild(std::make_unique<Graph>()));
        m_graph->setColor(accent());
        m_graph->setLineWidth(1.6F);
        m_graph->setFillOpacity(0.2F);
        sync();
      }
      void sync() override {
        const ControlCenterServices& sv = services();
        if (sv.sysmon == nullptr) return;
        const auto hist = sv.sysmon->history(60);
        double top = 1.0;
        for (const auto& h : hist) top = std::max(top, h.netRxBytesPerSec);
        top *= 1.15;
        std::vector<float> v;
        for (const auto& h : hist) v.push_back(static_cast<float>(h.netRxBytesPerSec / top));
        m_graph->setValues(std::move(v));
      }
      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        m_graph->setPosition(0.0F, 0.0F);
        m_graph->setSize(width, 60.0F);
        m_graph->layout(renderer);
        setSize(width, 60.0F);
        return 60.0F;
      }

    private:
      Graph* m_graph = nullptr;
    };

    class DeviceLine : public Item {
    public:
      DeviceLine(std::shared_ptr<Devices> devices, std::string dev) : m_devices(std::move(devices)), m_dev(std::move(dev)) {
        m_dot = static_cast<Box*>(addChild(ui::box({})));
        m_name = static_cast<Label*>(addChild(makeText(m_dev, 12.0F, true)));
        m_type = static_cast<Label*>(addChild(makeText("", 11.0F, false, dim())));
        m_conn = static_cast<Label*>(addChild(makeText("", 11.0F)));
        m_conn->setMaxLines(1);
        sync();
      }
      void sync() override {
        const NmDevice* d = nullptr;
        for (const auto& x : *m_devices) {
          if (x.dev == m_dev) d = &x;
        }
        if (d == nullptr) return;
        m_dot->setFill(d->state.starts_with("connected") ? fixedColorSpec(Color{0x85 / 255.0F, 0xcc / 255.0F, 0x87 / 255.0F, 1.0F})
                       : d->state == "unavailable"       ? danger()
                                                         : dim());
        bool changed = m_type->setText(d->type);
        changed = m_conn->setText(d->conn.empty() ? d->state : d->conn) || changed;
        if (changed) requestLayout();
      }
      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float h = 36.0F;
        m_dot->setSize(8.0F, 8.0F);
        m_dot->setRadius(4.0F);
        m_dot->setPosition(0.0F, 14.0F);
        for (Label* l : {m_name, m_type, m_conn}) l->measure(renderer);
        m_name->setPosition(20.0F, std::round((h - m_name->height()) / 2.0F));
        m_type->setPosition(180.0F, std::round((h - m_type->height()) / 2.0F));
        m_conn->setPosition(300.0F, std::round((h - m_conn->height()) / 2.0F));
        setSize(width, h);
        return h;
      }

    private:
      std::shared_ptr<Devices> m_devices;
      std::string m_dev;
      Box* m_dot = nullptr;
      Label* m_name = nullptr;
      Label* m_type = nullptr;
      Label* m_conn = nullptr;
    };

  } // namespace

  void buildNetwork(Column& page) {
    auto devices = std::make_shared<Devices>();
    std::weak_ptr<Devices> weak = devices;
    page.add<Poll>(5000, [weak]() { readDevices(weak); });
    // Rates and the sparkline update once a second, matching the system monitor's sampling.
    page.add<Poll>(1000, []() { refresh(); });

    {
      auto* g = page.add<Group>("Connection");
      g->add<Connection>();
      g->add<Spark>();
    }
    {
      auto* g = page.add<Group>("Devices");
      g->add<Repeater>(
          [devices]() {
            std::vector<std::string> keys;
            for (const auto& d : *devices) keys.push_back(d.dev);
            return keys;
          },
          [devices](const std::string& dev) -> std::unique_ptr<Item> { return std::make_unique<DeviceLine>(devices, dev); }
      );
      g->add<Chip>("Connection editor", 0xf06f3)->onClick([]() { spawn({"nm-connection-editor"}); });
    }
  }

} // namespace kusanagi::sp
