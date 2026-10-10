#pragma once

// Fake polkit, Bluetooth pairing and Wi-Fi password prompts, so a test shell (tools/kdev/kt) can show and
// drive them without real hardware or answering anything real. Only active when KUSANAGI_DEV_SLOT is set,
// where the real polkit, BlueZ and NetworkManager agents are never registered. Answers are only logged.
//
//   kusanagi-shell msg dev-prompt polkit [identities] [error]   password "kusanagi" passes; error adds a PAM line
//   kusanagi-shell msg dev-prompt bt on|off                     pretend there is an adapter
//   kusanagi-shell msg dev-prompt bt confirm|pin|passkey|display|authorize|none
//   kusanagi-shell msg dev-prompt wifi on|off                   made-up networks in the network tab

#include <optional>
#include <string>
#include <vector>

class IpcService;

namespace kusanagi {

  class PolkitFlow;

  namespace dev {

    [[nodiscard]] bool enabled();
    void registerIpc(IpcService& ipc);
    void log(const std::string& line);

    // The fake polkit flow while it's showing, else nullptr.
    [[nodiscard]] PolkitFlow* polkitFlow();

    // kind is one of confirm, pin, passkey, display or authorize.
    struct BtRequest {
      std::string kind;
      std::string name;
      std::string code;
    };
    [[nodiscard]] bool fakeBluetooth();
    [[nodiscard]] std::optional<BtRequest>& fakeBtRequest();

    struct WifiNetwork {
      std::string ssid;
      int signal = 0;
      bool secure = false;
      bool active = false;
      bool known = false;
    };
    [[nodiscard]] bool fakeWifi();
    [[nodiscard]] const std::vector<WifiNetwork>& fakeNetworks();

  } // namespace dev

} // namespace kusanagi
