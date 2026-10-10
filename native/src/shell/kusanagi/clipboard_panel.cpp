#include "shell/kusanagi/clipboard_panel.h"

#include "core/deferred_call.h"
#include "core/log.h"
#include "core/process/process.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/core/async_texture_cache.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/panel/panel_manager.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/flex.h"
#include "ui/controls/image.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/scroll_view.h"
#include "ui/controls/virtual_list_view.h"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace {

  constexpr Logger kLog("kusanagi-clipboard");

  constexpr const char* kIconFont = "JetBrainsMono Nerd Font";
  constexpr float kFieldH = 60.0F;
  constexpr float kTextRowH = 44.0F;
  constexpr float kImageRowH = 76.0F;
  constexpr float kListW = 400.0F;

  std::string utf8(char32_t c) {
    std::string out;
    out += static_cast<char>(0xF0 | (c >> 18));
    out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
    return out;
  }

  std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }

  std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
  }

  // Collapses runs of whitespace into one space and trims both ends.
  std::string oneLine(std::string_view s) {
    std::string out;
    bool space = false;
    for (const char c : s) {
      if (std::isspace(static_cast<unsigned char>(c))) {
        space = true;
        continue;
      }
      if (space && !out.empty()) out += ' ';
      space = false;
      out += c;
    }
    return out;
  }

  // #rgb, #rrggbb or #aarrggbb. Eight digits put alpha first, so they are reordered for tryParseHexColor.
  bool parseColour(const std::string& value, Color& out) {
    const std::string v = trim(value);
    if (v.size() < 4 || v[0] != '#') return false;
    const std::string digits = v.substr(1);
    if (digits.size() != 3 && digits.size() != 6 && digits.size() != 8) return false;
    if (!std::ranges::all_of(digits, [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; })) {
      return false;
    }
    const std::string rgba = digits.size() == 8 ? "#" + digits.substr(2) + digits.substr(0, 2) : v;
    return tryParseHexColor(rgba, out);
  }

  std::string cacheDir() {
    const char* xdg = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    const std::string base = xdg != nullptr && *xdg != '\0' ? xdg : std::string(home != nullptr ? home : "") + "/.cache";
    return base + "/kusanagi/clip";
  }

  // Prints "id<TAB>txt<TAB>text" or "id<TAB>img<TAB>/path/to/preview" per entry. Only the 40 newest images are
  // decoded, each once, and cached previews of deleted entries are removed.
  constexpr const char* kListScript = R"SH(
cache="$1"
mkdir -p "$cache"
tab=$(printf '\t')
cliphist list | head -n 300 | {
    n=0
    while IFS="$tab" read -r id rest; do
        case "$rest" in
            "[[ binary data"*)
                case "$rest" in
                    *jpeg*|*jpg*) ext=jpg ;; *webp*) ext=webp ;; *gif*) ext=gif ;; *bmp*) ext=bmp ;; *) ext=png ;;
                esac
                f="$cache/$id.$ext"
                if [ ! -s "$f" ] && [ "$n" -lt 40 ]; then
                    cliphist decode "$id" > "$f" 2>/dev/null
                fi
                n=$((n + 1))
                printf '%s\timg\t%s\n' "$id" "$f" ;;
            *)
                printf '%s\ttxt\t%s\n' "$id" "$rest" ;;
        esac
    done
}
ids=$(cliphist list | cut -f1)
ls "$cache" | while read -r f; do
    printf '%s\n' "$ids" | grep -qx "${f%%.*}" || rm -f "$cache/$f"
