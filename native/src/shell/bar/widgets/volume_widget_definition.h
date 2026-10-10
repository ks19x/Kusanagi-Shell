#pragma once

#include "shell/bar/widget_definition.h"
#include "shell/bar/widgets/volume_widget.h"

[[nodiscard]] const kusanagi::bar::WidgetDefinition<VolumeWidget::Options>& volumeWidgetDefinition();
