// Settings > Notifications: popups, the OSD and the screenshot card. Picking a design shows a preview.

#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"

#include <array>

namespace kusanagi::sp {

  namespace {

    // Sample popup, cycling through three bodies.
    void testNotification() {
      static int count = 0;
      ++count;
      static const std::array<const char*, 3> bodies{"This is how your notifications look.",
                                                     "Swipe right or right-click to dismiss.",
                                                     "Hover to pause the countdown."};
      const json payload{{"app_name", "Kusanagi"}, {"summary", "Kusanagi"}, {"body", bodies[static_cast<std::size_t>(count % 3)]}};
      (void)ipc("notification-show " + payload.dump());
    }

    void osdPreview() { (void)ipc("osd-preview volume"); }

    // Do not disturb lives in the shell, not in settings.json.
    Binding dndBinding() {
      return Binding{
          .get = []() -> json { return ipc("notification-dnd-status").starts_with("on"); },
          .set = [](const json& v) { (void)ipc(std::string("notification-dnd-set ") + (truthy(v) ? "on" : "off")); },
      };
    }

  } // namespace

  void buildNotifications(Column& page) {
    {
      auto* g = page.add<Group>("Notifications");
      g->add<Row>("Do not disturb", "popups stay quiet, history still fills", std::make_unique<Switch>(dndBinding()));
      g->add<Heading>("Design");
      g->add<StylePicker>("notifications",
                          std::vector<Option>{{"Comfortable", "comfortable"}, {"Compact", "compact"}, {"Minimal", "minimal"},
                                              {"Accent edge", "accent"}},
                          bind("notifications.style", [](const json&) { testNotification(); }), 128.0F);
      g->add<Heading>("Where they pop up");
      g->add<StylePicker>("corner",
                          std::vector<Option>{{"Top left", "top-left"},
                                              {"Top centre", "top-center"},
                                              {"Top right", "top-right"},
                                              {"Bottom left", "bottom-left"},
                                              {"Bottom centre", "bottom-center"},
                                              {"Bottom right", "bottom-right"}},
                          bind("notifications.position", [](const json&) { testNotification(); }), 112.0F);
      g->add<Row>("Stay for", std::make_unique<Stepper>(bind("notifications.timeout"),
                                                        StepperOpts{.from = 2, .to = 15, .suffix = " s", .scale = 1000.0}));
      g->add<Row>("At most on screen", std::make_unique<Stepper>(bind("notifications.max"), StepperOpts{.from = 1, .to = 8}));
      g->add<Row>("Countdown line", std::make_unique<Switch>(bind("notifications.progress")));
      g->add<Row>("Pictures", "album art, avatars", std::make_unique<Switch>(bind("notifications.images")));
      g->add<Chip>("Send a test notification", 0xf009e)->onClick([]() { testNotification(); });
    }

    {
      auto* g = page.add<Group>("On-screen display", "The pill that shows up when volume, mic or game mode change.");
      g->add<Heading>("Design");
      g->add<StylePicker>("osd", std::vector<Option>{{"Pill", "pill"}, {"Minimal", "minimal"}, {"Box", "box"}},
                          bind("osd.style", [](const json&) { osdPreview(); }), 128.0F);
      const auto notBox = []() { return get<std::string>("osd.style", "pill") != "box"; };
      g->add<Heading>("Where")->showIf(notBox);
      g->add<StylePicker>("edge",
                          std::vector<Option>{{"Top", "top"}, {"Bottom", "bottom"}, {"Left edge", "left"}, {"Right edge", "right"}},
                          bind("osd.position", [](const json&) { osdPreview(); }), 128.0F)
          ->showIf(notBox);
      g->add<Row>("Stay for", std::make_unique<Stepper>(bind("osd.timeout"),
                                                        StepperOpts{.from = 6, .to = 40, .step = 2, .suffix = "00 ms", .scale = 100.0}));
      g->add<Row>("Show the number", std::make_unique<Switch>(bind("osd.showValue", [](const json&) { osdPreview(); })));
      g->add<Row>("Volume", std::make_unique<Switch>(bind("osd.volume")));
      g->add<Row>("Microphone", std::make_unique<Switch>(bind("osd.mic")));
      g->add<Row>("Brightness", std::make_unique<Switch>(bind("osd.brightness")));
      g->add<Row>("Game mode", std::make_unique<Switch>(bind("osd.gamemode")));
      g->add<Chip>("Preview", 0xf0208)->onClick([]() { osdPreview(); });
    }

    {
      auto* g = page.add<Group>("Screenshots", "The preview card after Print / Super+Shift+S.");
      g->add<Row>("Corner", std::make_unique<Segmented>(bind("screenshot.position"),
                                                        std::vector<Option>{{"Bottom right", "bottom-right"},
                                                                            {"Bottom left", "bottom-left"},
                                                                            {"Top right", "top-right"},
                                                                            {"Top left", "top-left"}},
                                                        380.0F, 10.0F));
      g->add<Row>("Stay for", std::make_unique<Stepper>(bind("screenshot.timeout"),
                                                        StepperOpts{.from = 2, .to = 20, .suffix = " s", .scale = 1000.0}));
      g->add<Row>("Editor", "command — the file is added at the end", textField("screenshot.editor", 200.0F));
    }
  }

} // namespace kusanagi::sp
