#include "shell/bar/widgets/notification_widget_definition.h"

const kusanagi::bar::WidgetDefinition<NotificationWidget::Options>& notificationWidgetDefinition() {
  using kusanagi::bar::field;
  using Options = NotificationWidget::Options;

  static const kusanagi::bar::WidgetDefinition<Options> definition{
      .type = "notifications",
      .fields = {
          field<&Options::hideWhenNoUnread>({
              .key = "hide_when_no_unread",
          }),
      },
  };
  return definition;
}
