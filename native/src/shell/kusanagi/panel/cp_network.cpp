#include "dbus/network/inetwork_service.h"
#include "render/core/renderer.h"
#include "shell/control_center/control_center_services.h"
#include "shell/kusanagi/panel/control_panel.h"
#include "shell/kusanagi/panel/cp_controls.h"
#include "shell/kusanagi/panel/cp_field.h"
#include "shell/kusanagi/panel/cp_pages.h"
#include "shell/panel/panel_manager.h"
#include "system/system_monitor_service.h"
#include "ui/builders.h"
#include "ui/controls/graph.h"
#include "ui/controls/label.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <sstream>

namespace kusanagi {

  namespace {
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

    // True when a Wi-Fi card exists, whether or not its radio is on.
    bool hasWifiCard() {
      std::error_code ec;
      for (const auto& e : std::filesystem::directory_iterator("/sys/class/net", ec)) {
        if (std::filesystem::exists(e.path() / "wireless", ec)) return true;
      }
      return false;
    }

    char32_t signalIcon(int signal) {
      static constexpr char32_t icons[] = {0xf092f, 0xf091f, 0xf0922, 0xf0925, 0xf0928};
      return icons[std::min(4, signal / 21)];
    }
  } // namespace

  CpNetwork::CpNetwork(ControlPanel& panel, const ControlCenterServices& services) : CpPage(panel, services) {
    // Connection
    m_conn = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    m_connIcon = static_cast<Label*>(m_conn->addChild(cp::icon(0xf0200, 26.0F)));
    m_connTitle = static_cast<Label*>(m_conn->addChild(cp::text("", 13.0F, true)));
    m_connDetail = static_cast<Label*>(m_conn->addChild(cp::text("", 11.0F, false, cp::dim())));
    m_down = static_cast<Label*>(m_conn->addChild(cp::text("", 13.0F, true)));
    m_up = static_cast<Label*>(m_conn->addChild(cp::text("", 11.0F, false, cp::dim())));
    m_spark = static_cast<Graph*>(m_conn->addChild(std::make_unique<Graph>()));
    m_spark->setColor(cp::accent());
    m_spark->setLineWidth(1.6F);
    m_spark->setFillOpacity(0.2F);

    // Wi-Fi, when there is a card
    m_hasWifi = hasWifiCard();
    m_wifi = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    m_wifi->setVisible(m_hasWifi);
    m_wifiTitle = static_cast<Label*>(m_wifi->addChild(cp::caption("WI-FI")));
    auto rescan = std::make_unique<cp::IconButton>(0xf0450);
    rescan->setOnActivate([this]() {
      if (dev::fakeWifi()) return dev::log("wifi: would rescan");
      if (m_services.network != nullptr) m_services.network->requestScan();
    });
    m_rescan = static_cast<cp::IconButton*>(m_wifi->addChild(std::move(rescan)));
    auto sw = std::make_unique<cp::Switch>();
    sw->setOnToggled([this](bool on) {
      if (dev::fakeWifi()) {
        dev::log(std::string("wifi: would switch the radio ") + (on ? "on" : "off"));
      } else if (m_services.network != nullptr) {
        m_services.network->setWirelessEnabled(on);
      }
      m_wifiSwitch->setOn(on, true);
    });
    m_wifiSwitch = static_cast<cp::Switch*>(m_wifi->addChild(std::move(sw)));
    m_wifiOff = static_cast<Label*>(m_wifi->addChild(cp::text("Wi-Fi is off", 12.0F, false, cp::dim())));
    // Password field for a new secured network, typed inline.
    auto pw = std::make_unique<cp::Field>(cp::Field::Look{}, 0xf033e);
    pw->setOnAccepted([this](const std::string& typed) {
      const std::string ssid = m_askFor;
      const std::string password = typed;  // `typed` is the field's own text, cleared below
      m_askFor.clear();
      m_password->setText("");
      join(ssid, password);
      m_panel.requestPageLayout();
    });
    m_password = static_cast<cp::Field*>(m_wifi->addChild(std::move(pw)));
    m_password->setVisible(false);

    // Mullvad and DNS
    m_vpn = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    m_vpn->setVisible(false);
    m_vpnIcon = static_cast<Label*>(m_vpn->addChild(cp::icon(0xf099d, 20.0F, cp::dim())));
    m_vpnName = static_cast<Label*>(m_vpn->addChild(cp::text("Mullvad", 13.0F, true)));
    auto vsw = std::make_unique<cp::Switch>();
    vsw->setOnToggled([this](bool on) {
      m_vpnStateText = on ? "connecting" : "disconnecting";
      m_vpnSwitch->setOn(on, true);
      cp::capture({"mullvad", on ? "connect" : "disconnect"}, m_alive, [this](std::string) { refreshVpn(); });
      PanelManager::instance().refresh();
    });
    m_vpnSwitch = static_cast<cp::Switch*>(m_vpn->addChild(std::move(vsw)));
    m_vpnState = static_cast<Label*>(m_vpn->addChild(cp::text("", 11.0F, true, cp::dim())));
    m_vpnWhere = static_cast<Label*>(m_vpn->addChild(cp::text("", 10.0F, false, cp::dim())));
    m_vpnWhere->setMaxLines(1);

    m_dns = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    m_dnsIcon = static_cast<Label*>(m_dns->addChild(cp::icon(0xf0483, 20.0F, cp::danger())));
    m_dnsTitle = static_cast<Label*>(m_dns->addChild(cp::text("DNS", 13.0F, true)));
    m_dnsName = static_cast<Label*>(m_dns->addChild(cp::text("", 11.0F, true)));
    m_dnsName->setMaxLines(1);
    m_dnsUpstream = static_cast<Label*>(m_dns->addChild(cp::text("", 10.0F, false, cp::dim())));
    m_dnsUpstream->setMaxLines(1);

    auto settings = std::make_unique<cp::Chip>("Network settings", 0xf0493);
    settings->setOnActivate([this]() { m_panel.ipcClosed("settings-open"); });
    m_chips.push_back(static_cast<cp::Chip*>(addChild(std::move(settings))));
    auto editor = std::make_unique<cp::Chip>("Connection editor", 0xf06f3);
    editor->setOnActivate([this]() { m_panel.runClosed({"nm-connection-editor"}); });
    m_chips.push_back(static_cast<cp::Chip*>(addChild(std::move(editor))));

    refreshVpn();
    refreshDns();
  }

