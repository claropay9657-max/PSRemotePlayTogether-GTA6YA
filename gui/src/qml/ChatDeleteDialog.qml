import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import "controls" as C

// "Delete for everybody?" inside the chat it is about: during a game the chats are windows of
// their own over the picture, where the app's own dialogs would not show. ask(then) opens it;
// then() runs on Delete.
Popup {
    id: dialog

    property var then: null

    function ask(action) {
        then = action;
        open();
    }

    modal: true
    focus: true
    anchors.centerIn: parent
    width: Math.min(340, parent ? parent.width - 40 : 340)
    padding: 20
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: 16
        color: "#17132d"
        border.color: C.Theme.lineStrong
    }

    contentItem: ColumnLayout {
        spacing: 10

        Label {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.pixelSize: 16
            font.bold: true
            color: C.Theme.text
            text: qsTr("Delete for everybody?")
        }

        Label {
            Layout.fillWidth: true
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.pixelSize: 13
            color: C.Theme.textSoft
            text: qsTr("The message goes away for everybody in this chat. Whoever has read it already has seen it.")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 8

            Item {
                Layout.fillWidth: true
            }

            C.PillButton {
                small: true
                text: qsTr("Cancel")
                onClicked: dialog.close()
            }

            C.PillButton {
                small: true
                kind: "danger"
                text: qsTr("Delete")
                onClicked: {
                    const action = dialog.then;
                    dialog.close();
                    if (action)
                        action();
                }
            }
        }
    }
}
