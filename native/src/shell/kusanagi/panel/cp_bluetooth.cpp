#include "cursor-shape-v1-client-protocol.h"
#include "dbus/bluetooth/bluetooth_agent.h"
#include "dbus/bluetooth/bluetooth_service.h"
#include "render/core/renderer.h"
#include "shell/control_center/control_center_services.h"
#include "shell/kusanagi/bt_request.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/panel/control_panel.h"
#include "shell/kusanagi/panel/cp_controls.h"
#include "shell/kusanagi/panel/cp_field.h"
#include "shell/kusanagi/panel/cp_pages.h"
#include "shell/panel/panel_manager.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace kusanagi {

  namespace {
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
  } // namespace

  // Clicking connects or disconnects a paired device and pairs a new one. Hovering shows the forget button.
  class CpBluetooth::DeviceRow : public InputArea {
  public:
    DeviceRow(BluetoothService* bt, std::string path) : m_bt(bt), m_path(std::move(path)) {
      m_bg = static_cast<Box*>(addChild(ui::box({})));
      m_icon = static_cast<Label*>(addChild(cp::icon(0xf00af, 17.0F)));
      m_name = static_cast<Label*>(addChild(cp::text("", 12.0F)));
      m_name->setMaxLines(1);
      m_status = static_cast<Label*>(addChild(cp::text("", 10.0F, false, cp::dim())));
      m_status->setMaxLines(1);
      auto forget = std::make_unique<cp::IconButton>(0xf0a7a, 14.0F);
      forget->setOnActivate([this]() {
        if (m_bt != nullptr) m_bt->forget(m_path);
      });
      m_forget = static_cast<cp::IconButton*>(addChild(std::move(forget)));
      m_forget->setVisible(false);
      setCursorShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
      setOnEnter([this](const PointerData&) { restyle(); });
      setOnLeave([this]() { restyle(); });
      setOnClick([this](const PointerData&) {
        if (m_bt == nullptr) return;
        if (m_known) {
          m_live ? (void)m_bt->disconnectDevice(m_path) : (void)m_bt->connect(m_path);
        } else {
          (void)m_bt->pair(m_path);
        }
      });
    }

    bool set(const BluetoothDeviceInfo& d) {
      m_known = d.paired;
      m_live = d.connected;
      bool changed = m_icon->setText(cp::utf8(deviceIcon(d.kind)));
      changed = m_name->setText(d.alias.empty() ? d.address : d.alias) || changed;
      changed = m_status->setText(deviceStatus(d)) || changed;
      m_name->setFontWeight(m_live ? FontWeight::Bold : FontWeight::Normal);
      restyle();
      return changed;
    }

    void layout(Renderer& renderer) {
      const float w = width();
      const float h = 40.0F;
      m_bg->setSize(w, h);
      m_bg->setRadius(std::max(6.0F, kusanagi::radius() - 8.0F));
      m_icon->measure(renderer);
      m_icon->setPosition(10.0F, std::round((h - m_icon->height()) / 2.0F));
      m_forget->setSize(28.0F, 28.0F);
      m_forget->setPosition(w - 6.0F - 28.0F, 6.0F);
      m_forget->layout(renderer);
      const float x = 10.0F + m_icon->width() + 10.0F;
      const float tw = std::max(1.0F, w - 6.0F - 28.0F - 8.0F - x);
      m_name->setMaxWidth(tw);
      m_status->setMaxWidth(tw);
      m_name->measure(renderer);
      m_status->measure(renderer);
      const float colH = m_name->height() + 1.0F + m_status->height();
      const float y = std::round((h - colH) / 2.0F);
      m_name->setPosition(x, y);
      m_status->setPosition(x, y + m_name->height() + 1.0F);
    }

  private:
    void restyle() {
      const bool hover = hovered() || m_forget->hovered();
      m_bg->setFill(m_live ? cp::accent(hover ? 0.22F : 0.16F) : cp::textA(hover ? 0.06F : 0.0F));
      m_icon->setColor(m_live ? cp::accent() : cp::textA(1.0F));
      m_forget->setVisible(hover && m_known);
    }

    BluetoothService* m_bt;
    std::string m_path;
    Box* m_bg = nullptr;
    Label* m_icon = nullptr;
    Label* m_name = nullptr;
    Label* m_status = nullptr;
    cp::IconButton* m_forget = nullptr;
    bool m_known = false;
    bool m_live = false;
  };

  CpBluetooth::CpBluetooth(ControlPanel& panel, const ControlCenterServices& services) : CpPage(panel, services) {
    m_adapter = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    m_icon = static_cast<Label*>(m_adapter->addChild(cp::icon(0xf00af, 24.0F)));
    m_name = static_cast<Label*>(m_adapter->addChild(cp::text("Bluetooth", 13.0F, true)));
    m_status = static_cast<Label*>(m_adapter->addChild(cp::text("", 11.0F, false, cp::dim())));
    m_status->setMaxLines(1);
    auto scan = std::make_unique<cp::IconButton>(0xf0450);
    scan->setOnActivate([this]() {
      BluetoothService* bt = m_services.bluetooth;
      if (bt == nullptr || !bt->state().powered) return;
      if (bt->state().discovering) {
        bt->stopDiscovery();
      } else {
        bt->startDiscovery();
      }
      m_weScan = !bt->state().discovering;
    });
    m_scan = static_cast<cp::IconButton*>(m_adapter->addChild(std::move(scan)));
    auto sw = std::make_unique<cp::Switch>();
    sw->setOnToggled([this](bool on) {
      if (m_services.bluetooth != nullptr) m_services.bluetooth->setPowered(on);
      m_switch->setOn(on, true);
    });
    m_switch = static_cast<cp::Switch*>(m_adapter->addChild(std::move(sw)));

    // Pairing request card, shown under the adapter while a device asks something.
    m_req = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
    m_req->surface()->setBorder(cp::accent(0.6F), 1.0F);
    m_req->setVisible(false);
    m_reqTitle = static_cast<Label*>(m_req->addChild(cp::text("", 12.0F, true)));
    m_reqCode = static_cast<Label*>(m_req->addChild(cp::text("", 26.0F, true, cp::accent())));
    m_reqHint = static_cast<Label*>(m_req->addChild(
        cp::text("Check it's the same number on the device.", 11.0F, false, cp::dim())
    ));
    auto field = std::make_unique<cp::Field>(cp::Field::Look{}, 0xf0306);
    field->setOnAccepted([this](const std::string& t) { answer(true, t); });
    m_reqField = static_cast<cp::Field*>(m_req->addChild(std::move(field)));
    auto yes = std::make_unique<cp::Chip>("Pair", 0xf012c, true);
    yes->setOnActivate([this]() { answer(true, m_reqField->text()); });
    m_reqYes = static_cast<cp::Chip*>(m_req->addChild(std::move(yes)));
    auto no = std::make_unique<cp::Chip>("Cancel", 0xf0156);
    no->setOnActivate([this]() { answer(false); });
    m_reqNo = static_cast<cp::Chip*>(m_req->addChild(std::move(no)));
    auto allow = std::make_unique<cp::Chip>("Allow", 0xf012c, true);
    allow->setOnActivate([this]() { answer(true, m_reqField->text()); });
    m_reqAllow = static_cast<cp::Chip*>(m_req->addChild(std::move(allow)));
    auto done = std::make_unique<cp::Chip>("Done", 0xf0156);
    done->setOnActivate([this]() { answer(false); });
    m_reqDone = static_cast<cp::Chip*>(m_req->addChild(std::move(done)));

    for (auto* card : {&m_mine, &m_nearby}) {
      card->card = static_cast<cp::Card*>(addChild(std::make_unique<cp::Card>()));
      card->title = static_cast<Label*>(card->card->addChild(cp::caption(card == &m_mine ? "MY DEVICES" : "NEARBY")));
      card->empty = static_cast<Label*>(card->card->addChild(cp::text(
          card == &m_mine ? "Nothing paired yet — pick one below." : "Looking… put the device in pairing mode.", 12.0F,
          false, cp::dim()
      )));
    }

    auto settings = std::make_unique<cp::Chip>("Bluetooth settings", 0xf0493);
    settings->setOnActivate([this]() { m_panel.ipcClosed("settings-open"); });
    m_settings = static_cast<cp::Chip*>(addChild(std::move(settings)));

    // Look for new devices while this tab is open (bluetooth.autoScan).
    BluetoothService* bt = m_services.bluetooth;
    if (bt != nullptr && bt->state().powered && !bt->state().discovering && opt<bool>("bluetooth", "autoScan", true)) {
      bt->startDiscovery();
      m_weScan = true;
    }
  }

  CpBluetooth::~CpBluetooth() {
    if (m_weScan && m_services.bluetooth != nullptr && m_services.bluetooth->state().discovering) {
      m_services.bluetooth->stopDiscovery();
    }
  }

  std::optional<dev::BtRequest> CpBluetooth::request() const {
    return bt::request(m_services.bluetooth, m_services.bluetoothAgent);
  }

  void CpBluetooth::answer(bool ok, const std::string& value) {
    bt::answer(m_services.bluetooth, m_services.bluetoothAgent, ok, value);
  }

  bool CpBluetooth::syncRequest() {
    const auto r = request();
    bool changed = m_req->visible() != r.has_value();
    m_req->setVisible(r.has_value());
    if (!r) {
      m_reqKey.clear();
      return changed;
    }
    const std::string key = r->kind + "\n" + r->name + "\n" + r->code;
    if (key == m_reqKey) return changed;
    m_reqKey = key;
    const std::string& k = r->kind;
    m_reqTitle->setText(k == "confirm"     ? "Pair with " + r->name + "?"
                        : k == "display"   ? "Type this on " + r->name + ", then press Enter there"
                        : k == "authorize" ? "Let " + r->name + " connect?"
                                           : r->name + " wants a " + (k == "pin" ? "PIN" : "passkey"));
    cp::setSpacedText(*m_reqCode, r->code, 4.0F);
    m_reqCode->setVisible(k == "confirm" || k == "display");
    m_reqHint->setVisible(k == "confirm");
    m_reqField->setVisible(k == "pin" || k == "passkey");
    m_reqField->setText("");
    m_reqField->setPlaceholder(k == "pin" ? "PIN (often 0000)" : "Passkey (numbers)");
    m_reqYes->setVisible(k != "display" && k != "authorize");
    m_reqAllow->setVisible(k == "authorize");
    m_reqNo->setVisible(k != "display");
    m_reqDone->setVisible(k == "display");
    return true;
  }

  float CpBluetooth::layoutRequest(Renderer& renderer, float width) {
    // Hidden items take no room.
    const float inner = width - 28.0F;
    float y = 12.0F;
    bool first = true;
    const auto place = [&](Node* n, float h) {
      if (!first) y += 10.0F;
      first = false;
      n->setPosition(14.0F, y);
      y += h;
    };
    m_reqTitle->setMaxWidth(inner);
    m_reqTitle->measure(renderer);
    place(m_reqTitle, m_reqTitle->height());
    if (m_reqCode->visible()) {
      m_reqCode->measure(renderer);
      place(m_reqCode, m_reqCode->height());
    }
    if (m_reqHint->visible()) {
      m_reqHint->setMaxWidth(inner);
      m_reqHint->measure(renderer);
      place(m_reqHint, m_reqHint->height());
    }
    if (m_reqField->visible()) {
      m_reqField->setSize(inner, 34.0F);
      m_reqField->layout(renderer);
      place(m_reqField, 34.0F);
    }
    float x = 14.0F;
    float rowH = 0.0F;
    if (!first) y += 10.0F;
    for (cp::Chip* c : {m_reqYes, m_reqAllow, m_reqNo, m_reqDone}) {
      if (!c->visible()) continue;
      c->layout(renderer);
      c->setPosition(x, y);
      x += c->width() + 8.0F;
      rowH = std::max(rowH, c->height());
    }
    y += rowH;
    m_req->setSize(width, y + 12.0F);
    return y + 12.0F;
  }

  bool CpBluetooth::syncCard(DeviceCard& card, bool paired) {
    BluetoothService* bt = dev::fakeBluetooth() ? nullptr : m_services.bluetooth;
    std::vector<const BluetoothDeviceInfo*> devs;
    if (bt != nullptr) {
      for (const auto& d : bt->devices()) {
        if (paired ? d.paired : (!d.paired && !d.alias.empty() && d.alias != d.address)) devs.push_back(&d);
      }
    }
    std::ranges::stable_sort(devs, [paired](const BluetoothDeviceInfo* a, const BluetoothDeviceInfo* b) {
      const auto rank = [](const BluetoothDeviceInfo* d) { return d->connected ? 0 : d->connecting ? 1 : 2; };
      if (paired && rank(a) != rank(b)) return rank(a) < rank(b);
      return a->alias < b->alias;
    });
    bool changed = false;
    std::vector<std::string> paths;
    for (const auto* d : devs) paths.push_back(d->path);
    if (paths != card.paths) {
      for (DeviceRow* r : card.rows) (void)card.card->removeChild(r);
      card.rows.clear();
      for (const auto& p : paths) {
        card.rows.push_back(static_cast<DeviceRow*>(card.card->addChild(std::make_unique<DeviceRow>(bt, p))));
      }
      card.paths = paths;
      changed = true;
    }
    for (std::size_t i = 0; i < devs.size(); ++i) changed = card.rows[i]->set(*devs[i]) || changed;
    card.empty->setVisible(devs.empty());
    return changed;
  }

  bool CpBluetooth::sync(Renderer& /*renderer*/) {
    // In a test shell with a fake adapter: powered on, nothing around, no BlueZ calls.
    const bool fake = dev::fakeBluetooth();
    BluetoothService* bt = fake ? nullptr : m_services.bluetooth;
    BluetoothState st = bt != nullptr ? bt->state() : BluetoothState{};
    if (fake) st.powered = true;
    const bool on = st.powered;
    std::vector<std::string> connected;
    if (bt != nullptr) {
      for (const auto& d : bt->devices()) {
        if (d.connected) connected.push_back(d.alias);
      }
    }
    bool changed = m_icon->setText(cp::utf8(!on ? 0xf00b2 : !connected.empty() ? 0xf00b1 : 0xf00af));
    m_icon->setColor(on ? cp::accent() : cp::dim());
    std::string status;
    if (st.rfkillSoftBlocked || st.rfkillHardBlocked) {
      status = "Blocked — switch it on to unblock";
    } else if (!on) {
      status = "Off";
    } else {
      for (const auto& n : connected) status += (status.empty() ? "" : ", ") + n;
      if (status.empty()) status = "On, nothing connected";
      if (st.discovering) status += "  ·  looking for devices";
    }
    changed = m_status->setText(status) || changed;
    if (m_scan->visible() != on) changed = true;
    m_scan->setVisible(on);
    m_switch->setOn(on, true);

    const bool showMine = on;
    const bool showNearby = on && (st.discovering || [&] {
      if (bt == nullptr) return false;
      for (const auto& d : bt->devices()) {
        if (!d.paired && !d.alias.empty() && d.alias != d.address) return true;
      }
      return false;
    }());
    if (m_mine.card->visible() != showMine || m_nearby.card->visible() != showNearby) changed = true;
    m_mine.card->setVisible(showMine);
    m_nearby.card->setVisible(showNearby);
    changed = syncCard(m_mine, true) || changed;
    changed = syncCard(m_nearby, false) || changed;
    changed = syncRequest() || changed;
    return changed;
  }

  float CpBluetooth::layoutCard(Renderer& renderer, DeviceCard& card, float width) {
    const float inner = width - 24.0F;
    float y = 12.0F;
    card.title->measure(renderer);
    card.title->setPosition(12.0F, y);
    y += card.title->height() + 4.0F;
    for (DeviceRow* r : card.rows) {
      y += 4.0F;
      r->setPosition(12.0F, y);
      r->setSize(inner, 40.0F);
      r->layout(renderer);
      y += 40.0F;
    }
    if (card.empty->visible()) {
      card.empty->measure(renderer);
      y += 4.0F;
      card.empty->setPosition(12.0F, y);
      y += card.empty->height();
    }
    card.card->setSize(width, y + 12.0F);
    return y + 12.0F;
  }

  float CpBluetooth::layout(Renderer& renderer, float width) {
    float y = 0.0F;
    const float h = 64.0F;
    m_adapter->setPosition(0.0F, y);
    m_adapter->setSize(width, h);
    m_icon->measure(renderer);
    m_icon->setPosition(14.0F, std::round((h - m_icon->height()) / 2.0F));
    float rx = width - 12.0F - m_switch->width();
    m_switch->setPosition(rx, std::round((h - m_switch->height()) / 2.0F));
    if (m_scan->visible()) {
      rx -= 6.0F + 34.0F;
      m_scan->setSize(34.0F, 34.0F);
      m_scan->setPosition(rx, std::round((h - 34.0F) / 2.0F));
      m_scan->layout(renderer);
    }
    const float tx = 14.0F + m_icon->width() + 12.0F;
    m_status->setMaxWidth(std::max(1.0F, rx - 8.0F - tx));
    m_name->measure(renderer);
    m_status->measure(renderer);
    const float colH = m_name->height() + 2.0F + m_status->height();
    m_name->setPosition(tx, std::round((h - colH) / 2.0F));
    m_status->setPosition(tx, std::round((h - colH) / 2.0F) + m_name->height() + 2.0F);
    y += h;

    if (m_req->visible()) {
      y += 12.0F;
      m_req->setPosition(0.0F, y);
      y += layoutRequest(renderer, width);
    }

    for (auto* card : {&m_mine, &m_nearby}) {
      if (!card->card->visible()) continue;
      y += 12.0F;
      card->card->setPosition(0.0F, y);
      y += layoutCard(renderer, *card, width);
    }
    y += 12.0F;
    m_settings->layout(renderer);
    m_settings->setPosition(0.0F, y);
    return y + m_settings->height();
  }

} // namespace kusanagi
