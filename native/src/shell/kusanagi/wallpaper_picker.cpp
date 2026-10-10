#include "shell/kusanagi/wallpaper_picker.h"

#include "config/config_service.h"
#include "core/deferred_call.h"
#include "core/input/key_modifiers.h"
#include "core/log.h"
#include "core/process/process.h"
#include "cursor-shape-v1-client-protocol.h"
#include "render/core/renderer.h"
#include "render/scene/input_area.h"
#include "shell/kusanagi/kusanagi_style.h"
#include "shell/panel/panel_manager.h"
#include "shell/wallpaper/panel/wallpaper_scanner.h"
#include "ui/builders.h"
#include "ui/controls/box.h"
#include "ui/controls/image.h"
#include "ui/controls/input.h"
#include "ui/controls/label.h"
#include "ui/controls/scroll_view.h"
#include "ui/controls/virtual_grid_view.h"
#include "util/file_utils.h"
#include "wayland/wayland_connection.h"

#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>

namespace {

  constexpr Logger kLog("kusanagi-wallpaper");

  constexpr const char* kIconFont = "JetBrainsMono Nerd Font";
  constexpr float kCellW = 236.0F;
  constexpr float kCellH = kCellW * 9.0F / 16.0F + 34.0F;
  constexpr float kHeaderH = 60.0F;

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

  // OutBack easing with overshoot `s`.
  float outBack(float t, float s) {
    const float f = t - 1.0F;
    return 1.0F + (s + 1.0F) * f * f * f + s * f * f;
  }
  float outQuint(float t) { return 1.0F - std::pow(1.0F - t, 5.0F); }

  bool isPicture(const std::filesystem::path& p) {
    const std::string ext = p.extension().string();
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".webp" || ext == ".JPG" || ext == ".PNG";
  }

  std::string canonical(const std::string& path) {
    std::error_code ec;
    auto c = std::filesystem::weakly_canonical(path, ec);
    return ec ? path : c.string();
  }

  std::string expandHome(const std::string& path) {
    if (path.starts_with("~")) {
      const char* home = std::getenv("HOME");
      return std::string(home != nullptr ? home : "") + path.substr(1);
    }
    return path;
  }

  std::string readFirstLine(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::string line;
    if (in) std::getline(in, line);
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    return line;
  }