done 2>/dev/null
)SH";

  bool validId(const std::string& id) {
    return !id.empty() && std::ranges::all_of(id, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
  }

  class ClipRow : public Node {
  public:
    ClipRow() {
      addChild(ui::box({.out = &m_swatch}));
      addChild(ui::label({.out = &m_icon, .maxLines = 1}));
      addChild(ui::label({.out = &m_text, .maxLines = 1, .ellipsize = TextEllipsize::End}));
      addChild(ui::box({.out = &m_thumbBg}));
      addChild(ui::image({.out = &m_thumb, .fit = ImageFit::Cover}));
      addChild(ui::label({.out = &m_imageLabel, .text = "Image", .maxLines = 1}));
      m_thumb->setAsyncReadyCallback([]() { PanelManager::instance().refresh(); });
    }

    void bind(Renderer& renderer, AsyncTextureCache* cache, const KusanagiClipboardPanel::Entry& e, bool selected, float s) {
      m_entry = &e;
      m_selected = selected;
      m_s = s;
      if (e.image) {
        if (cache != nullptr && m_thumb->sourcePath() != e.value) {
          (void)m_thumb->setSourceFileAsync(renderer, *cache, e.value, static_cast<int>(std::lround(208.0F * s)));
        }
      } else if (m_thumb->hasImage() || !m_thumb->sourcePath().empty()) {
        m_thumb->clear(renderer);
      }
      markLayoutDirty();
    }

  protected:
    void doLayout(Renderer& renderer) override {
      if (m_entry == nullptr) return;
      const auto& e = *m_entry;
      const float s = m_s, w = width(), h = height();
      const std::string font = kusanagi::font();

      // The selection highlight belongs to the panel so it can slide between rows.
      m_swatch->setVisible(!e.image && e.colour);
      m_icon->setVisible(!e.image && !e.colour);
      m_text->setVisible(!e.image);
      m_thumbBg->setVisible(e.image);
      m_thumb->setVisible(e.image);
      m_imageLabel->setVisible(e.image);

      if (e.image) {
        const float tw = 104.0F * s, th = 60.0F * s, ty = std::round((h - th) / 2);
        m_thumbBg->setPosition(10.0F * s, ty);
        m_thumbBg->setSize(tw, th);
        m_thumbBg->setRadius(6.0F * s);
        m_thumbBg->setFill(colorSpecFromRole(ColorRole::OnSurface, 0.06F));
        m_thumb->setPosition(10.0F * s, ty);
        m_thumb->setSize(tw, th);
        m_thumb->setRadius(6.0F * s);
        m_imageLabel->setFontFamily(font);
        m_imageLabel->setFontSize(12.0F * s);
        m_imageLabel->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
        m_imageLabel->measure(renderer);
        m_imageLabel->setPosition(128.0F * s, std::round((h - m_imageLabel->height()) / 2));
        return;
      }

      if (e.colour) {
        const float d = 16.0F * s;
        m_swatch->setPosition(12.0F * s, std::round((h - d) / 2));
        m_swatch->setSize(d, d);
        m_swatch->setRadius(4.0F * s);
        m_swatch->setFill(fixedColorSpec(e.swatch));
        m_swatch->setBorder(colorSpecFromRole(ColorRole::OnSurface, 0.2F), 1.0F * s);
      } else {
        m_icon->setText(utf8(e.url ? 0xf0337 : 0xf0284));
        m_icon->setFontFamily(kIconFont);
        m_icon->setFontSize(15.0F * s);
        m_icon->setColor(colorSpecFromRole(m_selected ? ColorRole::Primary : ColorRole::OnSurfaceVariant));
        m_icon->measure(renderer);
        m_icon->setPosition(12.0F * s + std::round((15.0F * s - m_icon->width()) / 2), std::round((h - m_icon->height()) / 2));
      }
      m_text->setText(e.oneLine);
      m_text->setFontFamily(font);
      m_text->setFontSize(12.0F * s);
      m_text->setColor(colorSpecFromRole(ColorRole::OnSurface, m_selected ? 1.0F : 0.8F));
      m_text->setMaxWidth(std::max(0.0F, w - 38.0F * s - 12.0F * s));
      m_text->measure(renderer);
      m_text->setPosition(38.0F * s, std::round((h - m_text->height()) / 2));
    }

  private:
    const KusanagiClipboardPanel::Entry* m_entry = nullptr;
    bool m_selected = false;
    float m_s = 1.0F;
    Box* m_swatch = nullptr;
    Label* m_icon = nullptr;
    Label* m_text = nullptr;
    Box* m_thumbBg = nullptr;
    Image* m_thumb = nullptr;
    Label* m_imageLabel = nullptr;
  };

  float outBack(float t, float s) {
    const float f = t - 1.0F;
    return 1.0F + (s + 1.0F) * f * f * f + s * f * f;
  }
  float outQuint(float t) { return 1.0F - std::pow(1.0F - t, 5.0F); }

} // namespace

class KusanagiClipboardPanel::Adapter : public VirtualListAdapter {
public:
  explicit Adapter(KusanagiClipboardPanel& panel) : m_panel(panel) {}

  void setScale(float s) { m_scale = s; }