  std::vector<CpNetwork::Net> CpNetwork::networks() const {
    std::vector<Net> out;
    if (dev::fakeWifi()) {
      for (const auto& f : dev::fakeNetworks()) out.push_back({f.ssid, f.signal, f.secure, f.secure, f.active, f.known});
    } else if (m_services.network != nullptr) {
      for (const auto& ap : m_services.network->accessPoints()) {
        if (ap.ssid.empty() || std::ranges::any_of(out, [&ap](const Net& n) { return n.ssid == ap.ssid; })) continue;
        out.push_back({ap.ssid, ap.strength, ap.requiresCredentials(), ap.secured, ap.active,
                       m_services.network->hasSavedConnection(ap.ssid)});
      }
    }
    std::ranges::stable_sort(out, [](const Net& a, const Net& b) {
      if (a.active != b.active) return a.active;
      return a.signal > b.signal;
    });
    if (out.size() > 8) out.resize(8);
    return out;
  }

  // A saved network comes up, a new secured one asks for its password here, and an open one just joins.
  void CpNetwork::connect(const Net& n) {
    if (n.active) return;
    if (n.known) {
      join(n.ssid, std::nullopt);
    } else if (n.secure) {
      m_askFor = n.ssid;
      m_password->setText("");
    } else {
      join(n.ssid, std::nullopt);
    }
    PanelManager::instance().refresh();
    m_panel.requestPageLayout();
  }

  void CpNetwork::join(const std::string& ssid, const std::optional<std::string>& password) {
    m_busy = ssid;
    m_busySawResolving = false;
    m_busySince = std::chrono::steady_clock::now();
    // Don't show "connecting" forever if NetworkManager never says how it went (nmcli waits 90 s).
    m_busyTimeout.start(std::chrono::seconds(dev::fakeWifi() ? 2 : 90), [this]() {
      m_busy.clear();
      PanelManager::instance().refresh();
    });
    if (dev::fakeWifi()) {
      // In a test shell, only log what would run.
      dev::log("wifi: would run nmcli " + std::string(password ? "device wifi connect " : "connection up id ") + "\""
               + ssid + "\"" + (password ? " password <" + std::to_string(password->size()) + " characters>" : ""));
      return;
    }
    if (m_services.network == nullptr) return;
    for (const auto& ap : m_services.network->accessPoints()) {
      if (ap.ssid != ssid || ap.active) continue;
      if (password) {
        (void)m_services.network->activateAccessPoint(ap, *password);
      } else {
        (void)m_services.network->activateAccessPoint(ap);
      }
      return;
    }
  }

