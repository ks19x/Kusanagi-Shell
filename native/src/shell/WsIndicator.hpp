// WsIndicator.hpp — workspace/tag indicator, every v4 style:
//   pills / dots (hollow ring = empty, solid = windows, wide accent pill w/ number = active, glow)
//   numbers / roman / kanji / custom (glyphs; active = accent + underline)
//   dwl (flat numbered blocks, filled = active, corner square = has windows)
#pragma once
#include "system/Wm.hpp"
#include "ui/Widgets.hpp"

namespace ks::shell {

class WsSlot;

class WsIndicator : public ui::Row {
public:
    WsIndicator();
    float slotHeight = 22;
    float implicitHeight() override { return slotHeight; }
    void setEntries(const std::vector<WsEntry>& e);
    void restyle();                                        // config / theme changed
    std::function<void(const WsEntry&)> onActivate;

    std::string style;
    bool glow = true;
    gfx::Color activeColor() const;
    std::string glyph(int n) const;

private:
    std::vector<WsEntry> m_entries;
    std::vector<WsSlot*> m_slots;
};

} // namespace ks::shell