  [[nodiscard]] std::size_t itemCount() const override { return m_panel.m_filtered.size(); }
  [[nodiscard]] std::uint64_t itemKey(std::size_t index) const override {
    return index < m_panel.m_filtered.size() ? m_panel.m_entries[m_panel.m_filtered[index]].key : index;
  }
  [[nodiscard]] std::uint64_t itemRevision(std::size_t index) const override {
    return index == m_panel.m_selected ? 1 : 0;
  }
  [[nodiscard]] bool itemInteractive(std::size_t /*index*/) const override { return true; }

  [[nodiscard]] float measureItem(Renderer& /*renderer*/, std::size_t index, float /*width*/) override {
    return m_panel.rowHeight(index);
  }

  [[nodiscard]] std::unique_ptr<Node> createItem() override { return std::make_unique<ClipRow>(); }

  void bindItem(Renderer& renderer, Node& item, std::size_t index, float /*width*/, bool hovered) override {
    if (index >= m_panel.m_filtered.size()) return;
    const bool selected = index == m_panel.m_selected;
    static_cast<ClipRow&>(item).bind(
        renderer, m_panel.m_asyncTextures, m_panel.m_entries[m_panel.m_filtered[index]], selected, m_scale
    );
    // Hovering a row selects it.
    if (hovered && !selected) {
      DeferredCall::callLater([panel = &m_panel, index]() { panel->select(index, false); });
    }
  }

  void onActivate(std::size_t index) override {
    DeferredCall::callLater([panel = &m_panel, index]() { panel->copy(index); });
  }
  // Middle click removes the row.
  [[nodiscard]] bool acceptsMiddleClick() const override { return true; }
  void onMiddleClick(std::size_t index) override {
    DeferredCall::callLater([panel = &m_panel, index]() { panel->remove(index); });
  }

private:
  KusanagiClipboardPanel& m_panel;
  float m_scale = 1.0F;
};

KusanagiClipboardPanel::KusanagiClipboardPanel(AsyncTextureCache* asyncTextures)
    : m_asyncTextures(asyncTextures), m_adapter(std::make_unique<Adapter>(*this)) {}

KusanagiClipboardPanel::~KusanagiClipboardPanel() { ++*m_generation; }

InputArea* KusanagiClipboardPanel::initialFocusArea() const {
  return m_input != nullptr ? m_input->inputArea() : nullptr;
}

void KusanagiClipboardPanel::create() {
  auto root = ui::node({.out = &m_rootNode});

  m_rootNode->addChild(ui::box({.out = &m_backdrop}));
  m_rootNode->addChild(ui::inputArea({
      .out = &m_backdropArea,
      .onClick = [](const InputArea::PointerData&) { PanelManager::instance().closePanel(); },
  }));

  m_rootNode->addChild(ui::node({.out = &m_cardGroup}));
  m_cardGroup->addChild(ui::box({.out = &m_shadow}));
  m_cardGroup->addChild(ui::box({.out = &m_card}));
  m_card->setClipChildren(true);
  m_card->addChild(ui::inputArea({.out = &m_cardArea}));

  // Search field
  m_card->addChild(ui::label({.out = &m_fieldIcon, .maxLines = 1}));
  m_card->addChild(ui::label({.out = &m_placeholder, .maxLines = 1}));
  m_card->addChild(ui::input({
      .out = &m_input,
      .fontSize = 16.0F,
      .controlHeight = 36.0F,
      .horizontalPadding = 0.0F,
      .clearButtonEnabled = false,
      .frameVisible = false,
      .onChange = [this](const std::string& text) {
        m_query = text;
        applyFilter();
        PanelManager::instance().requestLayout();
      },
  }));
  auto wipeChip = ui::inputArea({
      .out = &m_wipeArea,
      .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
      .onEnter = [this](const InputArea::PointerData&) {
        m_wipeHover = true;
        PanelManager::instance().requestLayout();
      },
      .onLeave = [this]() {
        m_wipeHover = false;
        PanelManager::instance().requestLayout();
      },
      .onClick = [this](const InputArea::PointerData&) { DeferredCall::callLater([this]() { wipe(); }); },
  });
  wipeChip->addChild(ui::box({.out = &m_wipeFace}));
  wipeChip->addChild(ui::label({.out = &m_wipeIcon, .maxLines = 1}));
  wipeChip->addChild(ui::label({.out = &m_wipeLabel, .text = "Clear all", .maxLines = 1}));
  m_card->addChild(std::move(wipeChip));
  m_card->addChild(ui::box({.out = &m_separator}));

  m_card->addChild(ui::virtualListView({
      .out = &m_list,
      .itemGap = 0.0F,
      .overscanItems = 2,
      .adapter = m_adapter.get(),
      .configure =
          [](VirtualListView& list) {
            list.scrollView().setScrollbarVisible(false);
            list.scrollView().setViewportPaddingH(0.0F);
            list.scrollView().setViewportPaddingV(0.0F);
          },
  }));

  // Preview of the selection
  m_card->addChild(ui::box({.out = &m_previewCard}));
  m_previewCard->setClipChildren(true);
  auto scroll = ui::scrollView({
      .out = &m_previewScroll,
      .scrollbarVisible = false,
      .viewportPaddingH = 0.0F,
      .viewportPaddingV = 0.0F,
  });
  scroll->content()->setDirection(FlexDirection::Vertical);
  scroll->content()->setAlign(FlexAlign::Start);
  scroll->content()->addChild(ui::label({.out = &m_previewText, .maxLines = 0}));
  m_previewCard->addChild(std::move(scroll));
  m_previewCard->addChild(ui::box({.out = &m_previewSwatch}));
  m_previewCard->addChild(ui::image({.out = &m_previewImage, .fit = ImageFit::Contain}));
  m_previewImage->setAsyncReadyCallback([]() { PanelManager::instance().refresh(); });
  m_previewCard->addChild(ui::label({.out = &m_emptyIcon, .maxLines = 1}));
  m_previewCard->addChild(ui::label({.out = &m_emptyLabel, .text = "Nothing selected", .maxLines = 1}));

  setRoot(std::move(root));
  if (m_animations != nullptr) m_rootNode->setAnimationManager(m_animations);
  m_highlight.attach(m_list->scrollView(), m_animations);
  m_highlightReset = true;
}

