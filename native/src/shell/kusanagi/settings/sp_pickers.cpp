// Font picker (every installed font drawn in itself, with only the visible rows created) and app picker
// (a search over the installed apps).

#include "core/deferred_call.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/animation/animation_manager.h"
#include "render/core/renderer.h"
#include "shell/kusanagi/settings/sp_kit.h"
#include "shell/settings/font_family_catalog.h"
#include "system/desktop_entry.h"
#include "system/icon_resolver.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/label.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <thread>
#include <unordered_set>
#include <wayland-client-protocol.h>

namespace kusanagi::sp {

  namespace {
    constexpr auto kPointer = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;
    constexpr float kRowH = 46.0F;
    constexpr float kRowStep = 48.0F; // Row height plus 2 px spacing.
    constexpr float kListH = 300.0F;

    std::string lower(std::string s) {
      for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return s;
    }

    std::string trim(const std::string& s) {
      std::size_t a = 0, b = s.size();
      while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
      while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
      return s.substr(a, b - a);
    }

    // One font: a sample in the font itself, its name under it, and a check mark on the current one.
    class FontRow : public InputArea {
    public:
      explicit FontRow(std::function<void(const std::string&)> picked) : m_picked(std::move(picked)) {
        m_bg = static_cast<Box*>(addChild(ui::box({})));
        m_bg->setRadius(9.0F);
        m_sample = static_cast<Label*>(addChild(makeText("", 15.0F)));
        m_sample->setMaxLines(1);
        m_sample->setEllipsize(TextEllipsize::End);
        m_name = static_cast<Label*>(addChild(makeText("", 10.0F, false, dim())));
        m_name->setMaxLines(1);
        m_check = static_cast<Label*>(addChild(makeIcon(0xf012c, 16.0F, accent())));
        setCursorShape(kPointer);
        setOnEnter([this](const PointerData&) { restyle(); });
        setOnLeave([this]() { restyle(); });
        setOnClick([this](const PointerData&) {
          if (m_picked) m_picked(m_family);
        });
      }

      void bind(const std::string& family, const std::string& sample, bool on) {
        if (family != m_family) {
          m_family = family;
          m_sample->setFontFamily(family);
          m_sample->setText(sample);
          m_name->setText(family);
          m_dirty = true;
        }
        m_on = on;
        restyle();
      }

      void restyle() {
        m_bg->setFill(m_on ? accent(0.16F) : hovered() ? textA(0.06F) : textA(0.0F));
        m_bg->setBorder(accent(0.5F), m_on ? 1.0F : 0.0F);
        m_check->setVisible(m_on);
      }

      void placeRow(Renderer& renderer, float w) {
        setSize(w, kRowH);
        m_bg->setSize(w, kRowH);
        m_sample->setMaxWidth(std::max(1.0F, w - 60.0F));
        m_sample->measure(renderer);
        m_name->measure(renderer);
        const float colH = m_sample->height() + 1.0F + m_name->height();
        const float y = std::round((kRowH - colH) / 2.0F);
        m_sample->setPosition(12.0F, y);
        m_name->setPosition(12.0F, y + m_sample->height() + 1.0F);
        m_check->measure(renderer);
        m_check->setPosition(w - 14.0F - m_check->width(), std::round((kRowH - m_check->height()) / 2.0F));
      }

      [[nodiscard]] const std::string& family() const noexcept { return m_family; }

    private:
      std::function<void(const std::string&)> m_picked;
      std::string m_family;
      bool m_on = false;
      bool m_dirty = true;
      Box* m_bg = nullptr;
      Label* m_sample = nullptr;
      Label* m_name = nullptr;
      Label* m_check = nullptr;
    };
  } // namespace

  const std::vector<std::string>* installedFonts() {
    static const std::vector<std::string>* ready = nullptr;
    static bool started = false;
    if (ready != nullptr || started) return ready;
    started = true;
    std::thread([]() {
      const auto& families = settings::discoverFontFamilies();
      DeferredCall::callLater([&families]() {
        ready = &families;
        refresh();
      });
    }).detach();
    return nullptr;
  }

  // FontPicker

