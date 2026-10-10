// Settings > Updates: pending package updates from the shell's check (updates:: in kusanagi_ipc.h),
// installing them in a terminal, and how often to check.

#include "render/core/renderer.h"
#include "shell/kusanagi/kusanagi_ipc.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/kusanagi/settings/sp_pages.h"
#include "ui/controls/label.h"

#include <ctime>
#include <memory>

namespace kusanagi::sp {

  namespace {

    std::string hhmm(long long ms) {
      const std::time_t t = static_cast<std::time_t>(ms / 1000);
      std::tm tm{};
      localtime_r(&t, &tm);
      char buf[8];
      std::strftime(buf, sizeof(buf), "%H:%M", &tm);
      return buf;
    }

    class UpdateLine : public Item {
    public:
      UpdateLine(const std::string& name, const std::string& source) {
        m_name = static_cast<Label*>(addChild(makeText(name, 11.0F)));
        m_name->setMaxLines(1);
        m_name->setEllipsize(TextEllipsize::End);
        m_source = static_cast<Label*>(addChild(makeText(source, 11.0F, false, dim())));
        m_source->setMaxLines(1);
      }
      float place(Renderer& renderer, float width) override {
        m_name->setMaxWidth(360.0F);
        m_name->measure(renderer);
        m_source->measure(renderer);
        m_name->setPosition(0.0F, 0.0F);
        m_source->setPosition(370.0F, 0.0F);
        const float h = std::max(m_name->height(), m_source->height());
        setSize(widthFor(width), h);
        return h;
      }

    private:
      Label* m_name = nullptr;
      Label* m_source = nullptr;
    };

  } // namespace

  void buildUpdates(Column& page) {
    // The update state lives in the shell, so poll it while the page is open.
    auto last = std::make_shared<updates::Status>(updates::status());
    page.add<Poll>(400, [last]() {
      const updates::Status s = updates::status();
      if (s.checking == last->checking && s.count == last->count && s.known == last->known && s.checkedAt == last->checkedAt) return;
      *last = s;
      refresh();
    });

    {
      auto* g = page.add<Group>(" ", "Checking never needs your password and never installs anything.");
      g->bindTitle([]() -> std::string {
        const updates::Status s = updates::status();
        if (s.checking) return "Checking…";
        if (!s.known) return "Not checked yet";
        if (s.count == 0) return "Up to date";
        return std::to_string(s.count) + (s.count == 1 ? " update waiting" : " updates waiting");
      });
      g->bindHint([]() -> std::string {
        const updates::Status s = updates::status();
        const std::string tail = "Checking never needs your password and never installs anything.";
        return s.checkedAt != 0 ? "Last check " + hhmm(s.checkedAt) + ". " + tail : tail;
      });
      auto* flow = g->add<Flow>(8.0F);
      flow->add<Chip>("Check now", 0xf0450)->onClick([]() {
        updates::check();
        refresh();
      });
      flow->add<Chip>("Install in a terminal", 0xf06b0)
          ->onWhen([]() { return true; })
          ->onClick([]() { updates::upgrade(); })
          ->showIf([]() { return updates::status().count > 0; });
      auto* list = g->add<Column>(4.0F);
      list->showIf([]() { return !updates::status().list.empty(); });
      list->add<Repeater>(
          []() {
            // Name and source, so a new check rebuilds the lines.
            std::vector<std::string> keys;
            const auto l = updates::status().list;
            for (std::size_t i = 0; i < l.size() && i < 40; ++i) keys.push_back(l[i].first + "\t" + l[i].second);
            return keys;
          },
          [](const std::string& key) -> std::unique_ptr<Item> {
            const auto tab = key.find('\t');
            return std::make_unique<UpdateLine>(key.substr(0, tab), key.substr(tab + 1));
          },
          4.0F
      );
      list->add<Text>("", TextOpts{.px = 11.0F, .color = dim()})
          ->bindText([]() {
            const std::size_t n = updates::status().list.size();
            return n > 40 ? "… and " + std::to_string(n - 40) + " more" : std::string();
          })
          ->showIf([]() { return updates::status().list.size() > 40; });
    }

    {
      auto* g = page.add<Group>("Checking", "Checks run only while the Updates module is on a bar (Settings → Bar) or notifications are on.");
      g->add<Row>("Check every",
                  std::make_unique<Segmented>(bind("updates.interval"),
                                              std::vector<Option>{{"Never", 0}, {"1 h", 1}, {"3 h", 3}, {"6 h", 6}, {"12 h", 12}}, 340.0F));
      g->add<Row>("Notify me", "a notification when new updates show up", std::make_unique<Switch>(bind("updates.notify")));
    }
  }

} // namespace kusanagi::sp