void KusanagiClipboardPanel::onOpen(std::string_view /*context*/) {
  m_query.clear();
  m_selected = 0;
  m_previewId.clear();
  m_wipeHover = false;
  load();
  startOpenAnimation();
}

void KusanagiClipboardPanel::onClose() {
  ++*m_generation; // a listing still on its way is dropped
  m_loading = false;
  m_entries.clear();
  m_entries.shrink_to_fit();
  m_filtered.clear();
  m_filtered.shrink_to_fit();
  m_rootNode = nullptr;
  m_backdrop = nullptr;
  m_backdropArea = nullptr;
  m_cardGroup = nullptr;
  m_shadow = m_card = m_wipeFace = m_separator = m_previewCard = m_previewSwatch = nullptr;
  m_cardArea = m_wipeArea = nullptr;
  m_fieldIcon = m_placeholder = m_wipeIcon = m_wipeLabel = m_previewText = m_emptyIcon = m_emptyLabel = nullptr;
  m_input = nullptr;
  m_list = nullptr;
  m_highlight.detach();
  m_previewScroll = nullptr;
  m_previewImage = nullptr;
}

void KusanagiClipboardPanel::load() {
  if (!process::commandExists("cliphist")) {
    kLog.warn("cliphist isn't installed: no clipboard history");
    setEntries({});
    return;
  }
  m_loading = true;
  const std::uint64_t gen = ++*m_generation;
  std::weak_ptr<std::uint64_t> alive = m_generation;
  auto out = std::make_shared<std::string>();
  process::RunCallbacks callbacks;
  callbacks.stdOut = [out](std::string_view chunk) { out->append(chunk); };
  callbacks.onExit = [this, alive, gen, out](process::RunResult /*result*/) {
    DeferredCall::callLater([this, alive, gen, text = std::move(*out)]() {
      const auto g = alive.lock();
      if (!g || *g != gen) return; // closed or reopened meanwhile
      m_loading = false;
      setEntries(text);
      PanelManager::instance().requestLayout();
    });
  };
  if (!process::runAsync(std::vector<std::string>{"sh", "-c", kListScript, "sh", cacheDir()}, std::move(callbacks))) {
    m_loading = false;
    kLog.warn("couldn't list the clipboard history");
  }
}

void KusanagiClipboardPanel::setEntries(const std::string& listing) {
  m_entries.clear();
  std::istringstream lines(listing);
  std::string line;
  while (std::getline(lines, line)) {
    const auto t1 = line.find('\t');
    if (t1 == std::string::npos) continue;
    const auto t2 = line.find('\t', t1 + 1);
    if (t2 == std::string::npos) continue;
    Entry e;
    e.id = line.substr(0, t1);
    if (!validId(e.id)) continue;
    e.key = std::strtoull(e.id.c_str(), nullptr, 10);
    e.image = line.compare(t1 + 1, t2 - t1 - 1, "img") == 0;
    e.value = line.substr(t2 + 1);
    if (!e.image) {
      e.oneLine = oneLine(e.value);
      e.lower = lower(e.value);
      e.colour = parseColour(e.value, e.swatch);
      e.url = e.value.starts_with("http://") || e.value.starts_with("https://");
    }
    m_entries.push_back(std::move(e));
  }
  applyFilter();
}

