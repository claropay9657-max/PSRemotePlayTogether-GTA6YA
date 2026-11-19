import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import "controls" as C

Dialog {
    id: dialog
    property alias text: label.text
    property var callback
    property var rejectCallback
    property bool newDialogOpen: false
    property Item restoreFocusItem
    parent: Overlay.overlay
    x: Math.round((root.width - width) / 2)
    y: Math.round((root.height - height) / 2)
    modal: true
    // A glass card in the GamerNova look, on a dimmed backdrop
    background: Rectangle {
        radius: 18
        color: Qt.rgba(13 / 255, 11 / 255, 31 / 255, 0.97)
        border.width: 1
        border.color: Qt.rgba(140 / 255, 82 / 255, 255 / 255, 0.6)
    }
    Overlay.modal: Rectangle {
        color: Qt.rgba(5 / 255, 5 / 255, 20 / 255, 0.62)
    }
    Material.roundedScale: Material.MediumScale
    onOpened: label.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        newDialogOpen = true;
        restoreFocus();
        callback();
    }
    onClosed: if(!newDialogOpen) { restoreFocus() }

    onRejected: {
        if(rejectCallback)
        {
            newDialogOpen = true;
            restoreFocus();
            rejectCallback();
        }
    }

    function restoreFocus() {
        if (restoreFocusItem)
            restoreFocusItem.forceActiveFocus(Qt.TabFocusReason);
        label.focus = false;
    }

    Component.onCompleted: {
        header.horizontalAlignment = Text.AlignHCenter;
        // Qt 6.6: Workaround dialog background becoming immediately transparent during close animation
        header.background = null;
    }

    ColumnLayout {
        spacing: 20

        Label {
            id: label
            Keys.onEscapePressed: dialog.reject()
            Keys.onReturnPressed: dialog.accept()
        }

        RowLayout {
            Layout.alignment: Qt.AlignCenter
            spacing: 20

            Button {
                text: qsTr("Yes")
                Material.background: Material.accent
                flat: true
                leftPadding: 50
                onClicked: dialog.accept()
                Material.roundedScale: Material.SmallScale

                Image {
                    anchors {
                        left: parent.left
                        verticalCenter: parent.verticalCenter
                        leftMargin: 12
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: root.controllerButton("cross")
                }
            }

            Button {
                Material.background: Material.accent
                text: qsTr("No")
                flat: true
                leftPadding: 50
                onClicked: dialog.reject()
                Material.roundedScale: Material.SmallScale

                Image {
                    anchors {
                        left: parent.left
                        verticalCenter: parent.verticalCenter
                        leftMargin: 12
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: root.controllerButton("moon")
                }
            }
        }
    }
}