  // The `kusanagi` CLI from the checkout this binary was built in (<repo>/bin), else the one on PATH.
  std::string kusanagiCli() {
    std::error_code ec;
    const auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
      const auto cli = exe.parent_path().parent_path().parent_path() / "bin" / "kusanagi";
      if (std::filesystem::is_regular_file(cli, ec)) return cli.string();
    }
    return process::commandExists("kusanagi") ? std::string("kusanagi") : std::string();
  }

  // One grid cell: thumbnail frame and name.
  class PickerTile : public Node {
  public:
    explicit PickerTile(ThumbnailService* thumbnails) : m_thumbnails(thumbnails) {
      auto frame = ui::node({.out = &m_frame});
      m_frame->addChild(ui::box({.out = &m_frameBox}));
      m_frame->addChild(ui::image({.out = &m_thumb, .fit = ImageFit::Cover}));
      m_frame->addChild(ui::box({.out = &m_badge}));
      m_frame->addChild(ui::label({.out = &m_check, .maxLines = 1}));
      addChild(std::move(frame));
      addChild(ui::label({.out = &m_name, .maxLines = 1, .ellipsize = TextEllipsize::Middle}));
    }

    ~PickerTile() override { releaseThumb(); }

    void bind(Renderer& renderer, const KusanagiWallpaperPanel::Item& item, bool selected, bool current, bool hovered,
              float scale, AnimationManager* animations) {
      const bool sameItem = m_path == item.path;
      if (!sameItem) {
        releaseThumb();
        m_path = item.path;
        m_thumb->clear(renderer);
        m_thumb->setVisible(false);
      }
      m_s = scale;
      m_selected = selected;
      m_current = current;
      m_name->setText(item.display);

      const int target = static_cast<int>(std::lround((kCellW - 12.0F) * scale * std::max(1.0F, renderer.renderScale())));
      if (m_thumbnails != nullptr && target != m_target) {
        if (m_target > 0) m_thumbnails->release(m_path, m_target);
        (void)m_thumbnails->acquire(m_path, target);
        m_target = target;
      }
      refreshThumb(renderer);

      // The frame grows a little when hovered and to full size when selected.
      const float to = selected ? 1.0F : hovered ? 0.985F : 0.96F;
      if (!sameItem || animations == nullptr) {
        if (animations != nullptr) animations->cancel(m_anim);
        m_scaleNow = m_scaleTo = to;
      } else if (to != m_scaleTo) {
        animations->cancel(m_anim);
        const float from = m_scaleNow;
        m_scaleTo = to;
        const float overshoot = kusanagi::bounce(1.4F);
        m_anim = animations->animate(
            0.0F, 1.0F, 220.0F, Easing::Linear,
            [this, from, to, overshoot](float t) {
              m_scaleNow = from + (to - from) * outBack(t, overshoot);
              m_frame->setScale(m_scaleNow);
              PanelManager::instance().requestRedraw();
            },
            {}, this
        );
      }
      markLayoutDirty();
    }

    void refreshThumb(Renderer& renderer) {
      if (m_thumbnails == nullptr || m_path.empty() || m_target <= 0) return;
      const TextureHandle handle = m_thumbnails->peek(m_path, m_target);
      if (handle.id != 0) {
        m_thumb->setExternalTexture(renderer, handle);
        m_thumb->setVisible(true);
      }
    }

    void unbind(Renderer& renderer) {
      releaseThumb();
      m_thumb->clear(renderer);
      m_path.clear();
    }

    [[nodiscard]] bool bound() const noexcept { return !m_path.empty(); }

  protected:
    void doLayout(Renderer& renderer) override {
      const float s = m_s;
      const float fw = width() - 12.0F * s;
      const float fh = std::round(fw * 9.0F / 16.0F);
      const float r = std::max(6.0F, kusanagi::radius() - 6.0F) * s;
      m_frame->setPosition(6.0F * s, 6.0F * s);
      m_frame->setSize(fw, fh);
      m_frame->setTransformOrigin(fw / 2, fh / 2);
      m_frame->setScale(m_scaleNow);

      m_frameBox->setSize(fw, fh);
      m_frameBox->setRadius(r);
      m_frameBox->setFill(colorSpecFromRole(ColorRole::OnSurface, 0.06F));
      m_frameBox->setBorder(colorSpecFromRole(ColorRole::Primary), m_selected ? 2.0F * s : 0.0F);

      const float inset = m_selected ? 3.0F * s : 0.0F;
      m_thumb->setPosition(inset, inset);
      m_thumb->setSize(fw - 2 * inset, fh - 2 * inset);
      m_thumb->setRadius(std::max(4.0F * s, r - inset));

      // Badge for the current wallpaper
      const float bd = 22.0F * s;
      m_badge->setVisible(m_current);
      m_check->setVisible(m_current);
      m_badge->setPosition(fw - 8.0F * s - bd, 8.0F * s);
      m_badge->setSize(bd, bd);
      m_badge->setRadius(bd / 2);
      m_badge->setFill(colorSpecFromRole(ColorRole::Primary));
      if (m_current) {
        m_check->setText(utf8(0xf012c));
        m_check->setFontFamily(kIconFont);
        m_check->setFontSize(13.0F * s);
        m_check->setColor(colorSpecFromRole(ColorRole::Surface));
        m_check->measure(renderer);
        m_check->setPosition(
            fw - 8.0F * s - bd + std::round((bd - m_check->width()) / 2), 8.0F * s + std::round((bd - m_check->height()) / 2)
        );
      }

      m_name->setFontFamily(kusanagi::font());
      m_name->setFontSize(10.0F * s);
      m_name->setColor(colorSpecFromRole(m_selected ? ColorRole::OnSurface : ColorRole::OnSurfaceVariant));
      m_name->setMaxWidth(fw);
      m_name->measure(renderer);
      m_name->setPosition(6.0F * s + std::round((fw - m_name->width()) / 2), 6.0F * s + fh + 6.0F * s);
    }

  private:
    void releaseThumb() {
      if (m_thumbnails != nullptr && !m_path.empty() && m_target > 0) m_thumbnails->release(m_path, m_target);
      m_target = 0;
    }

    ThumbnailService* m_thumbnails = nullptr;
    Node* m_frame = nullptr;
    Box* m_frameBox = nullptr;
    Image* m_thumb = nullptr;
    Box* m_badge = nullptr;
    Label* m_check = nullptr;
    Label* m_name = nullptr;
    std::string m_path;
    int m_target = 0;
    float m_s = 1.0F;
    bool m_selected = false;
    bool m_current = false;
    float m_scaleNow = 0.96F;
    float m_scaleTo = 0.96F;
    AnimationManager::Id m_anim = 0;
  };

} // namespace