void KusanagiClipboardPanel::applyFilter() {
  const std::string q = lower(m_query);
  m_filtered.clear();
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    const auto& e = m_entries[i];
    if (q.empty() || (!e.image && e.lower.find(q) != std::string::npos)) m_filtered.push_back(i);
  }
  m_selected = 0;
  m_highlightReset = true;
  if (m_list != nullptr) {
    m_list->notifyDataChanged();
    m_list->scrollView().setScrollOffset(0.0F);
  }
}

float KusanagiClipboardPanel::rowHeight(std::size_t index) const {
  const bool image = index < m_filtered.size() && m_entries[m_filtered[index]].image;
  return (image ? kImageRowH : kTextRowH) * contentScale();
}

void KusanagiClipboardPanel::syncHighlight(bool animate) {
  if (!m_highlight.attached() || m_list == nullptr) return;
  const bool shown = m_selected < m_filtered.size();
  m_highlight.setVisible(shown);
  if (!shown) {
    m_highlightReset = true;
    return;
  }
  const float s = contentScale();
  Box* box = m_highlight.box();
  box->setRadius(std::max(6.0F, kusanagi::radius() - 6.0F) * s);
  box->setFill(colorSpecFromRole(ColorRole::Primary, 0.16F));
  box->setBorder(colorSpecFromRole(ColorRole::Primary, 0.35F), 1.0F * s);
  // Moves take 160 ms whatever the distance. Resizes between text and image rows take 160 ms, or less when
  // 400 px/s gets there sooner.
  const auto d = static_cast<float>(kusanagi::ms(160));
  m_highlight.setMotion({.durationMs = d, .velocity = -1.0F}, {.durationMs = d, .velocity = 400.0F * s});
  float top = 0.0F;
  for (std::size_t i = 0; i < m_selected; ++i) top += rowHeight(i);
  const kusanagi::SlideHighlight::Rect rect{0.0F, top, m_listW, rowHeight(m_selected)};
  const auto& at = m_highlight.target();
  if (!m_highlightReset && at.x == rect.x && at.y == rect.y && at.w == rect.w && at.h == rect.h) return;
  m_highlight.moveTo(rect, animate && !m_highlightReset);
  m_highlightReset = false;
}

void KusanagiClipboardPanel::select(std::size_t index, bool scroll) {
  if (m_list == nullptr || index >= m_filtered.size()) return;
  if (index != m_selected) {
    const std::size_t old = m_selected;
    m_selected = index;
    m_list->notifyItemChanged(old);
    m_list->notifyItemChanged(index);
  }
  // The highlight keeps itself in view, so there's nothing extra to scroll.
  (void)scroll;
  syncHighlight(true);
  PanelManager::instance().requestLayout();
}

void KusanagiClipboardPanel::step(int delta) {
  const auto n = static_cast<long>(m_filtered.size());
  if (n == 0) return;
  select(static_cast<std::size_t>(((static_cast<long>(m_selected) + delta) % n + n) % n), true);
}

void KusanagiClipboardPanel::copy(std::size_t index) {
  if (index >= m_filtered.size()) return;
  const std::string id = m_entries[m_filtered[index]].id;
  (void)process::runAsync(std::vector<std::string>{"sh", "-c", "cliphist decode \"$1\" | wl-copy", "sh", id});
  PanelManager::instance().closePanel();
}

void KusanagiClipboardPanel::remove(std::size_t index) {
  if (index >= m_filtered.size()) return;
  const std::size_t at = m_filtered[index];
  const std::string id = m_entries[at].id;
  (void)process::runAsync(std::vector<std::string>{"sh", "-c", "printf '%s\\t\\n' \"$1\" | cliphist delete", "sh", id});
  m_entries.erase(m_entries.begin() + static_cast<std::ptrdiff_t>(at));
  // Removing an entry resets the selection to the top and scrolls the list up.
  applyFilter();
  m_previewId.clear();
  PanelManager::instance().requestLayout();
}

