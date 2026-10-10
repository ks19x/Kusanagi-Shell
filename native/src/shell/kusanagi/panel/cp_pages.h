#pragma once

// The control panel's other tabs: Sound, System, Bluetooth, Network and Quick. Each lives only while its tab
// is shown, so whatever it polls or holds (sensors, Bluetooth discovery, nmcli, mullvad) stops with it.

#include "core/timer_manager.h"
#include "shell/kusanagi/dev_prompts.h"
#include "shell/kusanagi/panel/cp_page.h"
#include "ui/palette.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Box;
class CountdownRing;
class Graph;
class Label;

namespace kusanagi {

  namespace cp {
    class Card;
    class Chip;
    class Field;
    class IconButton;
    class ListRow;
    class Segmented;
    class Slider;
    class Switch;
  } // namespace cp

  // Sound

  class CpSound : public CpPage {
  public:
    CpSound(ControlPanel& panel, const ControlCenterServices& services);
    bool sync(Renderer& renderer) override;
    float layout(Renderer& renderer, float width) override;

  private:
    struct DeviceList {
      cp::Card* card = nullptr;
      Label* title = nullptr;
      std::vector<cp::ListRow*> rows;
      std::vector<std::uint32_t> ids;
      Label* empty = nullptr;
      cp::Slider* volume = nullptr;
      bool input = false;
    };
    void buildList(DeviceList& list, const char* title, bool input);
    bool syncList(DeviceList& list);
    float layoutList(Renderer& renderer, DeviceList& list, float width);

    DeviceList m_outputs;
    DeviceList m_inputs;
    cp::Card* m_apps = nullptr;
    Label* m_appsTitle = nullptr;
    Label* m_appsEmpty = nullptr;
    std::vector<cp::Slider*> m_appSliders;
    std::vector<std::uint32_t> m_appIds;
    std::vector<cp::Chip*> m_chips;
  };

  // System

  class CpSystem : public CpPage {
  public:
    CpSystem(ControlPanel& panel, const ControlCenterServices& services);
    ~CpSystem() override;
    bool sync(Renderer& renderer) override;
    float layout(Renderer& renderer, float width) override;
    void tick() override;

  private:
    struct Gauge {
      Node* node = nullptr;
      CountdownRing* track = nullptr;
      CountdownRing* arc = nullptr;
      std::array<Box*, 3> caps{};  // track start, track end, arc end
      Label* value = nullptr;
      Label* label = nullptr;
      Label* detail = nullptr;
      float shown = 0.0F;
      float target = -1.0F;
      float warnAt = 101.0F;
      float hotAt = 101.0F;
    };
    struct Usage {
      Label* label = nullptr;
      Label* text = nullptr;
      Box* track = nullptr;
      Box* fill = nullptr;
      float frac = 0.0F;
    };
    void buildGauge(Gauge& g, const std::string& label, float warnAt = 101.0F, float hotAt = 101.0F);
    void layoutGauge(Renderer& renderer, Gauge& g);
    void setGauge(Gauge& g, float value);
    void applyGauge(Gauge& g);
    void buildUsage(Usage& u, cp::Card* card, const std::string& label);
    void readHostInfo();

    cp::Card* m_gaugeCard = nullptr;
    std::array<Gauge, 4> m_gauges{};
    cp::Card* m_cpuCard = nullptr;
    Label* m_cpuTitle = nullptr;
    Label* m_load = nullptr;
    Graph* m_cpuSpark = nullptr;
    cp::Card* m_netCard = nullptr;
    Label* m_netTitle = nullptr;
    Label* m_netIf = nullptr;
    Label* m_downIcon = nullptr;
    Label* m_down = nullptr;
    Label* m_upIcon = nullptr;
    Label* m_up = nullptr;
    Graph* m_netSpark = nullptr;
    cp::Card* m_memCard = nullptr;
    Usage m_vram;
    Usage m_disk;
    Label* m_footer = nullptr;
    std::string m_host;
    std::string m_kernel;
    int m_threads = 0;
    // Read once a second while this page is shown.
    std::string m_gpuDev;  // /sys/class/drm/cardN/device of an amdgpu, or empty
    std::string m_gpuHwmon;
    double m_cpuGhz = 0.0;
    int m_gpuMhz = 0;
    int m_gpuTemp = -1;
    double m_vramUsed = 0.0;  // GiB
    double m_vramTotal = 0.0;
  };

  // Bluetooth

  class CpBluetooth : public CpPage {
  public:
    CpBluetooth(ControlPanel& panel, const ControlCenterServices& services);
    ~CpBluetooth() override;
    bool sync(Renderer& renderer) override;
    float layout(Renderer& renderer, float width) override;