  bool CpNetwork::dismissTransient() {
    if (m_askFor.empty()) return false;
    m_askFor.clear();
    m_password->setText("");
    PanelManager::instance().refresh();
    m_panel.requestPageLayout();
    return true;
  }

  void CpNetwork::refreshVpn() {
    cp::capture({"sh", "-c", "command -v mullvad >/dev/null && mullvad status --json"}, m_alive, [this](std::string out) {
      const bool has = out.find_first_not_of(" \n\t") != std::string::npos;
      m_hasMullvad = has;
      if (has) {
        try {
          const auto s = nlohmann::json::parse(out);
          m_vpnStateText = s.value("state", std::string("error"));
          m_vpnWhereText.clear();
          if (s.contains("details") && s["details"].is_object() && s["details"].contains("location")) {
            const auto& l = s["details"]["location"];
            std::string where;
            for (const char* k : {"city", "country"}) {
              const std::string v = l.value(k, std::string());
              if (!v.empty()) where += (where.empty() ? "" : ", ") + v;
            }
            const std::string ip = l.value("ipv4", std::string());
            m_vpnWhereText = where + (ip.empty() ? "" : "  ·  " + ip);
          }
        } catch (...) {
          m_vpnStateText = "error";
        }
      }
      PanelManager::instance().refresh();
    });
  }

  void CpNetwork::refreshDns() {
    // Read-only: the resolver is a system service, and switching it off would leave you without DNS.
    cp::capture({"sh", "-c",
                 "grep -m1 '^nameserver' /etc/resolv.conf | cut -d' ' -f2; pgrep -x nextdns >/dev/null && echo nextdns; "
                 "grep -m1 '^forwarder' /etc/nextdns.conf 2>/dev/null | cut -d' ' -f2"},
                m_alive, [this](std::string out) {
                  std::vector<std::string> lines;
                  std::istringstream in(out);
                  for (std::string l; std::getline(in, l);) lines.push_back(l);
                  const std::string first = lines.empty() ? "" : lines[0];
                  const bool local = first == "127.0.0.1" || first == "::1";
                  m_dnsRunning = std::ranges::find(lines, "nextdns") != lines.end();
                  m_dnsName->setText(m_dnsRunning ? "NextDNS (encrypted)"
                                     : local      ? "Local resolver (not running!)"
                                                  : (first.empty() ? std::string("unknown") : first));
                  std::string fwd;
                  for (const auto& l : lines) {
                    if (l.starts_with("https://")) fwd = l;
                  }
                  m_dnsUpstream->setText(fwd.find("1.1.1.1") != std::string::npos ? "→ Cloudflare over HTTPS"
                                         : !fwd.empty() ? "→ " + fwd.substr(0, fwd.find(','))
                                                        : "");
                  m_dnsIcon->setColor(m_dnsRunning ? cp::accent() : cp::danger());
                  PanelManager::instance().refresh();
                  m_panel.requestPageLayout();
                });
  }

  void CpNetwork::tick() {
    // Poll Mullvad every 3 s while this tab is open.
    if (++m_ticks % 3 == 0) refreshVpn();
  }

