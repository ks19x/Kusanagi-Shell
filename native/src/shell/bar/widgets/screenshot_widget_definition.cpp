#include "shell/bar/widgets/screenshot_widget_definition.h"

#include "shell/bar/widgets/glyph_button_definition.h"

const kusanagi::bar::WidgetDefinition<ScreenshotWidget::Options>& screenshotWidgetDefinition() {
  using Options = ScreenshotWidget::Options;

  static const kusanagi::bar::WidgetDefinition<Options> definition{
      .type = "screenshot",
      .fields = kusanagi::bar::glyphButtonFields<Options>(),
  };
  return definition;
}
