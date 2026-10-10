#include "shell/kusanagi/bt_request.h"

#include "dbus/bluetooth/bluetooth_agent.h"
#include "dbus/bluetooth/bluetooth_service.h"
#include "shell/panel/panel_manager.h"

#include <charconv>
#include <cstdio>

namespace kusanagi::bt {

  std::optional<dev::BtRequest> request(BluetoothService* service, BluetoothAgent* agent) {
    if (dev::fakeBluetooth()) return dev::fakeBtRequest();
    if (agent == nullptr || !agent->hasPendingRequest()) return std::nullopt;
    const BluetoothPairingRequest r = agent->pendingRequest();
    dev::BtRequest out;
    char code[16];
    switch (r.kind) {
    case BluetoothPairingKind::Confirm:
      out.kind = "confirm";
      std::snprintf(code, sizeof(code), "%06u", r.passkey);
      out.code = code;
      break;
    case BluetoothPairingKind::PinCode: out.kind = "pin"; break;
    case BluetoothPairingKind::Passkey: out.kind = "passkey"; break;
    case BluetoothPairingKind::DisplayPinCode:
      out.kind = "display";
      out.code = r.pin;
      break;
    case BluetoothPairingKind::DisplayPasskey:
      out.kind = "display";
      std::snprintf(code, sizeof(code), "%06u", r.passkey);
      out.code = code;
      break;
    case BluetoothPairingKind::Authorize:
    case BluetoothPairingKind::AuthorizeService: out.kind = "authorize"; break;
    default: return std::nullopt;
    }
    out.name = "A device";
    if (service != nullptr) {
      for (const auto& d : service->devices()) {
        if (d.path == r.devicePath) {
          out.name = d.alias.empty() ? d.address : d.alias;
          break;
        }
      }
    }
    return out;
  }

  void answer(BluetoothService* service, BluetoothAgent* agent, bool ok, const std::string& value) {
    const auto r = request(service, agent);
    if (!r) return;
    if (dev::fakeBluetooth()) {
      dev::log("bt: " + r->kind + " for " + r->name + " -> " + (ok ? "yes" : "no")
               + (ok && (r->kind == "pin" || r->kind == "passkey") ? " \"" + value + "\"" : ""));
      dev::fakeBtRequest().reset();
      PanelManager::instance().refresh();
      return;
    }
    if (agent == nullptr) return;
    if (r->kind == "display") {
      agent->acceptConfirm(); // Nothing to answer: the device shows whether it worked.
    } else if (!ok) {
      agent->rejectConfirm();
    } else if (r->kind == "pin") {
      agent->submitPin(value.empty() ? "0000" : value);
    } else if (r->kind == "passkey") {
      std::uint32_t passkey = 0;
      const std::string v = value.empty() ? "0" : value;
      const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), passkey);
      if (ec != std::errc{} || end != v.data() + v.size()) {
        agent->rejectConfirm();
      } else {
        agent->submitPasskey(passkey);
      }
    } else {
      agent->acceptConfirm();
    }
    PanelManager::instance().refresh();
  }

} // namespace kusanagi::bt
