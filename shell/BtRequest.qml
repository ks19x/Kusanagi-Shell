// BtRequest.qml — what a device asks while pairing (Bt.request): confirm a code, type a PIN / passkey,
// show a code to type on the device, or allow it to connect.
import QtQuick

CpCard {
    id: card
    readonly property var r: Bt.request
    visible: r !== null
    height: visible ? col.implicitHeight + 24 : 0
    border.color: Theme.alpha(Theme.accent, 0.6)

    Column {
        id: col
        x: 14; y: 12
        width: parent.width - 28
        spacing: 10

        CpText {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: 12
            font.bold: true
            text: !card.r ? "" : card.r.kind === "confirm" ? `Pair with ${card.r.name}?`
                : card.r.kind === "display" ? `Type this on ${card.r.name}, then press Enter there`
                : card.r.kind === "authorize" ? `Let ${card.r.name} connect?`
                : `${card.r.name} wants a ${card.r.kind === "pin" ? "PIN" : "passkey"}`
        }
        CpText {
            visible: !!card.r && (card.r.kind === "confirm" || card.r.kind === "display")
            text: card.r ? card.r.code : ""
            font.pixelSize: 26
            font.bold: true
            font.letterSpacing: 4
            color: Theme.accent
        }
        CpText {
            visible: !!card.r && card.r.kind === "confirm"
            width: parent.width; wrapMode: Text.Wrap
            text: "Check it's the same number on the device."
            font.pixelSize: 11; color: Theme.textDim
        }
        CpField {
            id: code
            visible: !!card.r && (card.r.kind === "pin" || card.r.kind === "passkey")
            width: parent.width
            icon: 0xf0306
            placeholder: card.r && card.r.kind === "pin" ? "PIN (often 0000)" : "Passkey (numbers)"
            onAccepted: t => Bt.answer(true, t)
        }
        Row {
            spacing: 8
            CpChip {
                visible: !!card.r && card.r.kind !== "display"
                on: true
                icon: 0xf012c
                label: card.r && card.r.kind === "authorize" ? "Allow" : "Pair"
                onClicked: Bt.answer(true, code.text)
            }
            CpChip {
                icon: 0xf0156
                label: card.r && card.r.kind === "display" ? "Done" : "Cancel"
                onClicked: Bt.answer(false)
            }
        }
    }
}