void KusanagiClipboardPanel::wipe() {
  (void)process::runAsync(std::vector<std::string>{"cliphist", "wipe"});
  m_entries.clear();
  m_previewId.clear();
  applyFilter();
  PanelManager::instance().requestLayout();
}

bool KusanagiClipboardPanel::handleGlobalKey(std::uint32_t sym, std::uint32_t /*modifiers*/, bool pressed, bool preedit) {
  if (!pressed || preedit) return false;
  switch (sym) {
  case XKB_KEY_Escape: PanelManager::instance().closePanel(); return true;
  case XKB_KEY_Down:
  case XKB_KEY_Tab: step(1); return true;
  case XKB_KEY_Up:
  case XKB_KEY_ISO_Left_Tab: step(-1); return true;
  case XKB_KEY_Return:
  case XKB_KEY_KP_Enter: copy(m_selected); return true;
  case XKB_KEY_Delete:
  case XKB_KEY_KP_Delete: remove(m_selected); return true;
  default: return false;
  }
}

void KusanagiClipboardPanel::startOpenAnimation() {
  m_closing = false;
  if (m_animations == nullptr || m_cardGroup == nullptr) {
    m_progress = 1.0F;
    return;
  }
  m_progress = 0.0F;
  m_animations->animate(
      0.0F, 1.0F, 380.0F, Easing::Linear,
      [this](float v) {
        m_progress = v;
        placeCard();
      },
      {}, m_cardGroup
  );
}

bool KusanagiClipboardPanel::beginCloseAnimation(std::function<void()> done) {
  if (m_animations == nullptr || m_cardGroup == nullptr) return false;
  m_animations->cancelForOwner(m_cardGroup);
  m_closing = true;
  const float from = m_progress;
  m_animations->animate(
      from, 0.0F, 160.0F * from, Easing::Linear,
      [this](float v) {
        m_progress = v;
        placeCard();
      },
      [done = std::move(done)]() { done(); }, m_cardGroup
  );
  return true;
}

void KusanagiClipboardPanel::placeCard() {
  if (m_cardGroup == nullptr || m_backdrop == nullptr) return;
  const float p = m_progress;
  const float s = contentScale();
  float opacity = 1.0F, scale = 1.0F, dy = 0.0F;
  if (m_closing) {
    // Fade over 140 ms, scale (InCubic) and slide (OutQuint) over 160 ms.
    const float t = 1.0F - p;
    opacity = 1.0F - std::clamp(t * 160.0F / 140.0F, 0.0F, 1.0F);
    scale = 1.0F + (0.95F - 1.0F) * t * t * t;
    dy = -14.0F * s * outQuint(t);
  } else {
    // Fade over 220 ms, scale (OutBack) and slide (OutQuint) over 380 ms.
    opacity = std::clamp(p * 380.0F / 220.0F, 0.0F, 1.0F);
    scale = 0.95F + (1.0F - 0.95F) * outBack(p, kusanagi::bounce(1.3F));
    dy = -14.0F * s * (1.0F - outQuint(p));
  }
  m_cardGroup->setPosition(m_cardX, m_cardY + std::round(dy));
  m_cardGroup->setSize(m_cardW, m_cardH);
  m_cardGroup->setTransformOrigin(m_cardW / 2, m_cardH / 2);
  m_cardGroup->setScale(scale);
  m_cardGroup->setOpacity(opacity);
  const float dim = static_cast<float>(kusanagi::opt<double>("look", "backdrop", 0.25));
  m_backdrop->setFill(ColorSpec{.role = std::nullopt, .fixed = rgba(0.0F, 0.0F, 0.0F, 1.0F), .alpha = dim * opacity});
  PanelManager::instance().requestRedraw();
}