class KusanagiWallpaperPanel::Adapter : public VirtualGridAdapter {
public:
  explicit Adapter(KusanagiWallpaperPanel& panel) : m_panel(panel) {}

  void setRenderer(Renderer* renderer) { m_renderer = renderer; }
  void setScale(float s) { m_scale = s; }

  [[nodiscard]] std::size_t itemCount() const override { return m_panel.m_items.size(); }

  [[nodiscard]] std::unique_ptr<Node> createTile() override {
    auto tile = std::make_unique<PickerTile>(m_panel.m_thumbnails);
    m_tiles.push_back(tile.get());
    return tile;
  }

  void bindTile(Node& node, std::size_t index, bool selected, bool hovered) override {
    if (m_renderer == nullptr || index >= m_panel.m_items.size()) return;
    const auto& item = m_panel.m_items[index];
    static_cast<PickerTile&>(node).bind(
        *m_renderer, item, selected, item.path == m_panel.m_current, hovered, m_scale, m_panel.m_animations
    );
    // Hovering a cell selects it.
    if (hovered && !selected) {
      DeferredCall::callLater([panel = &m_panel, index]() { panel->select(index); });
    }
  }

  void onActivate(std::size_t index) override {
    DeferredCall::callLater([panel = &m_panel, index]() { panel->apply(index); });
  }

  void refreshThumbnails(Renderer& renderer) {
    for (auto* tile : m_tiles) {
      if (tile->bound()) tile->refreshThumb(renderer);
    }
  }

  void forgetTiles() { m_tiles.clear(); }

private:
  KusanagiWallpaperPanel& m_panel;
  Renderer* m_renderer = nullptr;
  float m_scale = 1.0F;
  std::vector<PickerTile*> m_tiles;
};

KusanagiWallpaperPanel::KusanagiWallpaperPanel(
    WaylandConnection* wayland, ConfigService* config, ThumbnailService* thumbnails, WallpaperScanner* scanner
)
    : m_wayland(wayland), m_config(config), m_thumbnails(thumbnails), m_scanner(scanner),
      m_adapter(std::make_unique<Adapter>(*this)) {
  if (m_scanner != nullptr) {
    m_scanner->setOnComplete([this]() {
      if (m_scanPending) {
        m_scanPending = false;
        loadScan();
        PanelManager::instance().requestLayout();
      }
    });
  }
}

KusanagiWallpaperPanel::~KusanagiWallpaperPanel() {
  if (m_scanner != nullptr) m_scanner->setOnComplete(nullptr);
}

InputArea* KusanagiWallpaperPanel::initialFocusArea() const {
  return m_input != nullptr ? m_input->inputArea() : nullptr;
}

