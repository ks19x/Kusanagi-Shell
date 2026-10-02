// Clipboard.qml — clipboard history (Super+V), same spotlight look as the launcher.
// List on the left, full preview (text or image) of the selection on the right.
// Type to search, ↑/↓ to move, Enter copies, Delete removes, Esc closes. Loaded only while open.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Widgets
import QtQuick
import QtQuick.Effects

PanelWindow {
    id: root

    property bool showing: false
    function open() { showing = true }
    function close() { showing = false }
    function toggle() { showing = !showing }

    anchors { top: true; left: true; right: true; bottom: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "quickshell-clipboard"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: showing ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    visible: showing || card.opacity > 0.01
    mask: Region { item: root.showing ? backdrop : null }

    onShowingChanged: if (showing) {
        search.text = ""
        list.currentIndex = 0
        lister.running = true
        search.forceActiveFocus()
    }

    property var entries: []          // [{ id, kind: "txt" | "img", value }]
    readonly property var filtered: {
        const q = search.text.toLowerCase()
        return q === "" ? entries : entries.filter(e => e.kind === "txt" && e.value.toLowerCase().includes(q))
    }
    readonly property var selected: filtered[list.currentIndex] ?? null

    function isColour(v) { return /^#([0-9a-f]{3}|[0-9a-f]{6}|[0-9a-f]{8})$/i.test(v.trim()) }

    function copy(e) {
        if (!e) return
        Quickshell.execDetached(["sh", "-c", "cliphist decode \"$1\" | wl-copy", "sh", e.id])
        close()
    }
    function remove(e) {
        if (!e) return
        Quickshell.execDetached(["sh", "-c", "printf '%s\\t\\n' \"$1\" | cliphist delete", "sh", e.id])
        entries = entries.filter(x => x.id !== e.id)
    }
    function wipe() {
        Quickshell.execDetached(["cliphist", "wipe"])
        entries = []
    }

    Process {
        id: lister
        command: [Quickshell.shellDir + "/scripts/cliplist.sh"]
        stdout: StdioCollector {
            onStreamFinished: {
                const out = []
                for (const line of text.split("\n")) {
                    const p = line.split("\t")
                    if (p.length >= 3) out.push({ id: p[0], kind: p[1], value: p.slice(2).join("\t") })
                }
                root.entries = out
            }
        }
    }

    // ---------- view ----------
    Rectangle {
        id: backdrop
        anchors.fill: parent
        color: Theme.alpha("#000000", Config.look.backdrop * card.opacity)
        MouseArea { anchors.fill: parent; onClicked: root.close() }
    }

    RectangularShadow {

        visible: Config.look.shadows
        anchors.fill: card
        opacity: card.opacity
        scale: card.scale
        offset.y: 14
        blur: 40
        radius: card.radius
        color: Theme.alpha("#000000", 0.5)
    }

    Rectangle {
        id: card
        width: 820
        height: 500
        x: Math.round((root.width - width) / 2)
        y: Math.round(root.height * 0.22) + (root.showing ? 0 : -14)
        radius: Config.look.radius
        color: Theme.alpha(Theme.bgPanel, Config.panel.opacity)
        border.width: Theme.surfaceBorderWidth
        border.color: Theme.surfaceBorder
        clip: true

        opacity: root.showing ? 1 : 0
        scale: root.showing ? 1 : 0.95
        Behavior on opacity { NumberAnimation { duration: Config.ms(root.showing ? 220 : 140) } }
        Behavior on scale { NumberAnimation { duration: Config.ms(root.showing ? 380 : 160); easing.type: root.showing ? Easing.OutBack : Easing.InCubic; easing.overshoot: Config.bounce(1.3) } }
        Behavior on y { NumberAnimation { duration: Config.ms(root.showing ? 380 : 160); easing.type: Easing.OutQuint } }

        MouseArea { anchors.fill: parent }

        // ---- search ----
        Item {
            id: field
            width: parent.width
            height: 60

            CpIcon { x: 22; anchors.verticalCenter: parent.verticalCenter; cp: 0xf0147; font.pixelSize: 20; color: Theme.accent }
            TextInput {
                id: search
                anchors { left: parent.left; leftMargin: 56; right: wipeBtn.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 16
                clip: true
                onTextChanged: list.currentIndex = 0
                Keys.onPressed: e => {
                    const n = root.filtered.length
                    if (e.key === Qt.Key_Escape) root.close()
                    else if ((e.key === Qt.Key_Down || e.key === Qt.Key_Tab) && n) list.currentIndex = (list.currentIndex + 1) % n
                    else if ((e.key === Qt.Key_Up || e.key === Qt.Key_Backtab) && n) list.currentIndex = (list.currentIndex - 1 + n) % n
                    else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) root.copy(root.selected)
                    else if (e.key === Qt.Key_Delete) root.remove(root.selected)
                    else return
                    e.accepted = true
                }
            }
            CpText {
                anchors { left: search.left; verticalCenter: parent.verticalCenter }
                visible: !search.text
                text: root.entries.length ? `Search ${root.entries.length} items    ↵ copy    del remove` : "Clipboard is empty"
                font.pixelSize: 14
                color: Theme.textDim
            }
            CpChip {
                id: wipeBtn
                anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                visible: root.entries.length > 0
                label: "Clear all"
                icon: 0xf0a7a
                onClicked: root.wipe()
            }
        }

        Rectangle {
            anchors { top: field.bottom; left: parent.left; right: parent.right; leftMargin: 16; rightMargin: 16 }
            height: 1
            color: Theme.alpha(Theme.text, 0.07)
        }

        // ---- list ----
        ListView {
            id: list
            anchors { top: field.bottom; left: parent.left; bottom: parent.bottom; margins: 8; topMargin: 9 }
            width: 400
            clip: true
            model: root.filtered
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: Config.ms(160)
            highlightMoveVelocity: -1
            highlightResizeDuration: Config.ms(160)
            highlight: Rectangle {
                radius: Math.max(6, Config.look.radius - 6)
                color: Theme.alpha(Theme.accent, 0.16)
                border.width: 1
                border.color: Theme.alpha(Theme.accent, 0.35)
            }

            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool isImg: modelData.kind === "img"
                width: list.width
                height: isImg ? 76 : 44

                Rectangle {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !row.isImg && root.isColour(row.modelData.value)
                    width: 16; height: 16; radius: 4
                    color: visible ? row.modelData.value.trim() : "transparent"
                    border.width: 1
                    border.color: Theme.alpha(Theme.text, 0.2)
                }
                CpIcon {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !row.isImg && !root.isColour(row.modelData.value)
                    cp: /^https?:\/\//.test(row.modelData.value) ? 0xf0337 : 0xf0284
                    font.pixelSize: 15
                    color: row.ListView.isCurrentItem ? Theme.accent : Theme.textDim
                }
                CpText {
                    visible: !row.isImg
                    anchors { left: parent.left; leftMargin: 38; right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    text: row.modelData.value.replace(/\s+/g, " ").trim()
                    font.pixelSize: 12
                    elide: Text.ElideRight
                    color: row.ListView.isCurrentItem ? Theme.text : Theme.alpha(Theme.text, 0.8)
                }

                ClippingRectangle {
                    visible: row.isImg
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 104; height: 60
                    radius: 6
                    color: Theme.alpha(Theme.text, 0.06)
                    Image {
                        anchors.fill: parent
                        source: row.isImg ? "file://" + row.modelData.value : ""
                        sourceSize: Qt.size(208, 120)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }
                CpText {
                    visible: row.isImg
                    anchors { left: parent.left; leftMargin: 128; verticalCenter: parent.verticalCenter }
                    text: "Image"
                    font.pixelSize: 12
                    color: Theme.textDim
                }

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    onEntered: list.currentIndex = row.index
                    onClicked: e => e.button === Qt.MiddleButton ? root.remove(row.modelData) : root.copy(row.modelData)
                }
            }
        }

        // ---- preview of the selection ----
        CpCard {
            id: preview
            anchors { top: field.bottom; topMargin: 12; left: list.right; leftMargin: 8; right: parent.right; rightMargin: 12; bottom: parent.bottom; bottomMargin: 12 }
            clip: true

            Flickable {
                anchors { fill: parent; margins: 14 }
                visible: root.selected !== null && root.selected.kind === "txt"
                contentHeight: full.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                CpText {
                    id: full
                    width: parent.width
                    text: root.selected && root.selected.kind === "txt" ? root.selected.value : ""
                    wrapMode: Text.WrapAnywhere
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignTop
                }
            }

            // colour codes get a swatch
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 14 }
                height: 120
                radius: Math.max(6, Config.look.radius - 8)
                visible: root.selected !== null && root.selected.kind === "txt" && root.isColour(root.selected.value)
                color: visible ? root.selected.value.trim() : "transparent"
                border.width: 1
                border.color: Theme.alpha(Theme.text, 0.15)
            }

            Image {
                anchors { fill: parent; margins: 12 }
                visible: root.selected !== null && root.selected.kind === "img"
                source: visible ? "file://" + root.selected.value : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                sourceSize: Qt.size(640, 640)
            }

            Column {
                anchors.centerIn: parent
                visible: root.selected === null
                spacing: 8
                CpIcon { anchors.horizontalCenter: parent.horizontalCenter; cp: 0xf0147; font.pixelSize: 30; color: Theme.alpha(Theme.text, 0.2) }
                CpText { anchors.horizontalCenter: parent.horizontalCenter; text: "Nothing selected"; font.pixelSize: 11; color: Theme.textDim }
            }
        }
    }
}
