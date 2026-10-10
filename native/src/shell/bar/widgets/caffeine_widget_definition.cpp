#include "shell/bar/widgets/caffeine_widget_definition.h"

const kusanagi::bar::WidgetDefinition<std::monostate>& caffeineWidgetDefinition() {
  static const kusanagi::bar::WidgetDefinition<std::monostate> definition{
      .type = "caffeine",
  };
  return definition;
}
