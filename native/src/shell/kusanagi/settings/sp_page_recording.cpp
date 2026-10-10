// Settings > Recording: the recorder's current state and controls, and the `recorder` settings used by
// `kusanagi record`.

#include "core/deferred_call.h"
#include "core/process/process.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/kusanagi_ipc.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>

namespace kusanagi::sp {

  namespace {

    // From $XDG_RUNTIME_DIR/kusanagi/record.json, which `kusanagi record` rewrites.
    struct RecState {
      std::string mode = "off"; // off, replay, record or stream.
      std::string backend = "none";
      double since = 0.0;
      std::vector<std::string> available; // Any of replay, record, stream.

      [[nodiscard]] bool active() const { return mode != "off"; }
      [[nodiscard]] bool has(const std::string& what) const { return std::ranges::find(available, what) != available.end(); }
      bool operator==(const RecState&) const = default;
    };

    std::string stateFile() {
      const char* rt = std::getenv("XDG_RUNTIME_DIR");
      return std::string(rt != nullptr && *rt != '\0' ? rt : "/tmp") + "/kusanagi/record.json";
    }

    // Anything that isn't valid JSON leaves the state alone.
    void parse(RecState& s, const std::string& text) {
      const json j = json::parse(text, nullptr, false);
      if (!j.is_object()) return;
      const auto str = [&j](const char* k, const char* fallback) {
        return j.contains(k) && j[k].is_string() && !j[k].get<std::string>().empty() ? j[k].get<std::string>() : std::string(fallback);
      };
      s.mode = str("mode", "off");
      s.backend = str("backend", "none");
      s.since = j.contains("since") && j["since"].is_number() ? j["since"].get<double>() : 0.0;
      s.available.clear();
      if (j.contains("available") && j["available"].is_array()) {
        for (const auto& a : j["available"]) {
          if (a.is_string()) s.available.push_back(a.get<std::string>());
        }
      }
    }

    void readState(RecState& s) {
      std::ifstream f(stateFile());
      if (!f) return;
      std::stringstream ss;
      ss << f.rdbuf();
      parse(s, ss.str());
    }

    // "12:34" or "1:02:03".
    std::string elapsed(const RecState& s) {
      if (!s.active() || s.since <= 0.0) return "";
      const double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
      const long sec = std::max(0L, static_cast<long>(std::floor(now - s.since)));
      const long h = sec / 3600, m = sec / 60 % 60;
      const auto pad = [](long n) { return (n < 10 ? "0" : "") + std::to_string(n); };
      return (h != 0 ? std::to_string(h) + ":" + pad(m) : std::to_string(m)) + ":" + pad(sec % 60);
    }

    std::string modeTitle(const std::string& mode) {
      if (mode == "off") return "Not recording";
      if (mode == "replay") return "Replay buffer on";
      if (mode == "record") return "Recording";
      if (mode == "stream") return "Streaming";
      return mode;
    }

    class Status : public Item {
    public:
      explicit Status(std::shared_ptr<RecState> state) : m_state(std::move(state)) {
        m_circle = static_cast<Box*>(addChild(ui::box({})));
        m_icon = static_cast<Label*>(addChild(makeIcon(0xf044a, 22.0F)));
        m_title = static_cast<Label*>(addChild(makeText("", 14.0F, true)));
        m_sub = static_cast<Label*>(addChild(makeText("", 11.0F, false, dim())));
        sync();
      }

      void sync() override {
        const RecState& s = *m_state;
        const ColorSpec to = s.active() ? (s.mode == "replay" ? accent() : danger()) : textA(0.08F);
        const std::string key = s.active() ? s.mode : "";
        if (m_first || key != m_colorKey) {
          if (m_first) {
            m_circle->setFill(to);
          } else {
            tweenColor(*m_circle, m_color, to, 200, [this](const ColorSpec& c) { m_circle->setFill(c); });
          }
          m_color = to;
          m_colorKey = key;
          m_first = false;
        }
        const char32_t cp = s.mode == "replay" ? 0xf0450 : s.mode == "stream" ? 0xf0567 : 0xf044a;
        bool changed = m_icon->setText(utf8Of(cp));
        m_icon->setColor(s.active() ? bgPanel() : dim());
        changed = m_title->setText(modeTitle(s.mode)) || changed;
        std::string sub = s.active() && s.mode != "replay" ? elapsed(s)
                          : s.mode == "replay" ? "keeps the last " + std::to_string(get<int>("recorder.replay", 30)) + " s"
                                               : "";
        changed = m_sub->setText(sub) || changed;
        m_sub->setVisible(!sub.empty());
        if (changed) requestLayout();
      }

      float place(Renderer& renderer, float /*width*/) override {
        m_circle->setSize(46.0F, 46.0F);
        m_circle->setRadius(23.0F);
        m_icon->measure(renderer);
        m_title->measure(renderer);
        m_sub->measure(renderer);
        // An empty subtitle takes no height, so the title centres on the circle.
        const float colH = m_title->height() + (m_sub->visible() ? m_sub->height() : 0.0F);
        const float h = std::max(46.0F, colH);
        m_circle->setPosition(0.0F, 0.0F);
        m_icon->setPosition(std::round((46.0F - m_icon->width()) / 2.0F), std::round((46.0F - m_icon->height()) / 2.0F));
        const float cy = std::round((h - colH) / 2.0F);
        m_title->setPosition(46.0F + 14.0F, cy);
        m_sub->setPosition(46.0F + 14.0F, cy + m_title->height());
        setSize(46.0F + 14.0F + std::max(m_title->width(), m_sub->width()), h);
        return h;
      }

    private:
      std::shared_ptr<RecState> m_state;
      Box* m_circle = nullptr;
      Label* m_icon = nullptr;
      Label* m_title = nullptr;
      Label* m_sub = nullptr;
      ColorSpec m_color;
      std::string m_colorKey;
      bool m_first = true;
    };

