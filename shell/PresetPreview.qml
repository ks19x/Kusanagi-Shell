// PresetPreview.qml — a tiny drawing of a preset's bar (style, height, roundness, workspaces, layout)
import QtQuick

Item {
    id: root
    required property var preset
    readonly property var b: preset.bar || {}
    readonly property var w: preset.workspaces || {}
    readonly property real k: 0.62                       // scale of the mini bar
    readonly property int h: Math.round((b.height || 28) * k)
    readonly property bool atBottom: b.position === "bottom"
    readonly property bool centered: b.layout === "centered"
    readonly property real r: (b.radius ?? 10) * k
    readonly property color barBg: Theme.alpha(Theme.bgPanel, Math.max(0.35, b.opacity ?? 0.5))

    clip: true

    // a slice of "desktop"
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.alpha(Theme.accent, 0.22) }
            GradientStop { position: 1; color: Theme.alpha(Theme.accent2, 0.08) }
        }
    }

    Item {
        id: strip
        width: parent.width
        height: root.h
        y: root.atBottom ? parent.height - height : 0

        // solid / floating backgrounds
        Rectangle {
            visible: root.b.style === "solid" || root.b.style === "floating"
            readonly property bool fl: root.b.style === "floating"
            x: fl ? 4 : 0; y: fl ? 2 : 0
            width: parent.width - 2 * x; height: parent.height - 2 * y
            radius: fl ? root.r : 0
            color: root.barBg
        }

        component Isle: Rectangle {
            height: root.h - 5
            y: 2.5
            radius: root.r
            color: root.b.style === "islands" ? root.barBg : "transparent"
            border.width: root.b.outline ? 1 : 0
            border.color: Theme.alpha(Theme.text, 0.15)
        }

        // workspaces
        Isle {
            id: wsIsle
            readonly property bool flush: root.w.style === "dwl" && !root.centered
            x: flush ? 0 : root.centered ? (parent.width - width) / 2 : 4
            y: flush ? 0 : 2.5
            height: flush ? root.h : root.h - 5
            radius: flush ? 0 : root.r
            width: wsRow.width + (flush ? 0 : 10)
            Row {
                id: wsRow
                anchors.centerIn: parent
                spacing: root.w.style === "dwl" ? 0 : 3
                Repeater {
                    model: 5
                    Item {
                        required property int index
                        readonly property bool act: index === 1
                        readonly property bool occ: index === 0 || index === 2
                        readonly property string st: root.w.style || "pills"
                        width: st === "dwl" ? root.h : st === "pills" && act ? 15 : ["numbers", "roman", "kanji", "custom"].includes(st) ? 9 : 6
                        height: st === "dwl" ? wsIsle.height : 9
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        Rectangle {
                            visible: ["pills", "dots"].includes(parent.st)
                            anchors.centerIn: parent
                            width: parent.width; height: parent.st === "pills" && parent.act ? 8 : 6
                            radius: height / 2
                            color: parent.act ? Theme.accent : parent.occ ? Theme.alpha(Theme.text, 0.85) : "transparent"
                            border.width: parent.act || parent.occ ? 0 : 1
                            border.color: Theme.alpha(Theme.text, 0.35)
                        }
                        Rectangle {
                            visible: parent.st === "dwl"
                            anchors.fill: parent
                            color: parent.act ? Theme.accent : "transparent"
                        }
                        Text {
                            visible: !["pills", "dots"].includes(parent.st)
                            anchors.centerIn: parent
                            text: parent.st === "roman" ? ["I", "II", "III", "IV", "V"][parent.index] : parent.st === "kanji" ? ["一", "二", "三", "四", "五"][parent.index] : parent.index + 1
                            font.pixelSize: 7
                            font.family: Theme.fontFamily
                            color: parent.st === "dwl" && parent.act ? Theme.bgPanel : parent.act ? Theme.accent : Theme.alpha(Theme.text, parent.occ ? 0.85 : 0.4)
                        }
                    }
                }
            }
        }

        // window title (dwl / text-only looks)
        Text {
            visible: !!(root.b.modules && root.b.modules.title) && !root.centered
            x: wsIsle.x + wsIsle.width + 6
            anchors.verticalCenter: parent.verticalCenter
            text: "foot — ~/kusanagi"
            font.pixelSize: 7; font.family: Theme.fontFamily
            color: Theme.alpha(Theme.text, 0.8)
        }

        // clock
        Isle {
            x: root.centered ? 4 : (parent.width - width) / 2
            width: clk.implicitWidth + 12
            Text { id: clk; anchors.centerIn: parent; text: "20:31"; font.pixelSize: 7; font.bold: root.b.clockBold !== false; font.family: Theme.fontFamily; color: Theme.text }
        }

        // status
        Isle {
            x: parent.width - width - 4
            width: stat.implicitWidth + 12
            Text {
                id: stat
                anchors.centerIn: parent
                textFormat: Text.StyledText
                text: root.b.accentLabels ? `<font color="${Theme.accent}">CPU</font> 4%  <font color="${Theme.accent}">RAM</font> 23%` : "CPU 4%  RAM 23%"
                font.pixelSize: 7; font.family: Theme.fontFamily
                color: Theme.text
            }
        }
    }
}
