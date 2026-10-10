#pragma once

#include "shell/bar/widget_definition.h"
#include "shell/bar/widgets/clipboard_widget.h"

[[nodiscard]] const kusanagi::bar::WidgetDefinition<ClipboardWidget::Options>& clipboardWidgetDefinition();
