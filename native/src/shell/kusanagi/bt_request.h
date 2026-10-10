#pragma once

// Bluetooth pairing prompts: what a device asks while pairing (confirm a code, type a PIN or passkey, show a
// code to type on the device, allow it to connect) and the answer back. Comes from the BlueZ agent,
// or from a dev-prompt fake in a test shell. Shared by the control panel and the Bluetooth settings page.

#include "shell/kusanagi/dev_prompts.h"

#include <optional>
#include <string>

class BluetoothAgent;
class BluetoothService;

namespace kusanagi::bt {

  // The pending request, if any. kind is one of confirm, pin, passkey, display or authorize.
  [[nodiscard]] std::optional<dev::BtRequest> request(BluetoothService* service, BluetoothAgent* agent);
  // An empty PIN is sent as "0000". A passkey that isn't a number rejects the request.
  void answer(BluetoothService* service, BluetoothAgent* agent, bool ok, const std::string& value = {});

} // namespace kusanagi::bt
