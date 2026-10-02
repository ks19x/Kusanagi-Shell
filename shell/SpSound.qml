// SpSound.qml — outputs, inputs (pick the default + volume) and per-app volume
import Quickshell
import Quickshell.Services.Pipewire
import QtQuick

Column {
    id: page
    spacing: 22

    readonly property var nodes: Pipewire.nodes.values.filter(n => n.audio)
    readonly property var outputs: nodes.filter(n => n.isSink && !n.isStream)
    readonly property var inputs: nodes.filter(n => !n.isSink && !n.isStream)
    readonly property var apps: nodes.filter(n => n.isStream && n.properties["media.class"] === "Stream/Output/Audio")
    PwObjectTracker { objects: page.nodes }

    function label(n) { return n.nickname || n.description || n.name }

    // a device row: radio (default) + name, then its volume slider
    component Device: Column {
        id: dev
        required property var node
        property bool isDefault: false
        signal pick()
        width: parent.width
        spacing: 6

        Item {
            width: parent.width; height: 24
            Rectangle {
                id: radio
                width: 16; height: 16; radius: 8
                anchors.verticalCenter: parent.verticalCenter
                color: "transparent"
                border.width: 2
                border.color: dev.isDefault ? Theme.accent : Theme.alpha(Theme.text, 0.3)
                Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: Theme.accent; visible: dev.isDefault }
            }
            CpText {
                anchors { left: radio.right; leftMargin: 10; verticalCenter: parent.verticalCenter }
                text: page.label(dev.node)
                font.pixelSize: 12
                font.bold: dev.isDefault
            }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: dev.pick() }
        }
        CpSlider {
            width: parent.width; height: 30
            icon: dev.node.audio && dev.node.audio.muted ? 0xf0581 : 0xf057e
            value: dev.node.audio ? dev.node.audio.volume : 0
            muted: dev.node.audio && dev.node.audio.muted
            onMoved: v => dev.node.audio.volume = v
            onIconClicked: dev.node.audio.muted = !dev.node.audio.muted
        }
    }

    SpGroup {
        title: "Output"
        Repeater {
            model: page.outputs
            Device {
                required property var modelData
                node: modelData
                isDefault: Pipewire.defaultAudioSink === modelData
                onPick: Pipewire.preferredDefaultAudioSink = modelData
            }
        }
        CpText { visible: page.outputs.length === 0; text: "No output devices"; color: Theme.textDim; font.pixelSize: 12 }
    }

    SpGroup {
        title: "Input"
        Repeater {
            model: page.inputs
            Device {
                required property var modelData
                node: modelData
                isDefault: Pipewire.defaultAudioSource === modelData
                onPick: Pipewire.preferredDefaultAudioSource = modelData
            }
        }
        CpText { visible: page.inputs.length === 0; text: "No input devices"; color: Theme.textDim; font.pixelSize: 12 }
    }

    SpGroup {
        title: "Apps"
        hint: "Everything that's playing sound right now."
        Repeater {
            model: page.apps
            CpSlider {
                required property var modelData
                width: parent.width
                icon: modelData.audio.muted ? 0xf0581 : 0xf075a
                label: modelData.properties["application.name"] || page.label(modelData)
                value: modelData.audio.volume
                muted: modelData.audio.muted
                onMoved: v => modelData.audio.volume = v
                onIconClicked: modelData.audio.muted = !modelData.audio.muted
            }
        }
        CpText { visible: page.apps.length === 0; text: "Nothing is playing"; color: Theme.textDim; font.pixelSize: 12 }
    }

    CpChip { label: "Open the advanced mixer"; icon: 0xf066a; onClicked: Quickshell.execDetached(["pavucontrol"]) }
}