void KusanagiClipboardPanel::doLayout(Renderer& renderer, float width, float height) {
  if (m_rootNode == nullptr || m_list == nullptr) return;
  const float s = contentScale();
  m_adapter->setScale(s);
  m_rootNode->setSize(width, height);
  m_backdrop->setPosition(0.0F, 0.0F);
  m_backdrop->setSize(width, height);
  m_backdropArea->setPosition(0.0F, 0.0F);
  m_backdropArea->setSize(width, height);

  const std::string font = kusanagi::font();
  const float radius = kusanagi::radius() * s;
  m_cardW = std::min(width, 820.0F * s);
  m_cardH = std::min(height, 500.0F * s);
  m_cardX = std::round((width - m_cardW) / 2);
  m_cardY = std::round(height * 0.22F);

  m_shadow->setVisible(kusanagi::shadows());
  m_shadow->setPosition(0.0F, 14.0F * s);
  m_shadow->setSize(m_cardW, m_cardH);
  m_shadow->setRadius(radius);
  m_shadow->setSoftness(40.0F * s);
  m_shadow->setFill(ColorSpec{.role = std::nullopt, .fixed = rgba(0.0F, 0.0F, 0.0F, 1.0F), .alpha = 0.5F});

  m_card->setPosition(0.0F, 0.0F);
  m_card->setSize(m_cardW, m_cardH);
  m_card->setRadius(radius);
  m_card->setFill(colorSpecFromRole(ColorRole::Surface, static_cast<float>(kusanagi::opt<double>("panel", "opacity", 0.95))));
  m_card->setBorder(kusanagi::surfaceBorder(), kusanagi::surfaceBorderWidth() * s);
  m_cardArea->setPosition(0.0F, 0.0F);
  m_cardArea->setSize(m_cardW, m_cardH);

  // Search field
  const float hc = kFieldH * s / 2;
  m_fieldIcon->setText(utf8(0xf0147));
  m_fieldIcon->setFontFamily(kIconFont);
  m_fieldIcon->setFontSize(20.0F * s);
  m_fieldIcon->setColor(colorSpecFromRole(ColorRole::Primary));
  m_fieldIcon->measure(renderer);
  m_fieldIcon->setPosition(22.0F * s, std::round(hc - m_fieldIcon->height() / 2));

  // "Clear all" chip
  const bool any = !m_entries.empty();
  m_wipeArea->setVisible(any);
  float chipX = m_cardW - 16.0F * s;
  if (any) {
    m_wipeIcon->setText(utf8(0xf0a7a));
    m_wipeIcon->setFontFamily(kIconFont);
    m_wipeIcon->setFontSize(13.0F * s);
    m_wipeIcon->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
    m_wipeIcon->measure(renderer);
    m_wipeLabel->setFontFamily(font);
    m_wipeLabel->setFontSize(11.0F * s);
    m_wipeLabel->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
    m_wipeLabel->measure(renderer);
    const float cw = std::round(m_wipeIcon->width() + 6.0F * s + m_wipeLabel->width() + 24.0F * s);
    const float ch = 28.0F * s;
    chipX = m_cardW - 16.0F * s - cw;
    m_wipeArea->setPosition(chipX, std::round(hc - ch / 2));
    m_wipeArea->setSize(cw, ch);
    m_wipeFace->setSize(cw, ch);
    m_wipeFace->setRadius(ch / 2);
    m_wipeFace->setFill(colorSpecFromRole(ColorRole::OnSurface, m_wipeHover ? 0.09F : 0.05F));
    m_wipeFace->setBorder(colorSpecFromRole(ColorRole::OnSurface, 0.06F), 1.0F * s);
    m_wipeIcon->setPosition(12.0F * s, std::round((ch - m_wipeIcon->height()) / 2));
    m_wipeLabel->setPosition(12.0F * s + m_wipeIcon->width() + 6.0F * s, std::round((ch - m_wipeLabel->height()) / 2));
  }

  const float ix = 56.0F * s;
  const float iw = std::max(0.0F, chipX - 12.0F * s - ix);
  m_input->setFontSize(16.0F * s);
  m_input->setControlHeight(36.0F * s);
  m_input->setSize(iw, 36.0F * s);
  m_input->setPosition(ix, std::round(hc - 18.0F * s));
  m_input->layout(renderer);
  m_placeholder->setVisible(m_input->value().empty());
  m_placeholder->setText(
      any ? "Search " + std::to_string(m_entries.size()) + " items    ↵ copy    del remove"
          : (m_loading ? "" : "Clipboard is empty")
  );
  m_placeholder->setFontFamily(font);
  m_placeholder->setFontSize(14.0F * s);
  m_placeholder->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
  m_placeholder->setMaxWidth(iw);
  m_placeholder->measure(renderer);
  m_placeholder->setPosition(ix, std::round(hc - m_placeholder->height() / 2));

  m_separator->setPosition(16.0F * s, kFieldH * s);
  m_separator->setSize(m_cardW - 32.0F * s, std::max(1.0F, std::round(1.0F * s)));
  m_separator->setFill(colorSpecFromRole(ColorRole::OnSurface, 0.07F));

  // List
  const float lx = 8.0F * s, ly = kFieldH * s + 9.0F * s;
  const float lw = std::min(kListW * s, m_cardW - 16.0F * s);
  m_listH = std::max(0.0F, m_cardH - ly - 8.0F * s);
  m_list->setPosition(lx, ly);
  m_list->setSize(lw, m_listH);
  m_list->layout(renderer);
  if (std::abs(lw - m_listW) > 0.01F) m_highlightReset = true;
  m_listW = lw;
  syncHighlight(false);

  // Preview
  const float px = lx + lw + 8.0F * s, py = kFieldH * s + 12.0F * s;
  layoutPreview(renderer, px, py, std::max(0.0F, m_cardW - px - 12.0F * s), std::max(0.0F, m_cardH - py - 12.0F * s));

  placeCard();
}

