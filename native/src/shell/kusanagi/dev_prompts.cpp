#include "shell/kusanagi/dev_prompts.h"

#include "cli/schema_msg.h"
#include "core/log.h"
#include "core/timer_manager.h"
#include "ipc/ipc_service.h"
#include "shell/kusanagi/polkit_panel.h"
#include "shell/panel/panel_manager.h"

#include <cstdlib>
#include <sstream>

namespace kusanagi::dev {

  namespace {

    constexpr Logger kLog("kusanagi-dev");

    // A polkit request with made-up data. "kusanagi" is the right password.
    class FakePolkit : public PolkitFlow {
    public:
      void show(int identities, bool error) {
        m_active = true;
        m_ids = {"Sig"};
        if (identities > 1) m_ids.emplace_back("root");
        for (int i = 2; i < identities; ++i) m_ids.push_back("admin" + std::to_string(i));
        m_sel = 0;
        m_required = true;
        m_failures = 0;
        m_error = error;
        m_timer.stop();
      }

      [[nodiscard]] bool active() const override { return m_active; }
      [[nodiscard]] std::string actionId() const override { return "org.freedesktop.udisks2.filesystem-mount-system"; }
      [[nodiscard]] std::string message() const override {
        return "Authentication is required to mount Samsung SSD 870 EVO 1TB (/dev/sda2)";
      }
      [[nodiscard]] std::vector<std::string> identities() const override { return m_ids; }
      [[nodiscard]] std::size_t selectedIdentity() const override { return m_sel; }
      void selectIdentity(std::size_t index) override {
        if (index < m_ids.size()) m_sel = index;
        log("polkit: authenticate as " + m_ids[m_sel]);
      }
      [[nodiscard]] bool responseRequired() const override { return m_required; }
      [[nodiscard]] bool responseVisible() const override { return false; }
      [[nodiscard]] std::string inputPrompt() const override { return "Password: "; }
      [[nodiscard]] std::string supplementaryMessage() const override {
        return m_error ? "The account is locked due to 3 failed logins." : "";
      }
      [[nodiscard]] bool supplementaryIsError() const override { return m_error; }
      [[nodiscard]] std::uint32_t failures() const override { return m_failures; }

      void submit(const std::string& response) override {
        if (!m_required) return;
        m_required = false;
        log("polkit: submitted a response of " + std::to_string(response.size()) + " characters");
        const bool right = response == "kusanagi";
        // PAM takes a moment, and longer for a wrong password (pam_faildelay).
        m_timer.start(std::chrono::milliseconds(right ? 700 : 1500), [this, right]() {
          if (right) {
            log("polkit: authorized");
            m_active = false;
            PanelManager::instance().closePanel();
            return;
          }
          log("polkit: wrong password");
          ++m_failures;
          m_required = true;
          PanelManager::instance().refresh();
        });
        PanelManager::instance().refresh();
      }
      void cancel() override {
        if (!m_active) return;
        log("polkit: cancelled");
        m_active = false;
        m_timer.stop();
      }

    private:
      bool m_active = false;
      std::vector<std::string> m_ids;
      std::size_t m_sel = 0;
      bool m_required = false;
      bool m_error = false;
      std::uint32_t m_failures = 0;
      Timer m_timer;
    };

    FakePolkit& fakePolkit() {
      static FakePolkit f;
      return f;
    }

    bool g_bluetooth = false;
    bool g_wifi = false;

    std::vector<std::string> words(const std::string& s) {
      std::vector<std::string> out;
      std::istringstream in(s);
      for (std::string w; in >> w;) out.push_back(w);
      return out;
    }

  } // namespace

  bool enabled() {
    static const bool on = std::getenv("KUSANAGI_DEV_SLOT") != nullptr;
    return on;
  }

  void log(const std::string& line) { kLog.info("{}", line); }

  PolkitFlow* polkitFlow() { return enabled() && fakePolkit().active() ? &fakePolkit() : nullptr; }

  bool fakeBluetooth() { return enabled() && g_bluetooth; }

  std::optional<BtRequest>& fakeBtRequest() {
    static std::optional<BtRequest> r;
    return r;
  }

  bool fakeWifi() { return enabled() && g_wifi; }

  const std::vector<WifiNetwork>& fakeNetworks() {
    static const std::vector<WifiNetwork> nets{
        {"Section 9", 82, true, true, true},  {"Puppet Master", 64, true, false, true},
        {"Niihama Free", 55, false, false, false}, {"Batou's Hotspot", 47, true, false, false},
        {"Tachikoma", 30, true, false, false}, {"Laughing Man", 12, true, false, false},
    };
    return nets;
  }

  void registerIpc(IpcService& ipc) {
    if (!enabled()) return;
    ipc.bind(kusanagi::cli::msg::devPrompt, [](const std::string& args) -> std::string {
      const auto w = words(args);
      if (w.empty()) return "error: dev-prompt polkit|bt|wifi …\n";
      if (w[0] == "polkit") {
        const int ids = w.size() > 1 ? std::atoi(w[1].c_str()) : 1;
        const bool error = w.size() > 2 && w[2] == "error";
        fakePolkit().show(std::max(1, ids), error);
        if (PanelManager::instance().isOpenPanel("polkit")) {
          PanelManager::instance().refresh();
        } else {
          PanelManager::instance().openPanel("polkit");
        }
        return "ok\n";
      }
      if (w[0] == "bt" && w.size() > 1) {
        const std::string& k = w[1];
        if (k == "on" || k == "off") {
          g_bluetooth = k == "on";
          if (!g_bluetooth) fakeBtRequest().reset();
        } else if (k == "none") {
          fakeBtRequest().reset();
        } else if (k == "confirm" || k == "pin" || k == "passkey" || k == "display" || k == "authorize") {
          g_bluetooth = true;
          const std::string name = w.size() > 2 ? args.substr(args.find(w[2])) : "WH-1000XM4";
          fakeBtRequest() = BtRequest{.kind = k, .name = name,
                                      .code = k == "confirm" ? "482916" : k == "display" ? "0731" : ""};
        } else {
          return "error: dev-prompt bt on|off|none|confirm|pin|passkey|display|authorize [name]\n";
        }
        PanelManager::instance().refresh();
        return "ok\n";
      }
      if (w[0] == "wifi" && w.size() > 1 && (w[1] == "on" || w[1] == "off")) {
        g_wifi = w[1] == "on";
        PanelManager::instance().refresh();
        return "ok\n";
      }
      return "error: dev-prompt polkit [identities] [error] | bt … | wifi on|off\n";
    });
  }

} // namespace kusanagi::dev
