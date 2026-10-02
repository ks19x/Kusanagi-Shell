// CpSegmented.qml — one-of-N picker with a sliding highlight (tabs, bar style, speed…).
// options: [{ label, value, icon? }], current: the selected value.
import QtQuick

Rectangle {
    id: root

    property var options: []
    property var current
    property int fontSize: 11
    signal picked(var value)

    implicitHeight: 32
    radius: height / 2
    color: Theme.alpha(Theme.text, 0.06)

    readonly property int index: Math.max(0, options.findIndex(o => o.value === current))
    readonly property real cell: (width - 6) / Math.max(1, options.length)

    Rectangle {
        x: 3 + root.index * root.cell
        y: 3
        width: root.cell
        height: root.height - 6
        radius: height / 2
        color: Theme.accent
        Behavior on x { NumberAnimation { duration: Config.ms(320); easing.type: Easing.OutQuint } }
    }

    Row {
        x: 3; y: 3
        Repeater {
            model: root.options
            Item {
                id: seg
                required property var modelData
                required property int index
                readonly property bool active: index === root.index
                width: root.cell
                height: root.height - 6

                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    CpIcon {
                        visible: !!seg.modelData.icon
                        cp: seg.modelData.icon || 0
                        font.pixelSize: 14
                        color: seg.active ? Theme.bgPanel : (segArea.containsMouse ? Theme.text : Theme.textDim)
                        Behavior on color { ColorAnimation { duration: Config.ms(200) } }
                    }
                    CpText {
                        text: seg.modelData.label
                        font.pixelSize: root.fontSize
                        font.bold: seg.active
                        color: seg.active ? Theme.bgPanel : (segArea.containsMouse ? Theme.text : Theme.textDim)
                        Behavior on color { ColorAnimation { duration: Config.ms(200) } }
                    }
                }

                MouseArea {
                    id: segArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.picked(seg.modelData.value)
                }
            }
        }
    }
}