    std::unique_ptr<Segmented> seg(const std::string& path, std::vector<Option> options, float width = 300.0F) {
      return std::make_unique<Segmented>(bind(path), std::move(options), width);
    }

  } // namespace

  void buildRecording(Column& page) {
    auto state = std::make_shared<RecState>();
    readState(*state);

    // `kusanagi record status` also catches recorders started elsewhere (gsr-ui hotkeys).
    {
      std::weak_ptr<RecState> weak = state;
      (void)process::runAsync(std::vector<std::string>{cliCommand(), "record", "status"},
                              process::RunCallbacks{
                                  .onExit = [weak](process::RunResult r) {
                                    DeferredCall::callLater([weak, out = std::move(r.out)]() {
                                      auto s = weak.lock();
                                      if (!s) return;
                                      parse(*s, out);
                                      readState(*s);
                                      refresh();
                                    });
                                  },
                              });
    }
    // record.json is rewritten after every action, and the elapsed time ticks while something runs.
    page.add<Poll>(1000, [state]() {
      const RecState before = *state;
      readState(*state);
      if (!(before == *state) || state->active()) refresh();
    });

    const auto act = [state](const char* what) {
      return [state, what]() {
        (void)ipc(std::string("record ") + what);
        readState(*state);
        refresh();
      };
    };

    {
      auto* g = page.add<Group>("Now");
      g->bindHint([state]() -> std::string {
        if (state->available.empty()) {
          return "No recorder found. Install gpu-screen-recorder (recording, replays, streaming) or wf-recorder (recording) — "
                 "`kusanagi doctor` prints the command for your distro.";
        }
        const std::string& b = state->backend;
        const std::string using_ = b == "gsr-ui" ? "GPU Screen Recorder (gsr-ui)"
                                   : b == "gsr"  ? "gpu-screen-recorder"
                                   : b == "wf-recorder" ? "wf-recorder"
                                                        : "gpu-screen-recorder / wf-recorder, whichever is installed";
        return "Using " + using_ + ". Add the Recorder module to the bar or the Record / Replay / Save clip tiles to the control panel.";
      });
      g->add<Status>(state);
      auto* flow = g->add<Flow>(8.0F);
      flow->add<Chip>("Record", 0xf044a)
          ->labelFrom([state]() { return std::string(state->mode == "record" ? "Stop recording" : "Record"); })
          ->onWhen([state]() { return state->mode == "record"; })
          ->onClick(act("record"))
          ->showIf([state]() { return state->has("record"); });
      flow->add<Chip>("Start replay", 0xf0450)
          ->labelFrom([state]() { return std::string(state->mode == "replay" ? "Stop replay" : "Start replay"); })
          ->onWhen([state]() { return state->mode == "replay"; })
          ->onClick(act("replay"))
          ->showIf([state]() { return state->has("replay"); });
      flow->add<Chip>("Save clip", 0xf0fd8)->onClick(act("save"))->showIf([state]() { return state->mode == "replay"; });
      flow->add<Chip>("Stream", 0xf0567)
          ->labelFrom([state]() { return std::string(state->mode == "stream" ? "Stop stream" : "Stream"); })
          ->onWhen([state]() { return state->mode == "stream"; })
          ->onClick(act("stream"))
          ->showIf([state]() { return state->has("stream"); });
      flow->add<Chip>("Open folder", 0xf024b)->onClick([]() {
        spawn({"xdg-open", expandHome(get<std::string>("recorder.folder", "~/Videos"))});
      });
    }

    {
      auto* g = page.add<Group>("Video", "Used when Kusanagi runs the recorder itself. With gsr-ui running, its own settings apply.");
      g->add<Row>("Frame rate", seg("recorder.fps", {{"30", 30}, {"60", 60}, {"120", 120}, {"144", 144}}));
      g->add<Row>("Quality",
                  seg("recorder.quality", {{"Medium", "medium"}, {"High", "high"}, {"Very high", "very_high"}, {"Ultra", "ultra"}}));
      g->add<Row>("Codec", "older AMD cards (RX 400/500): HEVC",
                  seg("recorder.codec", {{"Auto", "auto"}, {"H.264", "h264"}, {"HEVC", "hevc"}, {"AV1", "av1"}}));
      g->add<Row>("Capture", "screen = no prompts (needs gsr's KMS helper); portal asks once",
                  std::make_unique<Segmented>(
                      Binding{
                          .get = []() -> json { return get<std::string>("recorder.capture", "screen") == "portal" ? "portal" : "screen"; },
                          .set = [](const json& v) { set("recorder.capture", v); },
                      },
                      std::vector<Option>{{"Screen", "screen"}, {"Portal", "portal"}}, 300.0F));
      g->add<Row>("Sound", seg("recorder.audio", {{"Desktop", "desktop"}, {"Mic", "mic"}, {"Both", "both"}, {"None", "none"}}));
    }

    {
      auto* g = page.add<Group>("Replay and files");
      g->add<Row>("Replay keeps the last", seg("recorder.replay", {{"15 s", 15}, {"30 s", 30}, {"60 s", 60}, {"2 min", 120}}));
      g->add<Row>("Folder", "replays go in Replays/ inside it",
                  std::make_unique<Field>(bind("recorder.folder"), FieldOpts{.width = 260.0F, .applyOnEdit = true}));
      g->add<Row>("Stream to", "rtmp://… with your key (Twitch, YouTube…)",
                  std::make_unique<Field>(bind("recorder.streamUrl"),
                                          FieldOpts{.width = 260.0F, .placeholder = "rtmp://", .applyOnEdit = true}));
    }
  }

} // namespace kusanagi::sp
