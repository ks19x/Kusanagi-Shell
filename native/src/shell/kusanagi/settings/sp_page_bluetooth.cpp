// Settings > Bluetooth: power, visibility, pairing requests, paired and nearby devices, and what's missing
// when there's no adapter. Scans while the page is open.
// In a test shell (tools/kdev/kt) the page never scans with the real adapter; `dev-prompt bt on` fakes one.

#include "cursor-shape-v1-client-protocol.h"
#include "dbus/bluetooth/bluetooth_service.h"
#include "render/core/renderer.h"
#include "shell/control_center/control_center_services.h"
#include "shell/kusanagi/bt_request.h"
#include "shell/kusanagi/dev_prompts.h"
#include "shell/kusanagi/game_mode.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace kusanagi::sp {

  namespace {

    // The real service, unless a test shell fakes the adapter.
    BluetoothService* service() { return dev::fakeBluetooth() ? nullptr : services().bluetooth; }

    // The adapter counts as unavailable while bluetooth.enabled is off.
    bool enabled() { return get<bool>("bluetooth.enabled", true); }
    bool available() {
      if (!enabled()) return false;
      if (dev::fakeBluetooth()) return true;
      return service() != nullptr && service()->state().adapterPresent;
    }
    BluetoothState btState() {
      BluetoothState st = service() != nullptr ? service()->state() : BluetoothState{};
      if (dev::fakeBluetooth()) {
        st.adapterPresent = true;
        st.powered = true;
      }
      return st;
    }
    bool blocked() { return available() && (btState().rfkillSoftBlocked || btState().rfkillHardBlocked); }
    bool on() { return available() && btState().powered; }
    bool scanning() { return on() && btState().discovering; }

    std::vector<BluetoothDeviceInfo> devices() {
      if (!available() || service() == nullptr) return {};
      return service()->devices();
    }
    std::string nameOf(const BluetoothDeviceInfo& d) { return d.alias.empty() ? d.address : d.alias; }

    // Paired devices: connected first, then connecting, then the rest, each by name.
    std::vector<BluetoothDeviceInfo> paired() {
      std::vector<BluetoothDeviceInfo> out;
      for (auto& d : devices()) {
        if (d.paired) out.push_back(d);
      }
      const auto rank = [](const BluetoothDeviceInfo& d) { return d.connected ? 0 : d.connecting ? 1 : 2; };
      std::ranges::stable_sort(out, [&rank](const auto& a, const auto& b) {
        return rank(a) != rank(b) ? rank(a) < rank(b) : nameOf(a) < nameOf(b);
      });
      return out;
    }
    // Unpaired devices that have a real name.
    std::vector<BluetoothDeviceInfo> nearby() {
      std::vector<BluetoothDeviceInfo> out;
      for (auto& d : devices()) {
        if (!d.paired && !d.alias.empty() && d.alias != d.address) out.push_back(d);
      }
      std::ranges::stable_sort(out, [](const auto& a, const auto& b) { return nameOf(a) < nameOf(b); });
      return out;
    }

    std::string summary() {
      if (!available()) return "No adapter";
      if (blocked()) return "Blocked";
      if (!on()) return "Off";
      std::vector<std::string> connected;
      for (const auto& d : devices()) {
        if (d.connected) connected.push_back(nameOf(d));
      }
      if (connected.size() == 1) return connected.front();
      if (connected.size() > 1) return std::to_string(connected.size()) + " devices";
      return "On";
    }

    char32_t deviceIcon(BluetoothDeviceKind k) {
      switch (k) {
      case BluetoothDeviceKind::Headset:
      case BluetoothDeviceKind::Headphones:
      case BluetoothDeviceKind::Earbuds: return 0xf02cb;
      case BluetoothDeviceKind::Speaker: return 0xf04c3;
      case BluetoothDeviceKind::Mouse: return 0xf037d;
      case BluetoothDeviceKind::Keyboard: return 0xf030c;
      case BluetoothDeviceKind::Gamepad: return 0xf0297;
      case BluetoothDeviceKind::Phone: return 0xf011c;
      case BluetoothDeviceKind::Computer: return 0xf0322;
      case BluetoothDeviceKind::Watch: return 0xf0598;
      default: return 0xf00af;
      }
    }

    std::string deviceStatus(const BluetoothDeviceInfo& d) {
      if (d.connecting) return "Connecting…";
      if (d.connected) return d.hasBattery ? "Connected · " + std::to_string(d.batteryPercent) + "%" : "Connected";
      return d.paired ? "Not connected" : "Tap to pair";
    }

    std::optional<dev::BtRequest> pending() { return bt::request(service(), services().bluetoothAgent); }

    // Click connects or disconnects a paired device, or pairs a new one. Hover shows Forget.
    class DeviceRow : public Item {
    public:
      explicit DeviceRow(std::string path) : m_path(std::move(path)) {
        m_bg = static_cast<Box*>(addChild(ui::box({})));
        m_icon = static_cast<Label*>(addChild(makeIcon(0xf00af, 17.0F)));
        m_name = static_cast<Label*>(addChild(makeText("", 12.0F)));
        m_name->setMaxLines(1);
        m_name->setEllipsize(TextEllipsize::End);
        m_status = static_cast<Label*>(addChild(makeText("", 10.0F, false, dim())));
        m_status->setMaxLines(1);
        m_status->setEllipsize(TextEllipsize::End);
        m_forget = static_cast<IconButton*>(addChild(std::make_unique<IconButton>(
            0xf0a7a,
            [this]() {
              if (BluetoothService* bt = service()) bt->forget(m_path);
            },
            28.0F, 14.0F
        )));
        m_forget->setVisible(false);
        setCursorShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
        setOnEnter([this](const PointerData&) { restyle(true); });
        setOnLeave([this]() { restyle(true); });
        m_forget->setOnEnter([this](const PointerData&) { restyle(true); });
        m_forget->setOnLeave([this]() { restyle(true); });
        setOnClick([this](const PointerData&) {
          BluetoothService* bt = service();
          if (bt == nullptr) return;
          if (m_known) {
            m_live || m_connecting ? (void)bt->disconnectDevice(m_path) : (void)bt->connect(m_path);
          } else {
            (void)bt->pair(m_path);
          }
        });
        sync();
      }

      void sync() override {
        const auto all = devices();
        const auto it = std::ranges::find_if(all, [this](const BluetoothDeviceInfo& d) { return d.path == m_path; });
        if (it == all.end()) return;
        m_known = it->paired;
        m_live = it->connected;
        m_connecting = it->connecting;
        bool changed = m_icon->setText(utf8Of(deviceIcon(it->kind)));
        changed = m_name->setText(nameOf(*it)) || changed;
        changed = m_status->setText(deviceStatus(*it)) || changed;
        m_name->setFontWeight(m_live ? FontWeight::Bold : FontWeight::Normal);
        restyle(!m_first);
        m_first = false;
        if (changed) requestLayout();
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float h = 40.0F;
        m_bg->setSize(width, h);
        m_bg->setRadius(std::max(6.0F, kusanagi::radius() - 8.0F));
        m_icon->measure(renderer);
        m_icon->setPosition(10.0F, std::round((h - m_icon->height()) / 2.0F));
        m_forget->place(renderer, 28.0F);
        m_forget->setPosition(width - 6.0F - 28.0F, 6.0F);
        // The text ends 8 px before the Forget button's slot, which is 0 wide while hidden.
        const float side = m_forget->visible() ? 28.0F : 0.0F;
        const float x = 10.0F + m_icon->width() + 10.0F;
        const float tw = std::max(1.0F, width - 6.0F - side - 8.0F - x);
        m_name->setMaxWidth(tw);
        m_status->setMaxWidth(tw);
        m_name->measure(renderer);
        m_status->measure(renderer);
        const float colH = m_name->height() + 1.0F + m_status->height();
        const float y = std::round((h - colH) / 2.0F);
        m_name->setPosition(x, y);
        m_status->setPosition(x, y + m_name->height() + 1.0F);
        setSize(width, h);
        return h;
      }

    private:
      void restyle(bool animate) {
        const bool hover = hovered() || m_forget->hovered();
        const ColorSpec bg = m_live ? accent(hover ? 0.22F : 0.16F) : textA(hover ? 0.06F : 0.0F);
        if (animate) {
          tweenColor(*m_bg, m_bgColor, bg, 160, [this](const ColorSpec& c) { m_bg->setFill(c); });
        } else {
          m_bg->setFill(bg);
        }
        m_bgColor = bg;
        m_icon->setColor(m_live ? accent() : textA(1.0F));
        const bool showForget = hover && m_known;
        if (showForget != m_forget->visible()) {
          m_forget->setVisible(showForget);
          requestLayout();
        }
      }

      std::string m_path;
      Box* m_bg = nullptr;
      Label* m_icon = nullptr;
      Label* m_name = nullptr;
      Label* m_status = nullptr;
      IconButton* m_forget = nullptr;
      ColorSpec m_bgColor;
      bool m_known = false;
      bool m_live = false;
      bool m_connecting = false;
      bool m_first = true;
    };

    // Card with an accent outline, shown while a device asks for something during pairing.
    class RequestCard : public Item {
    public:
      RequestCard() {
        m_card = static_cast<Box*>(addChild(ui::box({})));
        m_col = static_cast<Column*>(addChild(std::make_unique<Column>(10.0F)));
        m_title = m_col->add<Text>("", TextOpts{.bold = true, .wrap = true});
        m_code = m_col->add<Text>("", TextOpts{.px = 26.0F, .bold = true, .color = accent(), .letterSpacing = 4.0F});
        m_hint = m_col->add<Text>("Check it's the same number on the device.", TextOpts{.px = 11.0F, .color = dim(), .wrap = true});
        m_field = m_col->add<Field>(std::nullopt, FieldOpts{
                                                      .icon = 0xf0306,
                                                      .onAccepted = [](const std::string& t) {
                                                        bt::answer(service(), services().bluetoothAgent, true, t);
                                                        refresh();
                                                      },
                                                  });
        m_field->setFixedWidth(0.0F); // Full width.
        auto* row = m_col->add<HRow>(8.0F);
        m_yes = row->add<Chip>("Pair", 0xf012c);
        m_yes->onWhen([]() { return true; })->onClick([this]() {
          bt::answer(service(), services().bluetoothAgent, true, m_field->text());
          refresh();
        });
        m_no = row->add<Chip>("Cancel", 0xf0156);
        m_no->onClick([]() {
          bt::answer(service(), services().bluetoothAgent, false);
          refresh();
        });
        showIf([]() { return pending().has_value(); });
        sync();
      }

      void sync() override {
        const auto r = pending();
        if (!r) {
          m_key.clear();
          return;
        }
        const std::string key = r->kind + "\n" + r->name + "\n" + r->code;
        if (key == m_key) return;
        m_key = key;
        const std::string& k = r->kind;
        m_title->setText(k == "confirm"     ? "Pair with " + r->name + "?"
                         : k == "display"   ? "Type this on " + r->name + ", then press Enter there"
                         : k == "authorize" ? "Let " + r->name + " connect?"
                                            : r->name + " wants a " + (k == "pin" ? "PIN" : "passkey"));
        m_code->setText(r->code);
        m_code->showIf([k]() { return k == "confirm" || k == "display"; });
        m_hint->showIf([k]() { return k == "confirm"; });
        m_field->showIf([k]() { return k == "pin" || k == "passkey"; });
        m_field->setText("");
        m_field->setPlaceholder(k == "pin" ? "PIN (often 0000)" : "Passkey (numbers)");
        m_yes->showIf([k]() { return k != "display"; });
        m_yes->labelFrom([k]() { return std::string(k == "authorize" ? "Allow" : "Pair"); });
        m_no->labelFrom([k]() { return std::string(k == "display" ? "Done" : "Cancel"); });
        requestLayout();
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float colH = m_col->place(renderer, std::max(1.0F, width - 28.0F));
        m_col->setPosition(14.0F, 12.0F);
        const float h = colH + 24.0F;
        m_card->setSize(width, h);
        m_card->setRadius(std::max(6.0F, kusanagi::radius() - 6.0F));
        m_card->setFill(textA(0.045F));
        m_card->setBorder(accent(0.6F), 1.0F);
        setSize(width, h);
        return h;
      }

    private:
      Box* m_card = nullptr;
      Column* m_col = nullptr;
      Text* m_title = nullptr;
      Text* m_code = nullptr;
      Text* m_hint = nullptr;
      Field* m_field = nullptr;
      Chip* m_yes = nullptr;
      Chip* m_no = nullptr;
      std::string m_key;
    };

    // Scans while the page is open, if bluetooth.autoScan is on and game mode isn't keeping things quiet.
    struct Scan {
      bool weScan = false;
      std::optional<bool> wanted; // Acts only when this changes.
      ~Scan() {
        BluetoothService* bt = service();
        if (weScan && bt != nullptr && bt->state().discovering) bt->stopDiscovery();
      }
      void sync() {
        BluetoothService* bt = service();
        // A test shell shares the real system bus, so never scan with the real adapter there.
        if (bt == nullptr || dev::enabled()) return;
        const GameModeService* gm = GameModeService::instance();
        const bool want = on() && get<bool>("bluetooth.autoScan", true) && !(gm != nullptr && gm->quiet());
        if (wanted == want) return;
        wanted = want;
        if (want && !bt->state().discovering) {
          bt->startDiscovery();
          weScan = true;
        } else if (!want && weScan) {
          if (bt->state().discovering) bt->stopDiscovery();
          weScan = false;
        }
      }
    };

    struct Page {
      std::string why; // "nobluez", "nodaemon" or "noadapter".
      bool asked = false;
      Scan scan;
    };

  } // namespace

  void buildBluetooth(Column& page) {
    auto st = std::make_shared<Page>();
    std::weak_ptr<Page> weak = st;

    // BlueZ state changes on its own (devices found, connects, battery), so poll it.
    struct Seen {
      BluetoothState state;
      std::vector<BluetoothDeviceInfo> devices;
      std::string request;
    };
    auto seen = std::make_shared<Seen>();
    page.add<Poll>(250, [weak, seen]() {
      auto s = weak.lock();
      if (!s) return;
      s->scan.sync();
      // No adapter: find out once whether the daemon, the package or the hardware is missing.
      if (enabled() && !available() && !s->asked) {
        s->asked = true;
        run({"sh", "-c",
             "command -v bluetoothd >/dev/null || [ -x /usr/libexec/bluetooth/bluetoothd ] || [ -x /usr/lib/bluetooth/bluetoothd ] || { echo nobluez; exit; }; "
             "pgrep -x bluetoothd >/dev/null || { echo nodaemon; exit; }; echo noadapter"},
            [weak](const std::string& out, int) {
              if (auto p = weak.lock()) {
                p->why = out.substr(0, out.find('\n'));
                refresh();
              }
            });
      }
      const auto r = pending();
      Seen now{btState(), devices(), r ? r->kind + r->name + r->code : std::string()};
      if (now.state == seen->state && now.devices == seen->devices && now.request == seen->request) return;
      *seen = std::move(now);
      refresh();
    });

    {
      auto* g = page.add<Group>("Bluetooth");
      g->bindHint([]() {
        return available() ? (btState().adapterName.empty() ? std::string("Adapter") : btState().adapterName) + "  ·  " + summary()
                           : std::string();
      });
      g->add<Row>("Use Bluetooth in Kusanagi", "Off: no tab, tile or bar module — Kusanagi leaves Bluetooth alone.",
                  std::make_unique<Switch>(sp::bind("bluetooth.enabled")));
      g->add<Text>("", TextOpts{.color = dim(), .wrap = true})
          ->bindText([st]() -> std::string {
            if (st->why == "nobluez") return "BlueZ (the Bluetooth service) isn't installed.";
            if (st->why == "nodaemon") return "The Bluetooth service (bluetoothd) isn't running.";
            if (st->why == "noadapter") return "No Bluetooth adapter found — this computer may not have one (a USB dongle works).";
            return "Looking…";
          })
          ->showIf([]() { return enabled() && !available(); });
      g->add<Chip>("Set up Bluetooth", 0xf0493)
          ->onWhen([]() { return true; })
          ->onClick([]() { spawn({"kusanagi", "bluetooth", "setup"}); })
          ->showIf([st]() { return enabled() && !available() && (st->why == "nobluez" || st->why == "nodaemon"); });
      g->add<Row>("On", std::make_unique<Switch>(Binding{
                            .get = []() -> json { return on(); },
                            .set = [](const json& v) {
                              // setPowered unblocks rfkill first.
                              if (BluetoothService* bt = service()) bt->setPowered(truthy(v));
                              else if (dev::fakeBluetooth()) dev::log(std::string("bt: would switch it ") + (truthy(v) ? "on" : "off"));
                            },
                        }))
          ->bindHint([]() { return blocked() ? std::string("Blocked by a switch (rfkill) — turning it on unblocks it.") : std::string(); })
          ->showIf([]() { return available(); });
      g->add<Row>("Visible to other devices", "So a phone or another computer can find this one.",
                  std::make_unique<Switch>(Binding{
                      .get = []() -> json { return on() && btState().discoverable; },
                      .set = [](const json& v) {
                        if (BluetoothService* bt = service()) bt->setDiscoverable(truthy(v));
                      },
                  }))
          ->showIf([]() { return on(); });
      g->add<Row>("Look for devices while open", "Scans only while the Bluetooth tab or this page is open.",
                  std::make_unique<Switch>(sp::bind("bluetooth.autoScan")))
          ->showIf([]() { return available(); });
    }

    page.add<RequestCard>();

    {
      auto* g = page.add<Group>("My devices", "Click to connect or disconnect. Hover for Forget.");
      g->showIf([]() { return on(); });
      g->add<Repeater>(
          []() {
            std::vector<std::string> keys;
            for (const auto& d : paired()) keys.push_back(d.path);
            return keys;
          },
          [](const std::string& path) -> std::unique_ptr<Item> { return std::make_unique<DeviceRow>(path); }
      );
      g->add<Text>("Nothing paired yet.", TextOpts{.color = dim()})->showIf([]() { return paired().empty(); });
    }

    {
      auto* g = page.add<Group>("Nearby", "Put the device in pairing mode, then click it.");
      g->showIf([]() { return on(); });
      g->add<Repeater>(
          []() {
            std::vector<std::string> keys;
            for (const auto& d : nearby()) keys.push_back(d.path);
            return keys;
          },
          [](const std::string& path) -> std::unique_ptr<Item> { return std::make_unique<DeviceRow>(path); }
      );
      auto* row = g->add<HRow>(8.0F);
      row->add<Text>("", TextOpts{.color = dim()})
          ->bindText([]() { return std::string(scanning() ? "Looking…" : "Nothing found."); })
          ->showIf([]() { return nearby().empty(); });
      row->add<Chip>("Look for devices", 0xf0450)
          ->labelFrom([]() { return std::string(scanning() ? "Stop looking" : "Look for devices"); })
          ->onClick([weak]() {
            auto s = weak.lock();
            BluetoothService* bt = service();
            if (!on()) return;
            if (bt == nullptr) {
              dev::log(std::string("bt: would ") + (scanning() ? "stop looking" : "look for devices"));
              return;
            }
            // The page owns the scan from here on.
            const bool v = !bt->state().discovering;
            v ? bt->startDiscovery() : bt->stopDiscovery();
            if (s) s->scan.weScan = v;
          });
    }
  }

} // namespace kusanagi::sp