  bool CpNetwork::sync(Renderer& /*renderer*/) {
    bool changed = false;
    const auto set = [&changed](Label* l, const std::string& s) { changed = l->setText(s) || changed; };
    const NetworkState* st = m_services.network != nullptr ? &m_services.network->state() : nullptr;
    const bool up = st != nullptr && st->connected;
    const bool wifi = st != nullptr && st->kind == NetworkConnectivity::Wireless;
    set(m_connIcon, cp::utf8(!up ? 0xf05aa : wifi ? 0xf05a9 : 0xf0200));
    m_connIcon->setColor(up ? cp::accent() : cp::dim());
    set(m_connTitle, up ? std::string(wifi ? "Wi-Fi" : "Ethernet") + " · connected" : "Offline");
    set(m_connDetail, up ? st->interfaceName + "   ·   " + (st->ipv4.empty() ? "…" : st->ipv4) : "no default route");
    if (m_services.sysmon != nullptr) {
      const SystemStats s = m_services.sysmon->latest();
      set(m_down, "↓ " + rate(s.netRxBytesPerSec));
      set(m_up, "↑ " + rate(s.netTxBytesPerSec));
      const auto hist = m_services.sysmon->history(60);
      double top = 1.0;
      for (const auto& h : hist) top = std::max(top, h.netRxBytesPerSec + h.netTxBytesPerSec);
      std::vector<float> v;
      for (const auto& h : hist) v.push_back(static_cast<float>((h.netRxBytesPerSec + h.netTxBytesPerSec) / (top * 1.15)));
      m_spark->setValues(std::move(v));
    }

    // Wi-Fi networks: the active one on top, then strongest first, 8 at most.
    if (m_hasWifi || dev::fakeWifi()) {
      if (!m_wifi->visible()) changed = true;
      m_wifi->setVisible(true);
      const bool on = dev::fakeWifi() || (st != nullptr && st->wirelessEnabled);
      m_wifiSwitch->setOn(on, true);
      if (m_rescan->visible() != on) changed = true;
      m_rescan->setVisible(on);
      const std::vector<Net> nets = on ? networks() : std::vector<Net>{};
      // The connect finished: the network is up, or NetworkManager stopped trying.
      if (!m_busy.empty() && !dev::fakeWifi() && st != nullptr) {
        const bool joined = std::ranges::any_of(nets, [this](const Net& n) { return n.ssid == m_busy && n.active; });
        if (st->resolving) m_busySawResolving = true;
        // NetworkManager never started, which is a quick failure the shell reports with a notification.
        const bool idle = !st->resolving && std::chrono::steady_clock::now() - m_busySince > std::chrono::seconds(4);
        if (joined || (m_busySawResolving && !st->resolving) || idle) {
          m_busy.clear();
          m_busyTimeout.stop();
        }
      }
      std::vector<std::string> ssids;
      for (const auto& n : nets) ssids.push_back(n.ssid);
      if (ssids != m_ssids) {
        for (cp::ListRow* r : m_networks) (void)m_wifi->removeChild(r);
        m_networks.clear();
        for (const auto& ssid : ssids) {
          cp::ListRow::Look look{.height = 32.0F, .iconPx = 15.0F, .trailingPx = 12.0F,
                                 .iconColor = cp::textA(1.0F), .trailingColor = cp::dim(), .trailingOnlySelected = false};
          auto row = std::make_unique<cp::ListRow>(look);
          row->setOnActivate([this, ssid]() {
            for (const auto& n : networks()) {
              if (n.ssid == ssid) {
                connect(n);
                break;
              }
            }
          });
          m_networks.push_back(static_cast<cp::ListRow*>(m_wifi->addChild(std::move(row))));
        }
        m_ssids = ssids;
        changed = true;
      }
      for (std::size_t i = 0; i < nets.size(); ++i) {
        const std::string label = nets[i].ssid + (m_busy == nets[i].ssid ? "  · connecting…" : "");
        changed = m_networks[i]->set(signalIcon(nets[i].signal), label, nets[i].lock ? 0xf033e : 0, nets[i].active)
            || changed;
      }
      const bool ask = !m_askFor.empty();
      if (m_password->visible() != ask) changed = true;
      m_password->setVisible(ask);
      m_password->setPlaceholder("Password for " + m_askFor);
      if (m_wifiOff->visible() == on) changed = true;
      m_wifiOff->setVisible(!on);
    }

    // Mullvad
    if (m_vpn->visible() != m_hasMullvad) changed = true;
    m_vpn->setVisible(m_hasMullvad);
    if (m_hasMullvad) {
      const bool on = m_vpnStateText == "connected";
      const bool moving = m_vpnStateText == "connecting" || m_vpnStateText == "disconnecting";
      set(m_vpnIcon, cp::utf8(on ? 0xf0582 : 0xf099d));
      m_vpnIcon->setColor(on ? colorSpecFromRole(ColorRole::Tertiary) : cp::dim());
      m_vpnSwitch->setOn(on || m_vpnStateText == "connecting", true);
      set(m_vpnState, moving ? m_vpnStateText + "…" : on ? "Protected" : "Not protected");
      m_vpnState->setColor(on ? colorSpecFromRole(ColorRole::Tertiary) : cp::dim());
      set(m_vpnWhere, m_vpnWhereText);
    }
    return changed;
  }

