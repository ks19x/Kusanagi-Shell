#pragma once

// The Settings pages in sidebar order, with the function that builds each one (sp_page_<id>.cpp).
// See docs/native-settings.md for adding a page.

#include <vector>

namespace kusanagi::sp {

  class Column;

  // Builds a page's body into `page`, a Column at most 760 px wide.
  using PageBuilder = void (*)(Column& page);

  struct PageInfo {
    const char* id;
    const char* group; // Group heading on the first page of a group, else "".
    const char* name;
    char32_t icon;
    const char* desc;
    const char* keys; // Extra search words.
    PageBuilder build; // If null, the page shows a placeholder.
  };

  [[nodiscard]] const std::vector<PageInfo>& pages();

  void buildPlaceholder(Column& page, const PageInfo& info);

  void buildAppearance(Column& page);
  void buildWorkspaces(Column& page);
  void buildNotifications(Column& page);
  void buildAbout(Column& page);
  void buildPanel(Column& page);
  void buildLauncher(Column& page);
  void buildGameMode(Column& page);
  void buildPresets(Column& page);
  void buildWallpaper(Column& page);
  void buildLock(Column& page);
  void buildLogin(Column& page);
  void buildRecording(Column& page);
  void buildSound(Column& page);
  void buildBluetooth(Column& page);
  void buildDisplay(Column& page);
  void buildNetwork(Column& page);
  void buildStorage(Column& page);
  void buildUpdates(Column& page);
  void buildBar(Column& page);

} // namespace kusanagi::sp
