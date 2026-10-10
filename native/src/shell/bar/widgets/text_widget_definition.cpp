#include "shell/bar/widgets/text_widget_definition.h"

const kusanagi::bar::WidgetDefinition<TextWidget::Options>& textWidgetDefinition() {
  using kusanagi::bar::field;
  using Options = TextWidget::Options;

  static const kusanagi::bar::WidgetDefinition<Options> definition{
      .type = "text",
      .fields = {
          field<&Options::text>({
              .key = "text",
          }),
      },
  };
  return definition;
}
