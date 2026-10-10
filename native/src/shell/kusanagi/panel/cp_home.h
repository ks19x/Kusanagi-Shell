#pragma once

// Control panel home page: quick tiles, volume and mic sliders (plus brightness when a screen can be
// dimmed), weather, now playing and live mini stats. panel.order sets which sections exist and their order.

#include "shell/kusanagi/panel/cp_page.h"
#include "ui/palette.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

class Box;
class Image;
class InputArea;
class Label;

namespace kusanagi {

  namespace cp {
    class Card;
    class IconButton;
    class Slider;
    class Tile;
  } // namespace cp

  class CpHome : public CpPage {
  public:
    CpHome(ControlPanel& panel, const ControlCenterServices& services);
    ~CpHome() override;

    bool sync(Renderer& renderer) override;
    float layout(Renderer& renderer, float width) override;
    void tick() override;

  private:
    struct TileState {
      char32_t icon = 0;
      std::string label;
      std::string sub;
      bool on = false;
    };
    struct Stat {
      cp::Card* card = nullptr;
      Label* icon = nullptr;
      Label* label = nullptr;
      Label* value = nullptr;
      Box* track = nullptr;
      Box* fill = nullptr;
      float frac = 0.0F;
      std::string text;
    };

    [[nodiscard]] TileState tile(const std::string& id) const;
    void press(const std::string& id);
    void buildTiles();
    void buildSliders();
    void buildWeather();
    void buildMedia();
    void buildStats();
    bool syncMedia(Renderer& renderer);
    bool syncWeather();
    bool syncStats();
    void readRecorder();
    void setMediaFrac(float frac, bool animate);

    std::vector<std::string> m_order;
    std::vector<std::string> m_tileIds;
    std::string m_tileStyle;

    // tiles
    Node* m_tilesSec = nullptr;
    std::vector<cp::Tile*> m_tiles;
    bool m_nightlight = false;
    std::string m_recordMode = "off";
    double m_recordSince = 0.0;

    // sliders
    Node* m_slidersSec = nullptr;
    cp::Slider* m_output = nullptr;
    cp::Slider* m_mic = nullptr;
    cp::Slider* m_brightness = nullptr;

    // weather
    cp::Card* m_weather = nullptr;
    Label* m_wIcon = nullptr;
    Label* m_wTemp = nullptr;
    Label* m_wFeels = nullptr;
    Label* m_wDesc = nullptr;
    struct Day {
      Label* day = nullptr;
      Label* icon = nullptr;
      Label* range = nullptr;
    };
    std::array<Day, 3> m_days{};
    bool m_weatherShown = false;

    // media
    cp::Card* m_media = nullptr;
    Box* m_artBg = nullptr;
    Label* m_artIcon = nullptr;
    Image* m_art = nullptr;
    Label* m_title = nullptr;
    Label* m_artist = nullptr;
    Box* m_progress = nullptr;
    Box* m_progressFill = nullptr;
    InputArea* m_seek = nullptr;
    cp::IconButton* m_prev = nullptr;
    cp::IconButton* m_play = nullptr;
    cp::IconButton* m_next = nullptr;
    bool m_mediaShown = false;
    float m_mediaFrac = 0.0F;
    std::string m_artUrl;
    std::string m_trackKey;
    std::unordered_set<std::string> m_pendingArt;
    std::shared_ptr<int> m_alive = std::make_shared<int>(0);

    // stats
    Node* m_statsSec = nullptr;
    std::array<Stat, 4> m_stats{};
    bool m_retainedSensors = false;
  };

} // namespace kusanagi