void KusanagiWallpaperPanel::create() {
  m_adapter->forgetTiles();
  auto root = ui::node({.out = &m_rootNode});

  // The backdrop dims the screen, and a click on it closes the picker.
  m_rootNode->addChild(ui::box({.out = &m_backdrop}));
  m_rootNode->addChild(ui::inputArea({
      .out = &m_backdropArea,
      .onClick = [](const InputArea::PointerData&) { PanelManager::instance().closePanel(); },
  }));

  m_rootNode->addChild(ui::node({.out = &m_cardGroup}));
  m_cardGroup->addChild(ui::box({.out = &m_shadow}));
  m_cardGroup->addChild(ui::box({.out = &m_card}));
  m_card->setClipChildren(true);
  m_card->addChild(ui::inputArea({.out = &m_cardArea})); // swallows clicks on the card

  // Header: icon, title, filter and the random button
  m_card->addChild(ui::label({.out = &m_titleIcon, .maxLines = 1}));
  m_card->addChild(ui::label({.out = &m_title, .text = "Wallpapers", .fontWeight = FontWeight::Bold, .maxLines = 1}));
  m_card->addChild(ui::box({.out = &m_fieldPill}));
  m_card->addChild(ui::label({.out = &m_fieldIcon, .maxLines = 1}));
  m_card->addChild(ui::label({.out = &m_placeholder, .maxLines = 1}));
  m_card->addChild(ui::input({
      .out = &m_input,
      .fontSize = 12.0F,
      .controlHeight = 30.0F,
      .horizontalPadding = 0.0F,
      .clearButtonEnabled = false,
      .frameVisible = false,
      .onChange = [this](const std::string& text) {
        m_query = text;
        applyFilter();
        PanelManager::instance().requestLayout();
      },
  }));
  m_inputFocused = false;
  m_input->setOnFocusGain([this]() {
    m_inputFocused = true;
    PanelManager::instance().requestLayout();
  });
  m_input->setOnFocusLoss([this]() {
    m_inputFocused = false;
    PanelManager::instance().requestLayout();
  });
  auto random = ui::inputArea({
      .out = &m_randomArea,
      .cursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
      .onEnter = [this](const InputArea::PointerData&) {
        m_randomHover = true;
        PanelManager::instance().requestLayout();
      },
      .onLeave = [this]() {
        m_randomHover = false;
        PanelManager::instance().requestLayout();
      },
      .onClick = [this](const InputArea::PointerData&) { DeferredCall::callLater([this]() { applyRandom(); }); },
  });
  random->addChild(ui::box({.out = &m_randomFace}));
  random->addChild(ui::label({.out = &m_randomIcon, .maxLines = 1}));
  m_card->addChild(std::move(random));

  m_card->addChild(ui::virtualGridView({
      .out = &m_grid,
      .columns = m_columns,
      .cellHeight = kCellH,
      .squareCells = false,
      .columnGap = 0.0F,
      .rowGap = 0.0F,
      .overscanRows = 1,
      .itemCursorShape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,
      .scrollbarVisible = false,
      .adapter = m_adapter.get(),
      .onSelectionChanged =
          [this](std::optional<std::size_t> idx) {
            if (idx.has_value()) m_selected = *idx;
          },
      .configure =
          [](VirtualGridView& grid) {
            grid.scrollView().setViewportPaddingH(0.0F);
            grid.scrollView().setViewportPaddingV(0.0F);
          },
  }));
  m_card->addChild(ui::label({.out = &m_empty, .maxLines = 1, .visible = false}));

  setRoot(std::move(root));
  if (m_animations != nullptr) m_rootNode->setAnimationManager(m_animations);

  if (m_thumbnails != nullptr) {
    m_thumbSub = m_thumbnails->subscribePendingUpload([this]() {
      if (m_rootNode == nullptr) return;
      m_thumbRefresh = true;
      PanelManager::instance().requestUpdateOnly();
    });
  }
}

void KusanagiWallpaperPanel::onOpen(std::string_view /*context*/) {
  m_query.clear();
  m_selected = 0;
  m_randomHover = false;
  m_columns = static_cast<std::size_t>(std::clamp(kusanagi::opt<int>("wallpaper", "columns", 4), 1, 12));
  if (m_grid != nullptr) m_grid->setColumns(m_columns);
  m_current = canonical(readFirstLine(std::filesystem::path(FileUtils::configDir()) / "wallpaper"));
  scan();
  m_centerPending = true;
  startOpenAnimation();
}

