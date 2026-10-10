#include "shell/bar/widgets/custom_button_widget_definition.h"

#include "shell/bar/widgets/glyph_button_definition.h"

const kusanagi::bar::WidgetDefinition<CustomButtonWidget::Options>& customButtonWidgetDefinition() {
  using kusanagi::bar::field;
  using Options = CustomButtonWidget::Options;

  static const kusanagi::bar::WidgetDefinition<Options> definition{
      .type = "custom_button",
      .fields = kusanagi::bar::glyphButtonFields<Options>(
          field<&Options::label>({
              .key = "label",
          }),
          field<&Options::tooltip>({
              .key = "tooltip",
          })
      ),
      .glyph = [](const Options& options) { return options.glyph; },
  };
  return definition;
}
