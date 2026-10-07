// WdgWeather.qml — now (big icon + temperature) and the next days. Location: Settings → Control panel.
import QtQuick

Column {
    id: wd
    property var w
    readonly property bool shown: Weather.current !== null
    spacing: 12
    Component.onCompleted: Weather.wanted = true
    Row {
        spacing: 16
        CpIcon { cp: Weather.icon; font.pixelSize: 64; color: Theme.accent; anchors.verticalCenter: parent.verticalCenter }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            CpText { text: Weather.temp + Weather.unit; font.pixelSize: 44; font.bold: true }
            CpText { text: Weather.desc + " · feels " + Weather.feels + "°"; font.pixelSize: 12; color: Theme.alpha(Theme.text, 0.75) }
            CpText { text: Weather.place; font.pixelSize: 11; color: Theme.textDim }
        }
    }
    Row {
        spacing: 22
        Repeater {
            model: Weather.days
            Column {
                required property var modelData
                spacing: 3
                CpText { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.day; font.pixelSize: 11; font.bold: true; color: Theme.alpha(Theme.text, 0.8) }
                CpIcon { anchors.horizontalCenter: parent.horizontalCenter; cp: modelData.icon; font.pixelSize: 22; color: Theme.text }
                CpText { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.hi + "° " + modelData.lo + "°"; font.pixelSize: 11; color: Theme.textDim }
            }
        }
    }
}
