// WdgText.qml — your own words on the desktop. text, size (px), bold, align (left center right).
import QtQuick

Item {
    id: t
    property var w
    readonly property var o: w ? w.spec : ({})
    implicitWidth: Math.min(label.implicitWidth, 900)
    implicitHeight: label.implicitHeight
    width: implicitWidth
    height: implicitHeight
    Text {
        id: label
        width: Math.min(implicitWidth, 900)
        text: t.o.text || "Stay hungry, stay foolish."
        wrapMode: Text.Wrap
        horizontalAlignment: t.o.align === "left" ? Text.AlignLeft : t.o.align === "right" ? Text.AlignRight : Text.AlignHCenter
        font.family: t.o.font || Theme.fontFamily
        font.pixelSize: t.o.size || 28
        font.bold: !!t.o.bold
        font.italic: !!t.o.italic
        color: t.o.color ? BarSpec.color(t.o.color) : Theme.text
    }
}
