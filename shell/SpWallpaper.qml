// SpWallpaper.qml — Kusanagi's wallpaper engine: transitions, fit, parallax, dim, slideshow, picker
import Quickshell
import Quickshell.Io
import QtQuick

Column {
    id: page
    spacing: 22

    readonly property string rice: Quickshell.env("HOME") + "/.config/rices/zei"
    FileView { id: cur; path: page.rice + "/current-wallpaper"; watchChanges: true; onFileChanged: reload(); printErrors: false }

    // re-apply the current wallpaper's neighbour to see the transition
    function tryIt() {
        Quickshell.execDetached(["sh", "-c",
            "cur=$(cat \"$1\"); f=$(ls -1 \"$2\"/*.jpg \"$2\"/*.png 2>/dev/null | grep -vxF \"$cur\" | shuf -n1); [ -n \"$f\" ] && \"$3\" \"$f\"",
            "sh", page.rice + "/current-wallpaper", Config.wallpaper.folder.replace(/^~/, Quickshell.env("HOME")), page.rice + "/wallpaper"])
    }

    SpGroup {
        title: "Current"
        Item {
            width: parent.width; height: 120
            Rectangle {
                id: thumb
                width: 213; height: 120; radius: Math.max(6, Config.look.radius - 6)
                color: Theme.alpha(Theme.text, 0.06)
                clip: true
                Image {
                    anchors.fill: parent
                    source: cur.loaded ? "file://" + cur.text().trim() : ""
                    sourceSize: Qt.size(426, 240)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
            }
            Column {
                anchors { left: thumb.right; leftMargin: 18; verticalCenter: parent.verticalCenter }
                spacing: 10
                CpText {
                    width: page.width - 260
                    text: cur.loaded ? cur.text().trim().split("/").pop() : ""
                    font.pixelSize: 13; font.bold: true
                    elide: Text.ElideMiddle
                }
                Row {
                    spacing: 8
                    CpChip { label: "Pick…"; icon: 0xf0e09; onClicked: Quickshell.execDetached(["kusanagi", "msg", "wallpaper", "toggle"]) }
                    CpChip { label: "Random"; icon: 0xf0450; onClicked: page.tryIt() }
                }
            }
        }
    }

    SpGroup {
        title: "Engine"
        hint: "Kusanagi draws the wallpaper itself (transitions, parallax, slideshow). awww is the fallback."
        CpSegmented {
            width: parent.width
            current: Config.wallpaper.renderer
            options: [{ label: "Kusanagi", value: "kusanagi" }, { label: "awww", value: "awww" }]
            onPicked: v => {
                Config.wallpaper.renderer = v
                // hand it straight over (awww needs to be told to draw again)
                if (v === "awww") Quickshell.execDetached(["sh", "-c", "sleep 0.4; \"$1\" --restore", "sh", page.rice + "/wallpaper"])
            }
        }
    }

    SpGroup {
        title: "Transition"
        visible: Config.wallpaper.renderer === "kusanagi"
        Flow {
            width: parent.width
            spacing: 6
            Repeater {
                model: [
                    { v: "random", l: "Random" }, { v: "fade", l: "Fade" }, { v: "blur", l: "Blur" }, { v: "wipe", l: "Wipe" },
                    { v: "grow", l: "Grow" }, { v: "slide", l: "Slide" }, { v: "zoom", l: "Zoom" }, { v: "blinds", l: "Blinds" }
                ]
                CpChip {
                    required property var modelData
                    label: modelData.l
                    on: Config.wallpaper.transition === modelData.v
                    onClicked: Config.wallpaper.transition = modelData.v
                }
            }
        }
        CpRow {
            width: parent.width; label: "Speed"
            CpStepper { value: Config.wallpaper.duration / 100; from: 3; to: 30; suffix: "00 ms"; onChanged: v => Config.wallpaper.duration = v * 100 }
        }
        CpChip { label: "Try it"; icon: 0xf0208; onClicked: page.tryIt() }
    }

    SpGroup {
        title: "Picture"
        visible: Config.wallpaper.renderer === "kusanagi"
        CpRow {
            width: parent.width; label: "Fit"
            CpSegmented {
                width: 380; current: Config.wallpaper.fill
                options: [{ label: "Fill", value: "fill" }, { label: "Fit", value: "fit" }, { label: "Stretch", value: "stretch" },
                          { label: "Centre", value: "center" }, { label: "Tile", value: "tile" }]
                onPicked: v => Config.wallpaper.fill = v
            }
        }
        CpSlider {
            width: parent.width; height: 30
            icon: 0xf0e09; label: "Parallax"
            value: Config.wallpaper.parallax / 0.12
            valueText: Config.wallpaper.parallax > 0 ? Math.round(Config.wallpaper.parallax * 100) + "%" : "off"
            onMoved: v => Config.wallpaper.parallax = Math.round(v * 0.12 * 100) / 100
        }
        CpText { width: parent.width; wrapMode: Text.WordWrap; font.pixelSize: 10; color: Theme.textDim
                 text: "Parallax pans the picture a little as you move between workspaces." }
        CpSlider {
            width: parent.width; height: 30
            icon: 0xf050e; label: "Dim"
            value: Config.wallpaper.dim / 0.6
            valueText: Math.round(Config.wallpaper.dim * 100) + "%"
            onMoved: v => Config.wallpaper.dim = Math.round(v * 0.6 * 100) / 100
        }
    }

    SpGroup {
        title: "Slideshow"
        visible: Config.wallpaper.renderer === "kusanagi"
        hint: "A random wallpaper from the folder every so often — re-themed like a manual pick."
        CpRow {
            width: parent.width; label: "Change every"
            CpSegmented {
                width: 380; current: Config.wallpaper.slideshow
                options: [{ label: "Off", value: 0 }, { label: "5 min", value: 5 }, { label: "15 min", value: 15 },
                          { label: "1 h", value: 60 }, { label: "3 h", value: 180 }]
                onPicked: v => Config.wallpaper.slideshow = v
            }
        }
    }

    SpGroup {
        title: "Picker"
        hint: "Super+A"
        CpRow {
            width: parent.width; label: "Folder"
            CpField { width: 280; text: Config.wallpaper.folder; onAccepted: t => { if (t.trim()) Config.wallpaper.folder = t.trim() } }
        }
        CpRow {
            width: parent.width; label: "Columns"
            CpStepper { value: Config.wallpaper.columns; from: 3; to: 6; onChanged: v => Config.wallpaper.columns = v }
        }
    }
}
