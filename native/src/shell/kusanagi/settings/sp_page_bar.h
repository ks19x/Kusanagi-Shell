#pragma once

// Pieces of Settings > Bar. The classic options edit settings.json "bar" and show while "bars" is empty;
// the layout editor (sp_page_bar_editor.cpp) edits "bars".

#include <nlohmann/json.hpp>

namespace kusanagi::sp {

  class Column;

  void buildBarClassic(Column& col);
  void buildBarEditor(Column& col);
  // The classic bar written out as a custom layout (an array with one bar spec).
  [[nodiscard]] nlohmann::json legacyBars();
  // Turns whole-number reals into integers, recursively, so 24.0 is written as 24.
  [[nodiscard]] nlohmann::json deepNormalized(const nlohmann::json& v);

} // namespace kusanagi::sp
