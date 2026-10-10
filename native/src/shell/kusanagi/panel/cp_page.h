#pragma once

// One page of the control panel. A page is built when it is shown and dropped when the tab changes or the
// panel closes.

#include "render/scene/node.h"

#include <string>
#include <vector>

class Renderer;
struct ControlCenterServices;

namespace kusanagi {

  class ControlPanel;

  class CpPage : public Node {
  public:
    CpPage(ControlPanel& panel, const ControlCenterServices& services) : m_panel(panel), m_services(services) {}

    // Update phase: pulls the services' state into the controls. Returns true when text or structure
    // changed and the page must be laid out again.
    virtual bool sync(Renderer& renderer) = 0;
    // Layout phase: measures and places everything for `width`. Returns the page height.
    virtual float layout(Renderer& renderer, float width) = 0;
    // Once a second while the panel is open, for the media position and stats.
    virtual void tick() {}
    // Escape: close a transient part of the page (a prompt) and return true, else the panel closes.
    virtual bool dismissTransient() { return false; }

  protected:
    ControlPanel& m_panel;
    const ControlCenterServices& m_services;
  };

} // namespace kusanagi
