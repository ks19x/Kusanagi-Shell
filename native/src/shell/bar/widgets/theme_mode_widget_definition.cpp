#include "shell/bar/widgets/theme_mode_widget_definition.h"

const kusanagi::bar::WidgetDefinition<std::monostate>& themeModeWidgetDefinition() {
  static const kusanagi::bar::WidgetDefinition<std::monostate> definition{
      .type = "theme_mode",
  };
  return definition;
}