  struct FontPicker::Rows {
    std::vector<FontRow*> pool;
  };

  FontPicker::FontPicker(Binding binding, std::string sample)
      : m_binding(std::move(binding)), m_sample(std::move(sample)), m_rows(std::make_unique<Rows>()) {
    m_search = static_cast<Field*>(addChild(std::make_unique<Field>(
        std::nullopt, FieldOpts{
                          .width = 400.0F,
                          .placeholder = "Search fonts…",
                          .icon = 0xf0349,
                          .onEdited = [this](const std::string&) {
                            filter();
                            requestLayout();
                          },
                      }
    )));
    m_count = static_cast<Label*>(addChild(makeText("0 shown", 10.0F, false, dim())));
    m_box = static_cast<Box*>(addChild(ui::box({})));
    m_box->setRadius(12.0F);
    m_box->setFill(textA(0.03F));
    m_box->setClipChildren(true);
    m_viewport = static_cast<InputArea*>(m_box->addChild(ui::inputArea({})));
    m_viewport->setClipChildren(true);
    m_viewport->setOnAxisHandler([this](const InputArea::PointerData& d) {
      if (d.axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return false;
      scrollTo(m_offset + d.scrollDelta(60.0F));
      return true;
    });
    m_content = m_viewport->addChild(ui::node({}));
    m_indicator = static_cast<Box*>(m_box->addChild(ui::box({})));
    m_indicator->setFill(textA(0.2F));
    m_indicator->setRadius(1.5F);
    m_current = m_binding.get().is_string() ? m_binding.get().get<std::string>() : std::string();

    // fc-list takes a moment the first time, so list the fonts off the UI thread.
    std::weak_ptr<int> alive = m_alive;
    std::thread([this, alive]() {
      const auto& families = settings::discoverFontFamilies();
      DeferredCall::callLater([this, alive, &families]() {
        if (alive.expired()) return;
        // Some fonts list every style as its own family: keep one per name, Nerd Fonts first.
        std::unordered_set<std::string> seen;
        for (const auto& f : families) {
          if (f.empty() || f[0] == '.' || !seen.insert(f).second) continue;
          m_all.push_back(f);
        }
        std::ranges::stable_sort(m_all, [](const std::string& a, const std::string& b) {
          const bool na = a.find("Nerd") != std::string::npos;
          const bool nb = b.find("Nerd") != std::string::npos;
          if (na != nb) return na;
          return lower(a) < lower(b);
        });
        m_loaded = true;
        m_search->setPlaceholder("Search " + std::to_string(m_all.size()) + " fonts…");
        filter();
        requestLayout();
      });
    }).detach();
  }

  FontPicker::~FontPicker() = default;

  void FontPicker::filter() {
    const std::string q = lower(trim(m_search->text()));
    m_shownFonts.clear();
    for (const auto& f : m_all) {
      if (q.empty() || lower(f).find(q) != std::string::npos) m_shownFonts.push_back(f);
    }
    m_count->setText(std::to_string(m_shownFonts.size()) + " shown");
    m_offset = 0.0F;
  }

  void FontPicker::scrollTo(float offset) {
    const float contentH = std::max(0.0F, static_cast<float>(m_shownFonts.size()) * kRowStep - 2.0F);
    const float viewH = kListH - 12.0F;
    const float clamped = std::clamp(offset, 0.0F, std::max(0.0F, contentH - viewH));
    if (clamped == m_offset) return;
    m_offset = clamped;
    requestLayout();
  }

  void FontPicker::sync() {
    const json v = m_binding.get();
    const std::string cur = v.is_string() ? v.get<std::string>() : std::string();
    if (cur == m_current) return;
    m_current = cur;
    for (FontRow* r : m_rows->pool) r->bind(r->family(), m_sample, r->family() == m_current);
  }

  float FontPicker::place(Renderer& renderer, float width) {
    width = widthFor(width);
    m_count->measure(renderer);
    const float fieldW = std::max(1.0F, width - m_count->width() - 10.0F);
    m_search->setFixedWidth(fieldW);
    const float fh = m_search->place(renderer, fieldW);
    m_search->setPosition(0.0F, 0.0F);
    m_count->setPosition(fieldW + 10.0F, std::round((fh - m_count->height()) / 2.0F));

    const float boxY = fh + 8.0F;
    m_box->setPosition(0.0F, boxY);
    m_box->setSize(width, kListH);
    const float listW = width - 12.0F;
    const float viewH = kListH - 12.0F;
    m_viewport->setPosition(6.0F, 6.0F);
    m_viewport->setSize(listW, viewH);

    // Only the rows on screen exist.
    const auto n = static_cast<int>(m_shownFonts.size());
    const float contentH = std::max(0.0F, static_cast<float>(n) * kRowStep - 2.0F);
    m_offset = std::clamp(m_offset, 0.0F, std::max(0.0F, contentH - viewH));
    const int first = static_cast<int>(std::floor(m_offset / kRowStep));
    const int visible = static_cast<int>(std::ceil(viewH / kRowStep)) + 1;
    while (static_cast<int>(m_rows->pool.size()) < visible) {
      auto row = std::make_unique<FontRow>([this](const std::string& family) {
        m_binding.set(family);
        sync();
      });
      m_rows->pool.push_back(static_cast<FontRow*>(m_content->addChild(std::move(row))));
    }
    for (int i = 0; i < static_cast<int>(m_rows->pool.size()); ++i) {
      FontRow* r = m_rows->pool[static_cast<std::size_t>(i)];
      const int idx = first + i;
      if (idx >= n || i >= visible) {
        r->setVisible(false);
        continue;
      }
      r->setVisible(true);
      const std::string& family = m_shownFonts[static_cast<std::size_t>(idx)];
      r->bind(family, m_sample, family == m_current);
      r->placeRow(renderer, listW);
      r->setPosition(0.0F, std::round(static_cast<float>(idx) * kRowStep - m_offset));
    }
    m_indicator->setVisible(contentH > viewH);
    if (contentH > viewH) {
      const float ratio = viewH / contentH;
      const float pos = m_offset / contentH;
      m_indicator->setPosition(width - 5.0F, 6.0F + pos * (kListH - 12.0F));
      m_indicator->setSize(3.0F, ratio * (kListH - 12.0F));
    }
    const float h = boxY + kListH;
    setSize(width, h);
    return h;
  }

  // AppPicker

  struct AppPicker::Result : public InputArea {
    explicit Result(AppPicker& owner) : picker(owner) {
      bg = static_cast<Box*>(addChild(ui::box({})));
      bg->setRadius(10.0F);
      icon = static_cast<Image*>(addChild(ui::image({.fit = ImageFit::Contain})));
      name = static_cast<Label*>(addChild(makeText("", 12.0F)));
      name->setMaxLines(1);
      generic = static_cast<Label*>(addChild(makeText("", 10.0F, false, dim())));
      generic->setMaxLines(1);
      plus = static_cast<Label*>(addChild(makeIcon(0xf0415, 16.0F, accent())));
      setCursorShape(kPointer);
      setOnEnter([this](const PointerData&) { restyle(true); });
      setOnLeave([this]() { restyle(true); });
      setOnClick([this](const PointerData&) {
        const std::string picked = id;
        picker.m_query->setText("");
        picker.m_dirty = true;
        if (picker.m_picked) picker.m_picked(picked);
        requestLayout();
      });
      restyle(false);
    }

    void restyle(bool animate) {
      const ColorSpec c = hovered() ? accent(0.14F) : textA(0.04F);
      if (animate) {
        tweenColor(*bg, color, c, 120, [this](const ColorSpec& col) { bg->setFill(col); });
      } else {
        bg->setFill(c);
      }
      color = c;
    }

    AppPicker& picker;
    std::string id;
    std::string iconPath;
    std::string loadedIcon;
    Box* bg = nullptr;
    Image* icon = nullptr;
    Label* name = nullptr;
    Label* generic = nullptr;
    Label* plus = nullptr;
    ColorSpec color;
  };

  AppPicker::AppPicker(std::function<void(const std::string&)> picked, std::function<std::vector<std::string>()> exclude,
                       int max)
      : m_picked(std::move(picked)), m_exclude(std::move(exclude)), m_max(max) {
    m_query = static_cast<Field*>(addChild(std::make_unique<Field>(
        std::nullopt, FieldOpts{
                          .width = 400.0F,
                          .placeholder = "Search apps to add…",
                          .icon = 0xf0349,
                          .onEdited = [this](const std::string&) {
                            m_dirty = true;
                            requestLayout();
                          },
                          .onAccepted = [this](const std::string&) {
                            refresh();
                            if (!m_ids.empty() && m_picked) {
                              const std::string first = m_ids.front();
                              m_query->setText("");
                              m_dirty = true;
                              m_picked(first);
                              requestLayout();
                            }
                          },
                      }
    )));
  }

  void AppPicker::refresh() {
    m_dirty = false;
    m_ids.clear();
    for (Result* r : m_results) removeChild(r);
    m_results.clear();
    const std::string t = lower(trim(m_query->text()));
    if (t.empty()) return;
    const std::vector<std::string> exclude = m_exclude ? m_exclude() : std::vector<std::string>{};
    std::vector<const DesktopEntry*> found;
    for (const auto& e : desktopEntries()) {
      if (e.noDisplay || std::ranges::find(exclude, e.id) != exclude.end()) continue;
      if (e.nameLower.find(t) != std::string::npos || e.genericNameLower.find(t) != std::string::npos
          || e.idLower.find(t) != std::string::npos) {
        found.push_back(&e);
      }
    }
    // Sort by where the query appears in the name. Entries that only matched the id or generic name
    // sort first.
    std::ranges::stable_sort(found, [&t](const DesktopEntry* a, const DesktopEntry* b) {
      const auto pa = static_cast<long>(a->nameLower.find(t));
      const auto pb = static_cast<long>(b->nameLower.find(t));
      return (pa == static_cast<long>(std::string::npos) ? -1 : pa) < (pb == static_cast<long>(std::string::npos) ? -1 : pb);
    });
    if (static_cast<int>(found.size()) > m_max) found.resize(static_cast<std::size_t>(m_max));
    static IconResolver resolver;
    for (const DesktopEntry* e : found) {
      auto r = std::make_unique<Result>(*this);
      r->id = e->id;
      r->name->setText(e->name);
      r->generic->setText(e->genericName);
      r->generic->setVisible(!e->genericName.empty());
      r->iconPath = e->icon.empty() ? std::string() : (e->icon[0] == '/' ? e->icon : resolver.resolve(e->icon, 44));
      m_ids.push_back(e->id);
      m_results.push_back(static_cast<Result*>(addChild(std::move(r))));
    }
  }

  float AppPicker::place(Renderer& renderer, float width) {
    width = widthFor(width);
    if (m_dirty) refresh();
    m_query->setFixedWidth(width);
    float y = m_query->place(renderer, width);
    m_query->setPosition(0.0F, 0.0F);
    for (Result* r : m_results) {
      y += 6.0F;
      r->setPosition(0.0F, y);
      r->setSize(width, 38.0F);
      r->bg->setSize(width, 38.0F);
      if (r->loadedIcon != r->iconPath) {
        r->loadedIcon = r->iconPath;
        if (!r->iconPath.empty()) (void)r->icon->setSourceFile(renderer, r->iconPath, 44);
      }
      r->icon->setSize(22.0F, 22.0F);
      r->icon->setPosition(10.0F, 8.0F);
      r->name->measure(renderer);
      r->generic->measure(renderer);
      const float colH = r->name->height() + (r->generic->visible() ? r->generic->height() : 0.0F);
      const float cy = std::round((38.0F - colH) / 2.0F);
      r->name->setPosition(42.0F, cy);
      r->generic->setPosition(42.0F, cy + r->name->height());
      r->plus->measure(renderer);
      r->plus->setPosition(width - 12.0F - r->plus->width(), std::round((38.0F - r->plus->height()) / 2.0F));
      y += 38.0F;
    }
    setSize(width, y);
    return y;
  }

} // namespace kusanagi::sp
