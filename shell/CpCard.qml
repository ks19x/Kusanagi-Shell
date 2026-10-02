// CpCard.qml — the soft inset surface every control-panel section sits on
import QtQuick

Rectangle {
    radius: Math.max(6, Config.look.radius - 6)
    color: Theme.alpha(Theme.text, 0.045)
    border.width: 1
    border.color: Theme.alpha(Theme.text, 0.06)
}
