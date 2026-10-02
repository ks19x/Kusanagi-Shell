// CpIcon.qml — one Nerd Font glyph by codepoint (cp: 0xf0493), so no private-use characters live in source
import QtQuick

Text {
    property int cp: 0
    text: cp ? String.fromCodePoint(cp) : ""
    color: Theme.text
    font.family: Theme.iconFont
    font.pixelSize: 16
    renderType: Text.NativeRendering
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
}
