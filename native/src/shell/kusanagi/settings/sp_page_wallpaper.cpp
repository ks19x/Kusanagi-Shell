// Settings > Wallpaper: current wallpaper, engine (Kusanagi or awww), transitions, fit, parallax, dim,
// slideshow and the picker's folder and columns.

#include "render/core/renderer.h"
#include "shell/kusanagi/kusanagi_ipc.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>

namespace kusanagi::sp {

  namespace {

    std::string currentWallpaper() {
      std::ifstream f(expandHome("~/.config/kusanagi/wallpaper"));
      std::stringstream ss;
      ss << f.rdbuf();
      std::string s = ss.str();
      s.erase(0, s.find_first_not_of(" \t\r\n"));
      s.erase(s.find_last_not_of(" \t\r\n") + 1);
      return s;
    }

    // Switch to a random other wallpaper to show the transition.
    void tryIt() {
      spawn({"sh", "-c",
             "cur=$(cat \"$1\"); f=$(ls -1 \"$2\"/*.jpg \"$2\"/*.png 2>/dev/null | grep -vxF \"$cur\" | shuf -n1); [ -n \"$f\" ] && "
             "\"$3\" wallpaper \"$f\"",
             "sh", expandHome("~/.config/kusanagi/wallpaper"), expandHome(get<std::string>("wallpaper.folder", "~/Pictures/Wallpapers")),
             cliCommand()});
    }

    // Thumbnail and file name of the current wallpaper, with Pick and Random buttons.
    class Current : public Item {
    public:
      Current() {
        m_thumb = static_cast<Box*>(addChild(ui::box({})));
        m_thumb->setFill(textA(0.06F));
        m_image = static_cast<Image*>(addChild(ui::image({.fit = ImageFit::Cover})));
        m_name = static_cast<Label*>(addChild(makeText("", 13.0F, true)));
        m_name->setMaxLines(1);
        m_name->setEllipsize(TextEllipsize::Middle);
        m_chips = static_cast<HRow*>(addChild(std::make_unique<HRow>(8.0F)));
        m_chips->add<Chip>("Pick…", 0xf0e09)->onClick([]() { (void)ipc("panel-toggle wallpaper"); });
        m_chips->add<Chip>("Random", 0xf0450)->onClick([]() { tryIt(); });
        sync();
      }

      void sync() override {
        const std::string path = currentWallpaper();
        if (path != m_path) {
          m_path = path;
          m_loaded = false;
          const auto slash = path.rfind('/');
          m_name->setText(slash == std::string::npos ? path : path.substr(slash + 1));
          requestLayout();
        }
      }

      float place(Renderer& renderer, float width) override {
        width = widthFor(width);
        const float h = 120.0F;
        const float r = std::max(6.0F, kusanagi::radius() - 6.0F);
        m_thumb->setSize(213.0F, h);
        m_thumb->setRadius(r);
        m_image->setSize(213.0F, h);
        m_image->setRadius(0.0F); // Matches the classic look: the picture keeps square corners.
        if (!m_loaded) {
          m_loaded = true;
          if (m_path.empty()) m_image->clear(renderer);
          else (void)m_image->setSourceFile(renderer, m_path, 426);
        }
        // The row is 36 px narrower than the page; the name may use the page width minus 260.
        m_name->setMaxWidth(std::max(1.0F, width + 36.0F - 260.0F));
        m_name->measure(renderer);
        const float chipsH = m_chips->place(renderer, width);
        const float colH = m_name->height() + 10.0F + chipsH;
        const float x = 213.0F + 18.0F;
        const float y = std::round((h - colH) / 2.0F);
        m_name->setPosition(x, y);
        m_chips->setPosition(x, y + m_name->height() + 10.0F);
        setSize(width, h);
        return h;
      }

    private:
      Box* m_thumb = nullptr;
      Image* m_image = nullptr;
      Label* m_name = nullptr;
      HRow* m_chips = nullptr;
      std::string m_path = "\x01";
      bool m_loaded = false;
    };

    bool kusanagiEngine() { return get<std::string>("wallpaper.renderer", "kusanagi") == "kusanagi"; }

  } // namespace

