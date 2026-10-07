// BmLauncher.qml — app launcher button: a glyph (format) or, with "logo": true, the Kusanagi logo.
// Provider for BarModule (see its header).
import QtQuick

Item {
    id: p
    property var m
    readonly property bool logo: !!m && m.eff.logo === true
    readonly property bool visual: logo
    readonly property var vars: ({})
    readonly property string format: logo ? "" : String.fromCodePoint(0xf003b)
    readonly property var actions: ({ click: "launcher", rightClick: "settings" })
    readonly property string tooltip: "Apps (right-click: Settings)"
    readonly property real s: m ? Math.round(BarSpec.fontSize(m.eff.fontSize, m.win.spec.fontSize) * 1.5) : 18
    implicitWidth: logo ? s : 0
    implicitHeight: logo ? s : 0
    width: implicitWidth
    height: implicitHeight
    Image {
        visible: p.logo
        anchors.fill: parent
        source: p.logo ? Qt.resolvedUrl("logo.svg") : ""
        sourceSize: Qt.size(p.s * 2, p.s * 2)
        smooth: true
        mipmap: true
        scale: p.m && p.m.hovered ? 1.12 : 1
        Behavior on scale { NumberAnimation { duration: Config.ms(160); easing.type: Easing.OutBack } }
    }
}