void KusanagiWallpaperPanel::onClose() {
  // The scene, and with it every tile and its thumbnail, goes away right after this.
  m_thumbSub.disconnect();
  m_thumbRefresh = false;
  m_adapter->forgetTiles();
  m_adapter->setRenderer(nullptr);
  m_items.clear();
  m_items.shrink_to_fit();
  m_rootNode = nullptr;
  m_backdrop = nullptr;
  m_backdropArea = nullptr;
  m_cardGroup = nullptr;
  m_shadow = nullptr;
  m_card = nullptr;
  m_cardArea = nullptr;
  m_titleIcon = m_title = m_fieldIcon = m_placeholder = m_randomIcon = m_empty = nullptr;
  m_fieldPill = m_randomFace = nullptr;
  m_input = nullptr;
  m_randomArea = nullptr;
  m_grid = nullptr;
}

void KusanagiWallpaperPanel::scan() {
  const std::string folder = expandHome(kusanagi::opt<std::string>("wallpaper", "folder", "~/Pictures/Wallpapers"));
  m_folder = canonical(folder);
  if (m_scanner == nullptr) return;
  if (m_scanner->requestScan(m_folder, false)) {
    m_scanPending = false;
    loadScan();
  } else {
    m_scanPending = true;
    m_all.clear();
    applyFilter();
  }
}

void KusanagiWallpaperPanel::loadScan() {
  m_all.clear();
  if (const auto* result = m_scanner != nullptr ? m_scanner->cached(m_folder, false) : nullptr; result != nullptr) {
    for (const auto& e : result->entries) {
      if (e.isDir || !isPicture(e.absPath)) continue;
      const std::string name = e.absPath.filename().string();
      m_all.push_back(Item{
          .name = name,
          .display = e.absPath.stem().string(),
          .path = (std::filesystem::path(m_folder) / name).string(),
          .lower = lower(name),
      });
    }
  }
  std::ranges::sort(m_all, [](const Item& a, const Item& b) { return a.lower < b.lower; });
  applyFilter();
  m_centerPending = true;
}

void KusanagiWallpaperPanel::applyFilter() {
  std::string q = m_query;
  while (!q.empty() && std::isspace(static_cast<unsigned char>(q.back()))) q.pop_back();
  while (!q.empty() && std::isspace(static_cast<unsigned char>(q.front()))) q.erase(q.begin());
  q = lower(q);
  m_items.clear();
  for (const auto& it : m_all) {
    if (q.empty() || it.lower.find(q) != std::string::npos) m_items.push_back(it);
  }
  m_selected = 0;
  if (m_grid != nullptr) {
    m_grid->notifyDataChanged();
    m_grid->setSelectedIndex(m_items.empty() ? std::nullopt : std::optional<std::size_t>(0));
  }
}

void KusanagiWallpaperPanel::select(std::size_t index) {
  if (m_grid == nullptr || index >= m_items.size() || index == m_selected) return;
  m_selected = index;
  m_grid->setSelectedIndex(index);
  PanelManager::instance().requestLayout();
}

void KusanagiWallpaperPanel::move(int delta) {
  if (m_items.empty()) return;
  const long n = static_cast<long>(m_items.size());
  const long next = std::clamp(static_cast<long>(m_selected) + delta, 0L, n - 1);
  select(static_cast<std::size_t>(next));
}

void KusanagiWallpaperPanel::apply(std::size_t index) {
  if (index >= m_items.size()) return;
  const std::string path = m_items[index].path;
  m_current = path;
  setWallpaper(path);
  PanelManager::instance().closePanel();
}

void KusanagiWallpaperPanel::applyRandom() {
  if (m_items.empty()) return;
  static std::mt19937 rng{std::random_device{}()};
  apply(std::uniform_int_distribution<std::size_t>(0, m_items.size() - 1)(rng));
}