  private:
    class DeviceRow;
    struct DeviceCard {
      cp::Card* card = nullptr;
      Label* title = nullptr;
      Label* empty = nullptr;
      std::vector<DeviceRow*> rows;
      std::vector<std::string> paths;
    };
    bool syncCard(DeviceCard& card, bool paired);
    float layoutCard(Renderer& renderer, DeviceCard& card, float width);
    // What a pairing device asks, if anything.
    [[nodiscard]] std::optional<dev::BtRequest> request() const;
    void answer(bool ok, const std::string& value = {});
    bool syncRequest();
    float layoutRequest(Renderer& renderer, float width);

    cp::Card* m_adapter = nullptr;
    Label* m_icon = nullptr;
    Label* m_name = nullptr;
    Label* m_status = nullptr;
    cp::IconButton* m_scan = nullptr;
    cp::Switch* m_switch = nullptr;
    DeviceCard m_mine;
    DeviceCard m_nearby;
    cp::Chip* m_settings = nullptr;
    bool m_weScan = false;

    cp::Card* m_req = nullptr;
    Label* m_reqTitle = nullptr;
    Label* m_reqCode = nullptr;
    Label* m_reqHint = nullptr;
    cp::Field* m_reqField = nullptr;
    cp::Chip* m_reqYes = nullptr;    // Pair
    cp::Chip* m_reqAllow = nullptr;  // authorize
    cp::Chip* m_reqNo = nullptr;     // Cancel
    cp::Chip* m_reqDone = nullptr;   // display
    std::string m_reqKey;  // kind + name + code of the request shown
  };

  // Network

  class CpNetwork : public CpPage {
  public:
    CpNetwork(ControlPanel& panel, const ControlCenterServices& services);
    bool sync(Renderer& renderer) override;
    float layout(Renderer& renderer, float width) override;
    void tick() override;
    bool dismissTransient() override;

  private:
    struct Net {
      std::string ssid;
      int signal = 0;
      bool secure = false;  // needs a password to join
      bool lock = false;    // show the lock icon
      bool active = false;
      bool known = false;
    };
    [[nodiscard]] std::vector<Net> networks() const;
    void connect(const Net& n);
    void join(const std::string& ssid, const std::optional<std::string>& password);
    void refreshVpn();
    void refreshDns();

    cp::Card* m_conn = nullptr;
    Label* m_connIcon = nullptr;
    Label* m_connTitle = nullptr;
    Label* m_connDetail = nullptr;
    Label* m_down = nullptr;
    Label* m_up = nullptr;
    Graph* m_spark = nullptr;

    cp::Card* m_wifi = nullptr;
    Label* m_wifiTitle = nullptr;
    cp::IconButton* m_rescan = nullptr;
    cp::Switch* m_wifiSwitch = nullptr;
    std::vector<cp::ListRow*> m_networks;
    std::vector<std::string> m_ssids;
    Label* m_wifiOff = nullptr;
    bool m_hasWifi = false;
    cp::Field* m_password = nullptr;  // inline password for a new secured network
    std::string m_askFor;
    std::string m_busy;               // the SSID being connected to
    bool m_busySawResolving = false;
    std::chrono::steady_clock::time_point m_busySince;
    Timer m_busyTimeout;

    cp::Card* m_vpn = nullptr;
    Label* m_vpnIcon = nullptr;
    Label* m_vpnName = nullptr;
    cp::Switch* m_vpnSwitch = nullptr;
    Label* m_vpnState = nullptr;
    Label* m_vpnWhere = nullptr;
    bool m_hasMullvad = false;
    std::string m_vpnStateText;
    std::string m_vpnWhereText;

    cp::Card* m_dns = nullptr;
    Label* m_dnsIcon = nullptr;
    Label* m_dnsTitle = nullptr;
    Label* m_dnsName = nullptr;
    Label* m_dnsUpstream = nullptr;
    bool m_dnsRunning = false;

    std::vector<cp::Chip*> m_chips;
    int m_ticks = 0;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);
  };

  // Quick

  class CpQuick : public CpPage {
  public:
    CpQuick(ControlPanel& panel, const ControlCenterServices& services);
    bool sync(Renderer& renderer) override;
    float layout(Renderer& renderer, float width) override;

  private:
    struct Swatch {
      InputArea* area = nullptr;
      Box* dot = nullptr;
      std::string color;
    };
    float flow(Renderer& renderer, std::vector<Node*> items, float y, float width, float spacing);

    Label* m_presetTitle = nullptr;
    std::vector<cp::Chip*> m_presets;
    Label* m_accentTitle = nullptr;
    cp::Chip* m_wallpaperAccent = nullptr;
    std::vector<Swatch> m_swatches;
    Label* m_barTitle = nullptr;
    cp::Segmented* m_barStyle = nullptr;
    Label* m_wsTitle = nullptr;
    std::vector<cp::Chip*> m_wsChips;
    Label* m_motionTitle = nullptr;
    cp::Segmented* m_motion = nullptr;
    InputArea* m_all = nullptr;
    Box* m_allBg = nullptr;
    Label* m_allIcon = nullptr;
    Label* m_allText = nullptr;
  };

} // namespace kusanagi
