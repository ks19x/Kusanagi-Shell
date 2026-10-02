// CpText.qml — text for the control panel: theme font, greyscale + whole-pixel hinting (crisp, no colour fringes)
import QtQuick

Text {
    color: Theme.text
    font.family: Theme.fontFamily
    font.pixelSize: 12
    font.hintingPreference: Font.PreferFullHinting
    renderType: Text.NativeRendering
    verticalAlignment: Text.AlignVCenter
}