void KusanagiClipboardPanel::layoutPreview(Renderer& renderer, float x, float y, float w, float h) {
  const float s = contentScale();
  m_previewCard->setPosition(x, y);
  m_previewCard->setSize(w, h);
  m_previewCard->setRadius(std::max(6.0F, kusanagi::radius() - 6.0F) * s);
  m_previewCard->setFill(colorSpecFromRole(ColorRole::OnSurface, 0.045F));
  m_previewCard->setBorder(colorSpecFromRole(ColorRole::OnSurface, 0.06F), 1.0F * s);

  const Entry* e = m_selected < m_filtered.size() ? &m_entries[m_filtered[m_selected]] : nullptr;
  const bool text = e != nullptr && !e->image;
  m_previewScroll->setVisible(text);
  m_previewSwatch->setVisible(text && e->colour);
  m_previewImage->setVisible(e != nullptr && e->image);
  m_emptyIcon->setVisible(e == nullptr);
  m_emptyLabel->setVisible(e == nullptr);

  const std::string id = e != nullptr ? e->id : std::string();
  const bool changed = id != m_previewId;
  m_previewId = id;

  if (text) {
    const float m = 14.0F * s;
    const float tw = std::max(0.0F, w - 2 * m);
    m_previewText->setText(e->value);
    m_previewText->setFontFamily(kusanagi::font());
    m_previewText->setFontSize(12.0F * s);
    m_previewText->setColor(colorSpecFromRole(ColorRole::OnSurface));
    m_previewText->setMaxWidth(tw);
    m_previewScroll->setPosition(m, m);
    m_previewScroll->setSize(tw, std::max(0.0F, h - 2 * m));
    if (changed) m_previewScroll->setScrollOffset(0.0F);
    m_previewScroll->layout(renderer);
    if (e->colour) {
      const float sh = 120.0F * s;
      m_previewSwatch->setPosition(m, h - m - sh);
      m_previewSwatch->setSize(tw, sh);
      m_previewSwatch->setRadius(std::max(6.0F, kusanagi::radius() - 8.0F) * s);
      m_previewSwatch->setFill(fixedColorSpec(e->swatch));
      m_previewSwatch->setBorder(colorSpecFromRole(ColorRole::OnSurface, 0.15F), 1.0F * s);
    }
  }
  if (e != nullptr && e->image) {
    const float m = 12.0F * s;
    m_previewImage->setPosition(m, m);
    m_previewImage->setSize(std::max(0.0F, w - 2 * m), std::max(0.0F, h - 2 * m));
    if (changed && m_asyncTextures != nullptr) {
      (void)m_previewImage->setSourceFileAsync(renderer, *m_asyncTextures, e->value, static_cast<int>(std::lround(640.0F * s)));
    }
  } else if (changed && m_previewImage->hasImage()) {
    m_previewImage->clear(renderer);
  }
  if (e == nullptr) {
    m_emptyIcon->setText(utf8(0xf0147));
    m_emptyIcon->setFontFamily(kIconFont);
    m_emptyIcon->setFontSize(30.0F * s);
    m_emptyIcon->setColor(colorSpecFromRole(ColorRole::OnSurface, 0.2F));
    m_emptyIcon->measure(renderer);
    m_emptyLabel->setFontFamily(kusanagi::font());
    m_emptyLabel->setFontSize(11.0F * s);
    m_emptyLabel->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
    m_emptyLabel->measure(renderer);
    const float total = m_emptyIcon->height() + 8.0F * s + m_emptyLabel->height();
    const float top = std::round((h - total) / 2);
    m_emptyIcon->setPosition(std::round((w - m_emptyIcon->width()) / 2), top);
    m_emptyLabel->setPosition(std::round((w - m_emptyLabel->width()) / 2), top + m_emptyIcon->height() + 8.0F * s);
  }
}
