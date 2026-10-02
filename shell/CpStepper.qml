// CpStepper.qml — −  value  +  for small integers
import QtQuick

Row {
    id: root
    property int value: 0
    property int from: 0
    property int to: 10
    property int step: 1
    property string suffix: ""
    signal changed(int value)

    spacing: 6
    CpIconButton { width: 28; height: 28; icon: 0xf0374; onClicked: root.changed(Math.max(root.from, root.value - root.step)) }
    CpText {
        width: Math.max(34, implicitWidth); height: 28
        horizontalAlignment: Text.AlignHCenter
        text: root.value + root.suffix
        font.pixelSize: 13
        font.bold: true
    }
    CpIconButton { width: 28; height: 28; icon: 0xf0415; onClicked: root.changed(Math.min(root.to, root.value + root.step)) }
}