// `kusanagi wallpaper <file>` remembers the wallpaper, builds colours from it, runs the user's hook and
// tells the shell. The wallpaper is set right away so the transition doesn't wait for the palette;
// the CLI's wallpaper-set for the same path is then a no-op.
void KusanagiWallpaperPanel::setWallpaper(const std::string& path) {
  if (m_config != nullptr) {
    if (const WallpaperFavorite* favorite = m_config->wallpaperFavorite(path); favorite != nullptr) {
      std::vector<std::string> connectors;
      if (m_wayland != nullptr) {
        for (const auto& out : m_wayland->outputs()) {
          if (!out.connectorName.empty()) connectors.push_back(out.connectorName);
        }
      }
      m_config->applyWallpaperSelection(std::nullopt, path, favorite, connectors);
    } else {
      // Per-output paths win over the default, so set every output as well as the default.
      ConfigService::WallpaperBatch batch(*m_config);
      if (m_wayland != nullptr) {
        for (const auto& out : m_wayland->outputs()) {
          if (!out.connectorName.empty()) m_config->setWallpaperPath(out.connectorName, path);
        }
      }
      m_config->setWallpaperPath(std::nullopt, path);
    }
  }

  const std::string cli = kusanagiCli();
  if (!cli.empty()) {
    // KUSANAGI_ENGINE=native: the CLI talks back to this shell with `kusanagi-shell msg`.
    if (!process::runAsync(std::vector<std::string>{"env", "KUSANAGI_ENGINE=native", cli, "wallpaper", path})) {
      kLog.warn("couldn't run {} wallpaper", cli);
    }
    return;
  }
  // No CLI: at least remember it. The config reload follows ~/.config/kusanagi/wallpaper.
  const auto file = std::filesystem::path(FileUtils::configDir()) / "wallpaper";
  std::ofstream out(file);
  if (out) out << path << "\n";
}

bool KusanagiWallpaperPanel::handleGlobalKey(std::uint32_t sym, std::uint32_t modifiers, bool pressed, bool preedit) {
  if (!pressed || preedit) return false;
  const int cols = static_cast<int>(std::max<std::size_t>(1, m_columns));
  switch (sym) {
  case XKB_KEY_Escape: PanelManager::instance().closePanel(); return true;
  case XKB_KEY_Right: move(1); return true;
  case XKB_KEY_Left: move(-1); return true;
  case XKB_KEY_Down: move(cols); return true;
  case XKB_KEY_Up: move(-cols); return true;
  case XKB_KEY_Return:
  case XKB_KEY_KP_Enter: apply(m_selected); return true;
  case XKB_KEY_r:
  case XKB_KEY_R:
    if ((modifiers & KeyMod::Ctrl) != 0) {
      applyRandom();
      return true;
    }
    return false;
  default: return false;
  }
}

void KusanagiWallpaperPanel::startOpenAnimation() {
  m_closing = false;
  if (m_animations == nullptr || m_cardGroup == nullptr) {
    m_progress = 1.0F;
    return;
  }
  m_progress = 0.0F;
  m_animations->animate(
      0.0F, 1.0F, 400.0F, Easing::Linear,
      [this](float v) {
        m_progress = v;
        placeCard();
      },
      {}, m_cardGroup
  );
}

