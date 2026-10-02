// CpGauge.qml — 270° ring gauge: big value in the middle, label + detail under it.
// The arc eases to new values; `warnAt`/`hotAt` tint it for temperatures.
import QtQuick
import QtQuick.Shapes

Item {
    id: root

    property real value: 0              // 0..100
    property string text: Math.round(value) + "%"
    property string label: ""
    property string detail: ""
    property real warnAt: 101
    property real hotAt: 101
    property int size: 92

    implicitWidth: size
    implicitHeight: size + 34

    property real shown: value
    Behavior on shown { NumberAnimation { duration: Config.ms(700); easing.type: Easing.OutCubic } }

    readonly property color tint: value >= hotAt ? Theme.danger : value >= warnAt ? "#e8be62" : Theme.accent

    Shape {
        width: root.size; height: root.size
        preferredRendererType: Shape.CurveRenderer
        anchors.horizontalCenter: parent.horizontalCenter

        ShapePath {
            fillColor: "transparent"
            strokeColor: Theme.alpha(Theme.text, 0.08)
            strokeWidth: 7
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.size / 2; centerY: root.size / 2
                radiusX: root.size / 2 - 5; radiusY: root.size / 2 - 5
                startAngle: 135; sweepAngle: 270
            }
        }
        ShapePath {
            fillColor: "transparent"
            strokeColor: root.tint
            strokeWidth: 7
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: root.size / 2; centerY: root.size / 2
                radiusX: root.size / 2 - 5; radiusY: root.size / 2 - 5
                startAngle: 135; sweepAngle: Math.max(0.5, 270 * Math.min(100, root.shown) / 100)
            }
        }
    }

    CpText {
        width: root.size
        anchors.horizontalCenter: parent.horizontalCenter
        y: 0; height: root.size
        horizontalAlignment: Text.AlignHCenter
        text: root.text
        font.pixelSize: 19
        font.bold: true
    }

    Column {
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: root.size - 6 }
        spacing: 1
        CpText {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.label
            font.pixelSize: 11
            font.bold: true
            font.letterSpacing: 1
        }
        CpText {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.detail
            font.pixelSize: 10
            color: Theme.textDim
        }
    }
}
