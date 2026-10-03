// Shell.hpp — owns the per-output surfaces (bars now; panels/overlays as they land), registers
// the IPC targets, and is where shell-wide actions live.
#pragma once
#include "shell/Bar.hpp"

#include <memory>
#include <vector>

namespace ks::wl { class Output; }

namespace ks::shell {

class Shell {
public:
    static Shell& get();
    bool start();

    // actions shared by modules
    void togglePanel(wl::Output* from);
    void scrollAction(const std::string& action, int steps);
    void changeVolume(int steps);

    // extension points for bar modules implemented elsewhere (volume, network, tray, media, title)
    void addBarModules(Bar& bar, ui::Row& row, std::function<bool(int)> statScroll);
    void restyleBarModules(Bar& bar);

    const std::vector<std::unique_ptr<Bar>>& bars() const { return m_bars; }

private:
    void addOutput(wl::Output* o);
    void removeOutput(wl::Output* o);
    void registerIpc();
    bool wantsBar(wl::Output* o) const;
    std::vector<std::unique_ptr<Bar>> m_bars;
};

} // namespace ks::shell
