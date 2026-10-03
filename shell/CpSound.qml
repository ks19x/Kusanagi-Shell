// CpSound.qml — control panel Sound tab: switch output / input device in one click, their
// volumes, and a slider per app that's playing (Spotify, Vesktop, the game…).
import Quickshell
import Quickshell.Services.Pipewire
import QtQuick

Column {
    id: root
    required property var panel
    spacing: 12

    readonly property var nodes: Pipewire.nodes.values.filter(n => n.audio)
    readonly property var outputs: nodes.filter(n => n.isSink && !n.isStream)
    readonly property var inputs: nodes.filter(n => !n.isSink && !n.isStream)
    readonly property var apps: nodes.filter(n => n.isStream && n.properties["media.class"] === "Stream/Output/Audio")
    PwObjectTracker { objects: root.nodes }

    function label(n) { return n.nickname || n.description || n.name }
    // a rough device icon from its name: headphones / headset / HDMI·DisplayPort / speakers
    function deviceIcon(n, input) {
        const s = (label(n) + " " + (n.name || "")).toLowerCase()
        if (input) return s.includes("headset") || s.includes("usb") ? 0xf02cb : 0xf036c
        if (s.includes("headphone") || s.includes("headset") || s.includes("bluez")) return 0xf02cb
        if (s.includes("hdmi") || s.includes("displayport")) return 0xf0379
        return 0xf04c3
    }

    // one device pick-list: a row per device, the default one highlighted; its volume underneath
    component DeviceList: CpCard {
        id: list
        property string title
        property var devices: []
        property var current: null
        property bool input: false
        signal pick(var node)
        width: parent.width
        height: col.implicitHeight + 24

        Column {
            id: col
            x: 12; y: 12
            width: parent.width - 24
            spacing: 4

            CpText { text: list.title; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1; color: Theme.textDim; bottomPadding: 4 }

            Repeater {
                model: list.devices
                Rectangle {
                    id: row
                    required property var modelData
                    readonly property bool isDefault: modelData === list.current
                    width: col.width
                    height: 34
                    radius: Math.max(6, Config.look.radius - 8)
                    color: isDefault ? Theme.alpha(Theme.accent, 0.16) : Theme.alpha(Theme.text, rowArea.containsMouse ? 0.06 : 0)
                    Behavior on color { ColorAnimation { duration: Config.ms(160) } }

                    CpIcon {
                        id: ic
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        cp: root.deviceIcon(row.modelData, list.input)
                        font.pixelSize: 16
                        color: row.isDefault ? Theme.accent : Theme.textDim
                    }
                    CpText {
                        anchors { left: ic.right; leftMargin: 10; right: check.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
                        text: root.label(row.modelData)
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        font.bold: row.isDefault
                    }
                    CpIcon {
                        id: check
                        anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                        cp: 0xf012c
                        font.pixelSize: 14
                        color: Theme.accent
                        opacity: row.isDefault ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: Config.ms(160) } }
                    }
                    MouseArea {
                        id: rowArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: if (!row.isDefault) list.pick(row.modelData)
                    }
                }
            }
            CpText { visible: list.devices.length === 0; text: "No devices"; color: Theme.textDim; font.pixelSize: 12 }

            Item { width: 1; height: 4; visible: !!list.current }
            CpSlider {
                visible: !!list.current && !!list.current.audio
                width: col.width
                icon: list.current && list.current.audio && list.current.audio.muted ? (list.input ? 0xf036d : 0xf0581) : (list.input ? 0xf036c : 0xf057e)
                label: list.input ? "Input volume" : "Output volume"
                value: list.current && list.current.audio ? list.current.audio.volume : 0
                muted: !!list.current && !!list.current.audio && list.current.audio.muted
                onMoved: v => list.current.audio.volume = v
                onIconClicked: list.current.audio.muted = !list.current.audio.muted
            }
        }
    }

    DeviceList {
        title: "OUTPUT"
        devices: root.outputs
        current: Pipewire.defaultAudioSink
        onPick: n => Pipewire.preferredDefaultAudioSink = n
    }

    // per-app volume
    CpCard {
        width: parent.width
        height: appCol.implicitHeight + 24
        Column {
            id: appCol
            x: 12; y: 12
            width: parent.width - 24
            spacing: 8
            CpText { text: "APPS"; font.pixelSize: 10; font.bold: true; font.letterSpacing: 1; color: Theme.textDim }
            Repeater {
                model: root.apps
                CpSlider {
                    required property var modelData
                    width: appCol.width
                    icon: modelData.audio.muted ? 0xf0581 : 0xf075a
                    label: modelData.properties["application.name"] || root.label(modelData)
                    value: modelData.audio.volume
                    muted: modelData.audio.muted
                    onMoved: v => modelData.audio.volume = v
                    onIconClicked: modelData.audio.muted = !modelData.audio.muted
                }
            }
            CpText { visible: root.apps.length === 0; text: "Nothing is playing"; color: Theme.textDim; font.pixelSize: 12 }
        }
    }

    DeviceList {
        title: "INPUT"
        input: true
        devices: root.inputs
        current: Pipewire.defaultAudioSource
        onPick: n => Pipewire.preferredDefaultAudioSource = n
    }

    Row {
        spacing: 8
        CpChip { label: "Sound settings"; icon: 0xf0493; onClicked: { root.panel.close(); root.panel.shellRef.openSettings("sound") } }
        CpChip { label: "Mixer"; icon: 0xf066a; onClicked: root.panel.runClosed(["pavucontrol"]) }
    }
}
