#include "shell/bar/widgets/nightlight_widget_definition.h"

const kusanagi::bar::WidgetDefinition<std::monostate>& nightlightWidgetDefinition() {
  static const kusanagi::bar::WidgetDefinition<std::monostate> definition{
      .type = "nightlight",
  };
  return definition;
}