bool KusanagiWallpaperPanel::beginCloseAnimation(std::function<void()> done) {
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

// Places the card and backdrop for the animated open progress.
void KusanagiWallpaperPanel::placeCard() {
  if (m_cardGroup == nullptr || m_backdrop == nullptr) return;
  const float p = m_progress;
  float opacity = 1.0F;
  float scale = 1.0F;
  float dy = 0.0F;
  const float s = contentScale();
  if (m_closing) {
    // Close: fade over 140 ms, scale (InCubic) and slide (OutQuint) over 160 ms.
    const float t = 1.0F - p;
    opacity = 1.0F - std::clamp(t * 160.0F / 140.0F, 0.0F, 1.0F);
    scale = 1.0F + (0.96F - 1.0F) * t * t * t;
    dy = 16.0F * s * outQuint(t);
  } else {
    // Open: fade over 220 ms, scale (OutBack) and slide (OutQuint) over 400 ms.
    opacity = std::clamp(p * 400.0F / 220.0F, 0.0F, 1.0F);
    scale = 0.96F + (1.0F - 0.96F) * outBack(p, kusanagi::bounce(1.2F));
    dy = 16.0F * s * (1.0F - outQuint(p));
  }
  m_cardGroup->setPosition(m_cardX, m_cardY + std::round(dy));
  m_cardGroup->setSize(m_cardW, m_cardH);
  m_cardGroup->setTransformOrigin(m_cardW / 2, m_cardH / 2);
  m_cardGroup->setScale(scale);
  m_cardGroup->setOpacity(opacity);
  const float dim = std::min(0.7F, static_cast<float>(kusanagi::opt<double>("look", "backdrop", 0.25)) * 1.2F);
  m_backdrop->setFill(ColorSpec{.role = std::nullopt, .fixed = rgba(0.0F, 0.0F, 0.0F, 1.0F), .alpha = dim * opacity});
  PanelManager::instance().requestRedraw();
}

void KusanagiWallpaperPanel::doUpdate(Renderer& renderer) {
  if (m_thumbRefresh && m_thumbnails != nullptr) {
    m_thumbRefresh = false;
    if (m_thumbnails->uploadPending(renderer.textureManager())) {
      m_adapter->refreshThumbnails(renderer);
      PanelManager::instance().requestRedraw();
    }
  }
}

void KusanagiWallpaperPanel::doLayout(Renderer& renderer, float width, float height) {
  if (m_rootNode == nullptr || m_grid == nullptr) return;
  if (m_thumbnails != nullptr) {
    (void)m_thumbnails->uploadPending(renderer.textureManager());
    m_thumbRefresh = false;
  }
  const float s = contentScale();
  m_adapter->setRenderer(&renderer);
  m_adapter->setScale(s);
  m_surfaceW = width;
  m_surfaceH = height;
  m_rootNode->setSize(width, height);
  m_backdrop->setPosition(0.0F, 0.0F);
  m_backdrop->setSize(width, height);
  m_backdropArea->setPosition(0.0F, 0.0F);
  m_backdropArea->setSize(width, height);

  const std::string font = kusanagi::font();
  const float radius = kusanagi::radius() * s;
  m_cardW = std::min(width, static_cast<float>(m_columns) * kCellW * s + 32.0F * s);
  m_cardH = std::max(160.0F * s, std::min(height - 160.0F * s, 640.0F * s));
  m_cardX = std::round((width - m_cardW) / 2);
  m_cardY = std::round((height - m_cardH) / 2);

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

  // Header
  const float hc = kHeaderH * s / 2; // vertical centre of the header
  m_titleIcon->setText(utf8(0xf0e09));
  m_titleIcon->setFontFamily(kIconFont);
  m_titleIcon->setFontSize(20.0F * s);
  m_titleIcon->setColor(colorSpecFromRole(ColorRole::Primary));
  m_titleIcon->measure(renderer);
  m_titleIcon->setPosition(22.0F * s, std::round(hc - m_titleIcon->height() / 2));
  m_title->setFontFamily(font);
  m_title->setFontSize(15.0F * s);
  m_title->setColor(colorSpecFromRole(ColorRole::OnSurface));
  m_title->measure(renderer);
  m_title->setPosition(52.0F * s, std::round(hc - m_title->height() / 2));

  const float bd = 34.0F * s;
  const float bx = m_cardW - 16.0F * s - bd;
  const float by = std::round(hc - bd / 2);
  m_randomArea->setPosition(bx, by);
  m_randomArea->setSize(bd, bd);
  m_randomFace->setSize(bd, bd);
  m_randomFace->setRadius(bd / 2);
  m_randomFace->setFill(colorSpecFromRole(ColorRole::OnSurface, m_randomHover ? 0.1F : 0.0F));
  m_randomIcon->setText(utf8(0xf0450));
  m_randomIcon->setFontFamily(kIconFont);
  m_randomIcon->setFontSize(16.0F * s);
  m_randomIcon->setColor(colorSpecFromRole(m_randomHover ? ColorRole::OnSurface : ColorRole::OnSurfaceVariant));
  m_randomIcon->measure(renderer);
  m_randomIcon->setPosition(std::round((bd - m_randomIcon->width()) / 2), std::round((bd - m_randomIcon->height()) / 2));

  const float fw = 260.0F * s, fh = 34.0F * s;
  const float fx = bx - 10.0F * s - fw;
  const float fy = std::round(hc - fh / 2);
  m_fieldPill->setPosition(fx, fy);
  m_fieldPill->setSize(fw, fh);
  m_fieldPill->setRadius(fh / 2);
  m_fieldPill->setFill(colorSpecFromRole(ColorRole::OnSurface, 0.06F));
  m_fieldPill->setBorder(
      m_inputFocused ? colorSpecFromRole(ColorRole::Primary, 0.6F) : colorSpecFromRole(ColorRole::OnSurface, 0.08F), 1.0F * s
  );
  m_fieldIcon->setText(utf8(0xf0349));
  m_fieldIcon->setFontFamily(kIconFont);
  m_fieldIcon->setFontSize(14.0F * s);
  m_fieldIcon->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
  m_fieldIcon->measure(renderer);
  m_fieldIcon->setPosition(fx + 12.0F * s, std::round(hc - m_fieldIcon->height() / 2));
  const float ix = fx + 34.0F * s, iw = fw - 46.0F * s;
  m_input->setFontSize(12.0F * s);
  m_input->setControlHeight(fh - 4.0F * s);
  m_input->setSize(iw, fh - 4.0F * s);
  m_input->setPosition(ix, fy + 2.0F * s);
  m_input->layout(renderer);
  m_placeholder->setVisible(m_input->value().empty());
  m_placeholder->setText("Filter " + std::to_string(m_items.size()) + " wallpapers");
  m_placeholder->setFontFamily(font);
  m_placeholder->setFontSize(12.0F * s);
  m_placeholder->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
  m_placeholder->setMaxWidth(iw);
  m_placeholder->measure(renderer);
  m_placeholder->setPosition(ix, std::round(hc - m_placeholder->height() / 2));

  // Grid
  const float gx = 16.0F * s, gy = kHeaderH * s + 4.0F * s;
  const float gw = m_cardW - 32.0F * s, gh = std::max(0.0F, m_cardH - gy - 16.0F * s);
  m_grid->setColumns(m_columns);
  m_grid->setCellHeight(kCellH * s);
  m_grid->setScale(s);
  m_grid->setPosition(gx, gy);
  m_grid->setSize(gw, gh);
  m_grid->layout(renderer);

  // Open with the current wallpaper selected and centred.
  if (m_centerPending && !m_scanPending && !m_all.empty()) {
    m_centerPending = false;
    if (m_query.empty()) {
      const auto it = std::ranges::find_if(m_items, [this](const Item& i) { return i.path == m_current; });
      if (it != m_items.end()) {
        const auto index = static_cast<std::size_t>(it - m_items.begin());
        m_selected = index;
        m_grid->setSelectedIndex(index);
        const float rowTop = static_cast<float>(index / m_columns) * kCellH * s;
        m_grid->scrollView().setScrollOffset(std::max(0.0F, rowTop - (gh - kCellH * s) / 2));
        m_grid->layout(renderer);
      }
    }
  }

  m_empty->setVisible(m_items.empty() && !m_scanPending);
  if (m_items.empty()) {
    m_empty->setText(m_all.empty() ? "No wallpapers in " + kusanagi::opt<std::string>("wallpaper", "folder", "~/Pictures/Wallpapers")
                                   : "Nothing matches");
    m_empty->setFontFamily(font);
    m_empty->setFontSize(12.0F * s);
    m_empty->setColor(colorSpecFromRole(ColorRole::OnSurfaceVariant));
    m_empty->measure(renderer);
    m_empty->setPosition(std::round((m_cardW - m_empty->width()) / 2), std::round(gy + (gh - m_empty->height()) / 2));
  }

  placeCard();
}
