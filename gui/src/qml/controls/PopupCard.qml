import QtQuick
import QtQuick.Controls as QQC

// A question or a note over the whole window, in the new look: a card on a dimmed
// backdrop, its title on top. ConfirmDialog, RemindDialog and Main.qml's info note
// are made of it; their text holds the focus, so that Return and Escape (Cross and
// Circle on a controller) answer.
QQC.Dialog {
    id: card
    parent: QQC.Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    modal: true
    padding: 24
    topPadding: 14
    bottomPadding: 22

    background: Rectangle {
        radius: 20
        color: Theme.popup
        border.width: 1
        border.color: Theme.lineStrong
    }

    QQC.Overlay.modal: Rectangle {
        color: Qt.rgba(5 / 255, 5 / 255, 20 / 255, 0.62)
    }

    header: Text {
        visible: text.length > 0
        textFormat: Text.PlainText
        text: card.title
        leftPadding: 24
        rightPadding: 24
        topPadding: 22
        wrapMode: Text.WordWrap
        color: Theme.text
        font.pixelSize: 18
        font.weight: Font.ExtraBold
    }
}