  float CpNetwork::layout(Renderer& renderer, float width) {
    float y = 0.0F;
    // Connection
    m_conn->setPosition(0.0F, y);
    m_conn->setSize(width, 136.0F);
    m_connIcon->measure(renderer);
    m_connIcon->setPosition(14.0F, 16.0F);
    const float tx = 14.0F + m_connIcon->width() + 12.0F;
    m_connTitle->measure(renderer);
    m_connDetail->measure(renderer);
    m_connTitle->setPosition(tx, 14.0F);
    m_connDetail->setPosition(tx, 14.0F + m_connTitle->height() + 2.0F);
    m_down->measure(renderer);
    m_up->measure(renderer);
    m_down->setPosition(width - 14.0F - m_down->width(), 14.0F);
    m_up->setPosition(width - 14.0F - m_up->width(), 14.0F + m_down->height() + 2.0F);
    m_spark->setPosition(12.0F, 62.0F);
    m_spark->setSize(width - 24.0F, 62.0F);
    m_spark->layout(renderer);
    y += 136.0F;

    if (m_wifi->visible()) {
      y += 12.0F;
      const float inner = width - 24.0F;
      float wy = 12.0F;
      // Header: title on the left, rescan and the switch on the right.
      m_wifiTitle->measure(renderer);
      m_wifiTitle->setPosition(12.0F, wy + std::round((24.0F - m_wifiTitle->height()) / 2.0F));
      float rx = 12.0F + inner - m_wifiSwitch->width();
      m_wifiSwitch->setPosition(rx, wy + 1.0F);
      if (m_rescan->visible()) {
        rx -= 6.0F + 34.0F;
        m_rescan->setSize(34.0F, 34.0F);
        m_rescan->setPosition(rx, wy - 5.0F);
        m_rescan->layout(renderer);
      }
      wy += 24.0F;
      for (cp::ListRow* r : m_networks) {
        wy += 4.0F;
        r->setPosition(12.0F, wy);
        r->setSize(inner, 32.0F);
        r->layout(renderer);
        wy += 32.0F;
      }
      if (m_password->visible()) {
        wy += 4.0F;
        m_password->setPosition(12.0F, wy);
        m_password->setSize(inner, 34.0F);
        m_password->layout(renderer);
        wy += 34.0F;
      }
      if (m_wifiOff->visible()) {
        m_wifiOff->measure(renderer);
        wy += 4.0F;
        m_wifiOff->setPosition(12.0F, wy);
        wy += m_wifiOff->height();
      }
      m_wifi->setPosition(0.0F, y);
      m_wifi->setSize(width, wy + 12.0F);
      y += wy + 12.0F;
    }

    // VPN and DNS
    y += 12.0F;
    const float half = std::round((width - 12.0F) / 2.0F);
    const auto smallCard = [&renderer](cp::Card* card, Label* icon, Label* title, float x, float top, float w) {
      card->setPosition(x, top);
      card->setSize(w, 92.0F);
      icon->measure(renderer);
      icon->setPosition(14.0F, 14.0F);
      title->measure(renderer);
      title->setPosition(14.0F + icon->width() + 8.0F, 14.0F + std::round((icon->height() - title->height()) / 2.0F));
    };
    float dx = 0.0F;
    float dw = width;
    if (m_vpn->visible()) {
      smallCard(m_vpn, m_vpnIcon, m_vpnName, 0.0F, y, half);
      m_vpnSwitch->setPosition(half - 12.0F - m_vpnSwitch->width(),
                               14.0F + std::round((m_vpnIcon->height() - m_vpnSwitch->height()) / 2.0F));
      m_vpnState->setMaxWidth(half - 28.0F);
      m_vpnState->measure(renderer);
      m_vpnState->setPosition(14.0F, 46.0F);
      m_vpnWhere->setMaxWidth(half - 28.0F);
      m_vpnWhere->measure(renderer);
      m_vpnWhere->setPosition(14.0F, 64.0F);
      dx = width - half;
      dw = half;
    }
    smallCard(m_dns, m_dnsIcon, m_dnsTitle, dx, y, dw);
    m_dnsName->setMaxWidth(dw - 28.0F);
    m_dnsName->measure(renderer);
    m_dnsName->setPosition(14.0F, 46.0F);
    m_dnsUpstream->setMaxWidth(dw - 28.0F);
    m_dnsUpstream->measure(renderer);
    m_dnsUpstream->setPosition(14.0F, 64.0F);
    y += 92.0F + 12.0F;

    float x = 0.0F;
    float rowH = 0.0F;
    for (cp::Chip* c : m_chips) {
      c->layout(renderer);
      c->setPosition(x, y);
      x += c->width() + 8.0F;
      rowH = std::max(rowH, c->height());
    }
    return y + rowH;
  }

} // namespace kusanagi