  void buildWallpaper(Column& page) {
    // Watch ~/.config/kusanagi/wallpaper for changes.
    page.add<Poll>(1000, []() { refresh(); });

    page.add<Group>("Current")->add<Current>();

    {
      auto* g = page.add<Group>("Engine", "Kusanagi draws the wallpaper itself (transitions, parallax, slideshow). awww is the fallback.");
      g->add<Segmented>(Binding{
                            .get = []() { return value("wallpaper.renderer"); },
                            .set = [](const json& v) {
                              set("wallpaper.renderer", v);
                              // Hand over right away; awww needs to be told to draw again.
                              if (v == "awww") spawn({"sh", "-c", "sleep 0.4; " + cliCommand() + " wallpaper restore"});
                            },
                        },
                        std::vector<Option>{{"Kusanagi", "kusanagi"}, {"awww", "awww"}}, 0.0F);
    }

    {
      auto* g = page.add<Group>("Transition");
      g->showIf(kusanagiEngine);
      auto* flow = g->add<Flow>(6.0F);
      static const std::array<std::pair<const char*, const char*>, 8> kinds{{{"random", "Random"},
                                                                             {"fade", "Fade"},
                                                                             {"blur", "Blur"},
                                                                             {"wipe", "Wipe"},
                                                                             {"grow", "Grow"},
                                                                             {"slide", "Slide"},
                                                                             {"zoom", "Zoom"},
                                                                             {"blinds", "Blinds"}}};
      for (const auto& [v, l] : kinds) flow->addItem(choiceChip(l, "wallpaper.transition", v));
      g->add<Row>("Speed", std::make_unique<Stepper>(bind("wallpaper.duration"),
                                                     StepperOpts{.from = 3, .to = 30, .suffix = "00 ms", .scale = 100.0}));
      g->add<Chip>("Try it", 0xf0208)->onClick([]() { tryIt(); });
    }

    {
      auto* g = page.add<Group>("Picture");
      g->showIf(kusanagiEngine);
      g->add<Row>("Fit", std::make_unique<Segmented>(bind("wallpaper.fill"),
                                                     std::vector<Option>{{"Fill", "fill"},
                                                                         {"Fit", "fit"},
                                                                         {"Stretch", "stretch"},
                                                                         {"Centre", "center"},
                                                                         {"Tile", "tile"}},
                                                     380.0F));
      g->add<Slider>(bind("wallpaper.parallax"),
                     SliderOpts{
                         .icon = 0xf0e09,
                         .label = "Parallax",
                         .toUnit = [](const json& v) { return v.get<float>() / 0.12F; },
                         .fromUnit = [](float u) { return json(std::round(u * 0.12 * 100.0) / 100.0); },
                         .text = [](const json& v) -> std::string {
                           const double p = v.get<double>();
                           return p > 0.0 ? std::to_string(std::lround(p * 100.0)) + "%" : "off";
                         },
                     });
      g->add<Text>("Parallax pans the picture a little as you move between workspaces.",
                   TextOpts{.px = 10.0F, .color = dim(), .wrap = true});
      g->add<Slider>(bind("wallpaper.dim"),
                     SliderOpts{
                         .icon = 0xf050e,
                         .label = "Dim",
                         .toUnit = [](const json& v) { return v.get<float>() / 0.6F; },
                         .fromUnit = [](float u) { return json(std::round(u * 0.6 * 100.0) / 100.0); },
                         .text = [](const json& v) { return std::to_string(std::lround(v.get<double>() * 100.0)) + "%"; },
                     });
    }

    {
      auto* g = page.add<Group>("Slideshow", "A random wallpaper from the folder every so often — re-themed like a manual pick.");
      g->showIf(kusanagiEngine);
      g->add<Row>("Change every", std::make_unique<Segmented>(
                                      bind("wallpaper.slideshow"),
                                      std::vector<Option>{{"Off", 0}, {"5 min", 5}, {"15 min", 15}, {"1 h", 60}, {"3 h", 180}}, 380.0F));
    }

    {
      auto* g = page.add<Group>("Picker", "Super+A");
      g->add<Row>("Folder", textField("wallpaper.folder", 280.0F));
      g->add<Row>("Columns", std::make_unique<Stepper>(bind("wallpaper.columns"), StepperOpts{.from = 3, .to = 6}));
    }
  }

} // namespace kusanagi::sp
