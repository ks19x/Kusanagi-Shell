// CpInbox.qml — notification history with do-not-disturb and clear-all
import QtQuick

Column {
    id: root
    spacing: 12

    Item {
        width: parent.width
        height: 30

        CpText {
            anchors.verticalCenter: parent.verticalCenter
            text: Notifs.count ? `${Notifs.count} notification${Notifs.count === 1 ? "" : "s"}` : "No notifications"
            font.pixelSize: 13
            font.bold: true
        }

        Row {
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            spacing: 10
            CpText { anchors.verticalCenter: parent.verticalCenter; text: "Do not disturb"; font.pixelSize: 11; color: Theme.textDim }
            CpSwitch { anchors.verticalCenter: parent.verticalCenter; on: Notifs.dnd; onToggled: v => Notifs.dnd = v }
            CpChip {
                anchors.verticalCenter: parent.verticalCenter
                visible: Notifs.count > 0
                label: "Clear all"
                icon: 0xf0a7a
                onClicked: Notifs.clearAll()
            }
        }
    }

    ListView {
        id: list
        width: parent.width
        height: Notifs.count ? Math.min(contentHeight, 470) : 0
        visible: Notifs.count > 0
        clip: true
        spacing: 8
        boundsBehavior: Flickable.StopAtBounds
        model: Notifs.history.slice().reverse()
        delegate: NotificationCard {
            required property var modelData
            width: list.width
            notif: modelData
        }
        add: Transition { NumberAnimation { properties: "opacity"; from: 0; to: 1; duration: Config.ms(240) } }
        removeDisplaced: Transition { NumberAnimation { properties: "y"; duration: Config.ms(260); easing.type: Easing.OutCubic } }
    }

    // empty state
    Column {
        width: parent.width
        visible: Notifs.count === 0
        topPadding: 24
        bottomPadding: 28
        spacing: 8
        CpIcon { anchors.horizontalCenter: parent.horizontalCenter; cp: 0xf11e5; font.pixelSize: 34; color: Theme.alpha(Theme.text, 0.25) }
        CpText { anchors.horizontalCenter: parent.horizontalCenter; text: "All caught up"; font.pixelSize: 12; color: Theme.textDim }
    }
}
