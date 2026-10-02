// LockScreen.qml — what one monitor shows while locked: blurred wallpaper, big clock, you, password pill.
// Fades/zooms in on lock and dissolves outward on unlock. State lives in Lock.qml.
import Quickshell
import Quickshell.Widgets
import QtQuick
import QtQuick.Effects

Item {
    id: root
    required property var lock

    property bool shown: false
    Component.onCompleted: { shown = true; input.forceActiveFocus() }
    readonly property bool visibleState: shown && !lock.unlocking

    // ---------- background ----------
    Image {
        id: wall
        anchors.fill: parent
        source: root.lock.wallpaper ? "file://" + root.lock.wallpaper : ""
        fillMode: Image.PreserveAspectCrop
        sourceSize: Qt.size(1280, 720)       // it gets blurred anyway — decode small
        asynchronous: true
        visible: false
    }
    MultiEffect {
        anchors.fill: parent
        source: wall
        blurEnabled: Config.lock.blur > 0
        blur: Config.lock.blur
        blurMax: 64
        brightness: -Config.lock.dim
        saturation: 0.1
        scale: root.visibleState ? 1.0 : 1.08
        opacity: root.visibleState ? 1 : 0
        Behavior on scale { NumberAnimation { duration: Config.ms(700); easing.type: Easing.OutQuint } }
        Behavior on opacity { NumberAnimation { duration: Config.ms(420); easing.type: Easing.OutCubic } }
    }

    // ---------- content ----------
    Item {
        id: content
        anchors.fill: parent
        opacity: root.visibleState ? 1 : 0
        scale: root.visibleState ? 1 : (root.lock.unlocking ? 1.06 : 0.96)
        Behavior on opacity { NumberAnimation { duration: Config.ms(380); easing.type: Easing.OutCubic } }
        Behavior on scale { NumberAnimation { duration: Config.ms(520); easing.type: Easing.OutQuint } }

        SystemClock { id: clock; precision: Config.lock.clock.includes("ss") ? SystemClock.Seconds : SystemClock.Minutes }

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height * 0.18
            spacing: 4

            CpText {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Qt.formatDateTime(clock.date, Config.lock.clock)
                font.pixelSize: 128
                font.bold: true
                font.letterSpacing: -2
            }
            CpText {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Qt.formatDate(clock.date, "dddd, d MMMM")
                font.pixelSize: 20
                color: Theme.alpha(Theme.text, 0.75)
            }
        }

        // ---- you + password ----
        Column {
            id: login
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height * 0.58
            spacing: 16

            ClippingRectangle {
                visible: Config.lock.avatar
                anchors.horizontalCenter: parent.horizontalCenter
                width: 84; height: 84; radius: 42
                color: Theme.alpha(Theme.text, 0.1)
                border.width: 2
                border.color: Theme.alpha(Theme.accent, 0.8)
                Image {
                    anchors.fill: parent
                    source: "file://" + Quickshell.env("HOME") + "/.face"
                    fillMode: Image.PreserveAspectCrop
                    sourceSize: Qt.size(168, 168)
                    asynchronous: true
                }
            }

            CpText {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Config.lock.greeting || Quickshell.env("USER")
                font.pixelSize: 16
                font.bold: true
            }

            Rectangle {
                id: pill
                anchors.horizontalCenter: parent.horizontalCenter
                width: 320; height: 48
                radius: 24
                color: Theme.alpha(Theme.bgPanel, 0.6)
                border.width: 1
                border.color: root.lock.error ? Theme.danger : input.activeFocus ? Theme.alpha(Theme.accent, 0.7) : Theme.alpha(Theme.text, 0.12)
                Behavior on border.color { ColorAnimation { duration: Config.ms(200) } }

                // wrong password → shake
                property real shake: 0
                transform: Translate { x: pill.shake }
                SequentialAnimation {
                    id: shakeAnim
                    NumberAnimation { target: pill; property: "shake"; to: -12; duration: 50 }
                    NumberAnimation { target: pill; property: "shake"; to: 10; duration: 70 }
                    NumberAnimation { target: pill; property: "shake"; to: -6; duration: 70 }
                    NumberAnimation { target: pill; property: "shake"; to: 3; duration: 60 }
                    NumberAnimation { target: pill; property: "shake"; to: 0; duration: 60 }
                }
                Connections { target: root.lock; function onFailed() { shakeAnim.restart() } }

                CpIcon {
                    x: 18
                    anchors.verticalCenter: parent.verticalCenter
                    cp: root.lock.busy ? 0xf0450 : 0xf033e
                    font.pixelSize: 16
                    color: Theme.textDim
                    RotationAnimation on rotation { running: root.lock.busy; from: 0; to: 360; duration: 900; loops: Animation.Infinite }
                }

                // one dot per character, popping in
                Row {
                    anchors.centerIn: parent
                    spacing: 7
                    Repeater {
                        model: Math.min(root.lock.password.length, 24)
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: Theme.text
                            scale: 0
                            Component.onCompleted: scale = 1
                            Behavior on scale { NumberAnimation { duration: Config.ms(180); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(3) } }
                        }
                    }
                }
                CpText {
                    anchors.centerIn: parent
                    visible: root.lock.password.length === 0
                    text: root.lock.busy ? "Checking…" : "Password"
                    font.pixelSize: 13
                    color: Theme.textDim
                }

                TextInput {
                    id: input
                    anchors.fill: parent
                    opacity: 0                       // the dots are the visible part
                    echoMode: TextInput.Password
                    focus: true
                    text: root.lock.password
                    onTextChanged: if (text !== root.lock.password) root.lock.password = text
                    onAccepted: root.lock.submit()
                    Keys.onEscapePressed: {
                        if (root.lock.testMode) root.lock.release()
                        else root.lock.password = ""
                    }
                }
                MouseArea { anchors.fill: parent; onClicked: input.forceActiveFocus() }
            }

            CpText {
                anchors.horizontalCenter: parent.horizontalCenter
                height: 16
                text: root.lock.error
                font.pixelSize: 12
                color: Theme.danger
            }
        }

        // ---- now playing ----
        Rectangle {
            visible: Config.lock.media && root.lock.player !== null
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 48 }
            width: Math.min(420, mediaRow.implicitWidth + 32)
            height: 52
            radius: 26
            color: Theme.alpha(Theme.bgPanel, 0.55)
            border.width: 1
            border.color: Theme.alpha(Theme.text, 0.08)
            Row {
                id: mediaRow
                anchors.centerIn: parent
                spacing: 12
                CpIcon { anchors.verticalCenter: parent.verticalCenter; cp: 0xf075a; font.pixelSize: 16; color: Theme.accent }
                CpText {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, 260)
                    elide: Text.ElideRight
                    text: root.lock.player ? `${root.lock.player.trackTitle}${root.lock.player.trackArtist ? "  ·  " + root.lock.player.trackArtist : ""}` : ""
                    font.pixelSize: 12
                }
                CpIconButton { width: 28; height: 28; icon: 0xf04ae; onClicked: root.lock.player.previous() }
                CpIconButton { width: 28; height: 28; icon: root.lock.player && root.lock.player.isPlaying ? 0xf03e4 : 0xf040a; filled: true; onClicked: root.lock.player.togglePlaying() }
                CpIconButton { width: 28; height: 28; icon: 0xf04ad; onClicked: root.lock.player.next() }
            }
        }

        // ---- test-mode banner ----
        Rectangle {
            visible: root.lock.testMode
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 24 }
            width: banner.implicitWidth + 32; height: 34; radius: 17
            color: Theme.alpha("#e8be62", 0.9)
            CpText {
                id: banner
                anchors.centerIn: parent
                text: "TEST MODE — Esc unlocks, and it unlocks by itself after 30 s"
                font.pixelSize: 12
                font.bold: true
                color: "#1a1408"
            }
        }
    }
}
