pragma Singleton
// Polkit.qml — Kusanagi's polkit agent: when an app needs admin rights (GParted, mounting a disk,
// NetworkManager, pkexec …) it asks for your password here, in the shell's style. Needs the polkit
// daemon (polkitd), which every desktop distro runs. If another agent already holds the session
// (polkit-gnome, lxpolkit, hyprpolkitagent …) this one quietly stays out and tries again later.
// Config.polkit.enabled turns it off. Costs nothing until a request comes: the window is created then.
import Quickshell
import Quickshell.Wayland
import Quickshell.Services.Polkit
import QtQuick

Singleton {
    id: root
    function start() {}                    // shell.qml calls it so the agent exists from login on

    // re-created every couple of minutes while another agent holds the session
    property bool attempt: true
    readonly property var agent: loader.item
    readonly property bool registered: !!agent && agent.isRegistered

    LazyLoader {
        id: loader
        active: Config.polkit.enabled && root.attempt
        PolkitAgent {}
    }
    Timer {
        interval: 120000
        repeat: true
        running: Config.polkit.enabled && !!root.agent && !root.agent.isRegistered && !root.agent.isActive
        onTriggered: { root.attempt = false; Qt.callLater(() => root.attempt = true) }
    }

    readonly property var flow: agent && agent.isActive ? agent.flow : null

    LazyLoader {
        active: !!root.flow
        PanelWindow {
            id: win
            readonly property var flow: root.flow
            anchors { top: true; bottom: true; left: true; right: true }
            color: "transparent"
            exclusionMode: ExclusionMode.Ignore
            WlrLayershell.layer: WlrLayer.Overlay
            WlrLayershell.namespace: "kusanagi-polkit"
            WlrLayershell.keyboardFocus: WlrKeyboardFocus.Exclusive

            property bool shown: false
            Component.onCompleted: { shown = true; field.forceActiveFocus() }
            function cancel() { if (flow) flow.cancelAuthenticationRequest() }
            function submit() {
                if (!flow || !flow.isResponseRequired) return
                flow.submit(field.text)
                field.text = ""
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.alpha("#000000", 0.45 * (win.shown ? 1 : 0))
                Behavior on color { ColorAnimation { duration: Config.ms(200) } }
                MouseArea { anchors.fill: parent }
            }

            Rectangle {
                id: card
                anchors.centerIn: parent
                width: 420
                height: body.implicitHeight + 48
                radius: Config.look.radius
                color: Theme.alpha(Theme.bgPanel, Math.max(0.92, Config.panel.opacity))
                border.width: Theme.surfaceBorderWidth
                border.color: Theme.surfaceBorder
                opacity: win.shown ? 1 : 0
                scale: win.shown ? 1 : 0.94
                Behavior on opacity { NumberAnimation { duration: Config.ms(200) } }
                Behavior on scale { NumberAnimation { duration: Config.ms(340); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(1.2) } }

                // a wrong password shakes the card
                SequentialAnimation {
                    id: shake
                    NumberAnimation { target: card; property: "anchors.horizontalCenterOffset"; to: -10; duration: 50 }
                    NumberAnimation { target: card; property: "anchors.horizontalCenterOffset"; to: 10; duration: 70 }
                    NumberAnimation { target: card; property: "anchors.horizontalCenterOffset"; to: -6; duration: 60 }
                    NumberAnimation { target: card; property: "anchors.horizontalCenterOffset"; to: 0; duration: 50 }
                }
                Connections {
                    target: win.flow
                    function onAuthenticationFailed() { shake.restart(); field.forceActiveFocus() }
                }

                Column {
                    id: body
                    x: 24; y: 24
                    width: parent.width - 48
                    spacing: 14

                    Row {
                        spacing: 14
                        Rectangle {
                            width: 44; height: 44; radius: 14
                            color: Theme.alpha(Theme.accent, 0.16)
                            CpIcon { anchors.centerIn: parent; cp: 0xf0483; font.pixelSize: 22; color: Theme.accent }
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            CpText { text: "Authentication required"; font.pixelSize: 15; font.bold: true }
                            CpText { text: win.flow ? win.flow.actionId : ""; font.pixelSize: 10; color: Theme.textDim; width: body.width - 58; elide: Text.ElideMiddle }
                        }
                    }

                    CpText {
                        width: parent.width
                        text: win.flow ? win.flow.message : ""
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        color: Theme.alpha(Theme.text, 0.85)
                    }

                    // who authenticates (several admins: click to switch)
                    Rectangle {
                        width: parent.width; height: 36; radius: 10
                        color: Theme.alpha(Theme.text, 0.05)
                        readonly property var ids: win.flow ? win.flow.identities : []
                        CpIcon { x: 12; anchors.verticalCenter: parent.verticalCenter; cp: 0xf0004; font.pixelSize: 15; color: Theme.textDim }
                        CpText {
                            x: 36; anchors.verticalCenter: parent.verticalCenter
                            text: win.flow && win.flow.selectedIdentity ? win.flow.selectedIdentity.displayName || win.flow.selectedIdentity.name || "you" : "you"
                            font.pixelSize: 12
                        }
                        CpText {
                            visible: parent.ids.length > 1
                            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                            text: "switch ⇄"; font.pixelSize: 11; color: Theme.accent
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: parent.ids.length > 1
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const ids = parent.ids, i = ids.indexOf(win.flow.selectedIdentity)
                                win.flow.selectedIdentity = ids[(i + 1) % ids.length]
                            }
                        }
                    }

                    // the password
                    Rectangle {
                        width: parent.width; height: 42; radius: 12
                        color: Theme.alpha(Theme.text, 0.06)
                        border.width: 1
                        border.color: field.activeFocus ? Theme.alpha(Theme.accent, 0.7) : Theme.alpha(Theme.text, 0.1)
                        TextInput {
                            id: field
                            anchors { left: parent.left; right: parent.right; leftMargin: 14; rightMargin: 14; verticalCenter: parent.verticalCenter }
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            echoMode: win.flow && win.flow.responseVisible ? TextInput.Normal : TextInput.Password
                            passwordCharacter: "•"
                            enabled: !!win.flow && win.flow.isResponseRequired
                            clip: true
                            onAccepted: win.submit()
                            Keys.onEscapePressed: win.cancel()
                        }
                        CpText {
                            anchors { left: field.left; verticalCenter: parent.verticalCenter }
                            visible: !field.text
                            text: win.flow && win.flow.isResponseRequired ? (win.flow.inputPrompt || "Password").replace(/:\s*$/, "") : "Checking…"
                            font.pixelSize: 13
                            color: Theme.textDim
                        }
                    }

                    CpText {
                        visible: text !== ""
                        width: parent.width
                        text: win.flow ? win.flow.supplementaryMessage : ""
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        color: win.flow && win.flow.supplementaryIsError ? Theme.danger : Theme.textDim
                    }

                    Row {
                        anchors.right: parent.right
                        spacing: 8
                        CpChip { label: "Cancel"; onClicked: win.cancel() }
                        CpChip { label: "Authenticate"; icon: 0xf0483; on: true; onClicked: win.submit() }
                    }
                }
            }
        }
    }
}
