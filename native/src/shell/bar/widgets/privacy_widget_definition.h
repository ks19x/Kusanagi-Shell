#pragma once

#include "shell/bar/widget_definition.h"
#include "shell/bar/widgets/privacy_widget.h"

[[nodiscard]] const kusanagi::bar::WidgetDefinition<PrivacyWidget::Options>& privacyWidgetDefinition();
