// WallpaperPicker.qml — thumbnail grid of Config.wallpaper.folder (Super+A).
// Type to filter, arrows to move, Enter / click to apply (zei/wallpaper: awww transition + re-theme
// everything from it), R for a random one, Esc to close. Loaded only while open.
import Quickshell
import Quickshell.Io
import Quickshell.Wayland
import Quickshell.Widgets
import QtQuick
import QtQuick.Effects
import Qt.labs.folderlistmodel

PanelWindow {
    id: root

    property bool showing: false
    function open() { showing = true }
    function close() { showing = false }
    function toggle() { showing = !showing }

    anchors { top: true; left: true; right: true; bottom: true }
    color: "transparent"
    exclusionMode: ExclusionMode.Ignore
    WlrLayershell.namespace: "quickshell-wallpaper"
    WlrLayershell.layer: WlrLayer.Overlay
    WlrLayershell.keyboardFocus: showing ? WlrKeyboardFocus.Exclusive : WlrKeyboardFocus.None
    visible: showing || card.opacity > 0.01
    mask: Region { item: root.showing ? backdrop : null }

    readonly property string home: Quickshell.env("HOME")
    readonly property string folder: Config.wallpaper.folder.replace(/^~/, home)
    readonly property string thumbs: home + "/.cache/rice/thumbs"
    readonly property string rice: home + "/.config/rices/zei"
    property string current: ""

    onShowingChanged: if (showing) {
        filter.text = ""
        currentFile.reload()
        current = currentFile.text().trim()
        makeThumbs.running = true
        filter.forceActiveFocus()
        focusLater.restart()             // the grid fills now that it's visible
    }

    FileView { id: currentFile; path: root.rice + "/current-wallpaper"; blockLoading: true; printErrors: false }

    // fill in thumbnails for new wallpapers (same cache + size as zei/wallpaper)
    Process {
        id: makeThumbs
        command: ["sh", "-c",
            "mkdir -p \"$2\"; for f in \"$1\"/*; do case \"$f\" in *.jpg|*.jpeg|*.png|*.webp|*.JPG|*.PNG) t=\"$2/$(basename \"$f\").jpg\"; " +
            "[ -f \"$t\" ] || magick \"$f[0]\" -thumbnail 400x225^ -gravity center -extent 400x225 -quality 85 \"$t\" 2>/dev/null;; esac; done",
            "sh", root.folder, root.thumbs]
    }

    FolderListModel {
        id: files
        folder: "file://" + root.folder
        nameFilters: ["*.jpg", "*.jpeg", "*.png", "*.webp", "*.JPG", "*.PNG"]
        showDirs: false
        sortField: FolderListModel.Name
        sortCaseSensitive: false
        // the folder loads after the grid exists: jump to the current wallpaper once it's in
        onStatusChanged: if (status === FolderListModel.Ready) Qt.callLater(root.focusCurrent)
    }

    readonly property var items: {
        const q = filter.text.trim().toLowerCase()
        const out = []
        for (let i = 0; i < files.count; i++) {
            const name = files.get(i, "fileName")
            if (!q || name.toLowerCase().includes(q)) out.push({ name: name, path: files.get(i, "filePath") })
        }
        return out
    }

    Timer { id: focusLater; interval: 60; onTriggered: root.focusCurrent() }
    function focusCurrent() {
        const i = items.findIndex(w => w.path === current)
        if (i >= 0 && !filter.text) { grid.currentIndex = i; grid.positionViewAtIndex(i, GridView.Center) }
    }

    function apply(item) {
        if (!item) return
        Quickshell.execDetached([root.rice + "/wallpaper", item.path])
        current = item.path
        close()
    }

    // ---------- view ----------
    Rectangle {
        id: backdrop
        anchors.fill: parent
        color: Theme.alpha("#000000", Math.min(0.7, Config.look.backdrop * 1.2) * card.opacity)
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
        readonly property int cols: Config.wallpaper.columns
        readonly property real cellW: 236
        width: cols * cellW + 32
        height: Math.min(root.height - 160, 640)
        x: Math.round((root.width - width) / 2)
        y: Math.round((root.height - height) / 2) + (root.showing ? 0 : 16)
        radius: Config.look.radius
        color: Theme.alpha(Theme.bgPanel, Config.panel.opacity)
        border.width: Theme.surfaceBorderWidth
        border.color: Theme.surfaceBorder
        clip: true

        opacity: root.showing ? 1 : 0
        scale: root.showing ? 1 : 0.96
        Behavior on opacity { NumberAnimation { duration: Config.ms(root.showing ? 220 : 140) } }
        Behavior on scale { NumberAnimation { duration: Config.ms(root.showing ? 400 : 160); easing.type: root.showing ? Easing.OutBack : Easing.InCubic; easing.overshoot: Config.bounce(1.2) } }
        Behavior on y { NumberAnimation { duration: Config.ms(root.showing ? 400 : 160); easing.type: Easing.OutQuint } }

        MouseArea { anchors.fill: parent }

        // header: title, filter, random
        Item {
            id: header
            width: parent.width
            height: 60

            CpIcon { x: 22; anchors.verticalCenter: parent.verticalCenter; cp: 0xf0e09; font.pixelSize: 20; color: Theme.accent }
            CpText {
                x: 52
                anchors.verticalCenter: parent.verticalCenter
                text: "Wallpapers"
                font.pixelSize: 15
                font.bold: true
            }

            Rectangle {
                anchors { right: randomBtn.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
                width: 260; height: 34
                radius: height / 2
                color: Theme.alpha(Theme.text, 0.06)
                border.width: 1
                border.color: filter.activeFocus ? Theme.alpha(Theme.accent, 0.6) : Theme.alpha(Theme.text, 0.08)
                CpIcon { x: 12; anchors.verticalCenter: parent.verticalCenter; cp: 0xf0349; font.pixelSize: 14; color: Theme.textDim }
                TextInput {
                    id: filter
                    anchors { left: parent.left; leftMargin: 34; right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    clip: true
                    onTextChanged: grid.currentIndex = 0
                    Keys.onPressed: e => {
                        const n = root.items.length, c = card.cols
                        if (e.key === Qt.Key_Escape) root.close()
                        else if (e.key === Qt.Key_Right) grid.currentIndex = Math.min(n - 1, grid.currentIndex + 1)
                        else if (e.key === Qt.Key_Left) grid.currentIndex = Math.max(0, grid.currentIndex - 1)
                        else if (e.key === Qt.Key_Down) grid.currentIndex = Math.min(n - 1, grid.currentIndex + c)
                        else if (e.key === Qt.Key_Up) grid.currentIndex = Math.max(0, grid.currentIndex - c)
                        else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter) root.apply(root.items[grid.currentIndex])
                        else if (e.key === Qt.Key_R && e.modifiers & Qt.ControlModifier) randomBtn.clicked()
                        else return
                        e.accepted = true
                    }
                }
                CpText {
                    anchors { left: filter.left; verticalCenter: parent.verticalCenter }
                    visible: !filter.text
                    text: `Filter ${root.items.length} wallpapers`
                    font.pixelSize: 12
                    color: Theme.textDim
                }
            }

            CpIconButton {
                id: randomBtn
                anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                icon: 0xf0450
                onClicked: root.apply(root.items[Math.floor(Math.random() * root.items.length)])
            }
        }

        GridView {
            id: grid
            anchors { top: header.bottom; left: parent.left; right: parent.right; bottom: parent.bottom; margins: 16; topMargin: 4 }
            cellWidth: card.cellW
            cellHeight: card.cellW * 9 / 16 + 34
            clip: true
            // thumbnails only exist while it's on screen (it stays loaded in between)
            model: root.visible ? root.items : []
            boundsBehavior: Flickable.StopAtBounds
            highlightFollowsCurrentItem: true
            highlightMoveDuration: Config.ms(180)
            Component.onCompleted: Qt.callLater(root.focusCurrent)

            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                readonly property bool selected: GridView.isCurrentItem
                readonly property bool isCurrent: modelData.path === root.current
                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    id: frame
                    x: 6; y: 6
                    width: parent.width - 12
                    height: width * 9 / 16
                    radius: Math.max(6, Config.look.radius - 6)
                    color: Theme.alpha(Theme.text, 0.06)
                    border.width: cell.selected ? 2 : 0
                    border.color: Theme.accent
                    scale: cell.selected ? 1.0 : (area.containsMouse ? 0.985 : 0.96)
                    Behavior on scale { NumberAnimation { duration: Config.ms(220); easing.type: Easing.OutBack; easing.overshoot: Config.bounce(1.4) } }

                    ClippingRectangle {
                        anchors { fill: parent; margins: cell.selected ? 3 : 0 }
                        radius: Math.max(4, parent.radius - (cell.selected ? 3 : 0))
                        color: "transparent"
                        Image {
                            id: thumb
                            anchors.fill: parent
                            // cached thumbnail, else the file itself scaled down off-thread
                            property bool fallback: false
                            source: fallback ? "file://" + cell.modelData.path
                                             : "file://" + root.thumbs + "/" + cell.modelData.name + ".jpg"
                            onStatusChanged: if (status === Image.Error && !fallback) fallback = true
                            sourceSize: Qt.size(400, 225)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: false
                            opacity: status === Image.Ready ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Config.ms(250) } }
                        }
                    }

                    // the wallpaper that's on now
                    Rectangle {
                        visible: cell.isCurrent
                        anchors { right: parent.right; top: parent.top; margins: 8 }
                        width: 22; height: 22; radius: 11
                        color: Theme.accent
                        CpIcon { anchors.centerIn: parent; cp: 0xf012c; font.pixelSize: 13; color: Theme.bgPanel }
                    }
                }

                CpText {
                    anchors { left: frame.left; right: frame.right; top: frame.bottom; topMargin: 6 }
                    text: cell.modelData.name.replace(/\.[^.]+$/, "")
                    font.pixelSize: 10
                    color: cell.selected ? Theme.text : Theme.textDim
                    elide: Text.ElideMiddle
                    horizontalAlignment: Text.AlignHCenter
                }

                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { grid.currentIndex = cell.index; root.apply(cell.modelData) }
                    onEntered: grid.currentIndex = cell.index
                }
            }
        }
    }
}
