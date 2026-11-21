import QtQuick
import QtQuick.Layouts
import "controls" as C

// A yes-or-no question over the window (Main.qml's showConfirmDialog). Return or
// Cross says yes, Escape or Circle says no.
C.PopupCard {
    id: dialog
    property alias text: label.text
    property var callback
    property var rejectCallback
    property bool newDialogOpen: false
    property Item restoreFocusItem

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

    ColumnLayout {
        spacing: 20

        Text {
            id: label
            Layout.preferredWidth: Math.min(implicitWidth, 460)
            wrapMode: Text.WordWrap
            lineHeight: 1.2
            color: C.Theme.textSoft
            font.pixelSize: 14
            Keys.onEscapePressed: dialog.reject()
            Keys.onReturnPressed: dialog.accept()
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 10

            C.GlyphButton {
                text: qsTr("No")
                glyph: root.controllerButton("moon")
                onClicked: dialog.reject()
            }

            C.GlyphButton {
                text: qsTr("Yes")
                primary: true
                glyph: root.controllerButton("cross")
                onClicked: dialog.accept()
            }
        }
    }
}
