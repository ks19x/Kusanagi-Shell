#include "shell/bar/widgets/test_widget_definition.h"

const kusanagi::bar::WidgetDefinition<std::monostate>& testWidgetDefinition() {
  static const kusanagi::bar::WidgetDefinition<std::monostate> definition{
      .type = "test",
  };
  return definition;
}
