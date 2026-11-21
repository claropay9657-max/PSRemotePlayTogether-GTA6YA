import QtQuick
import QtQuick.Layouts
import "controls" as C

import org.streetpea.chiaking

// A question the app asks at the start (PSN, a Steam shortcut): Yes, No (never ask
// again) or Later. Return or Cross says yes, Escape or Circle no, Triangle later.
C.PopupCard {
    id: dialog
    property alias text: label.text
    property var remotePlay
    property var callback
    property bool newDialogOpen: false
    property Item restoreFocusItem

    onOpened: label.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: {
        newDialogOpen = true;
        restoreFocus();
        callback();
    }
    onRejected: {
        if(dialog.remotePlay)
            Chiaki.settings.remotePlayAsk = false;
        else
            Chiaki.settings.addSteamShortcutAsk = false;
    }
    onClosed: {
        if(newDialogOpen)
            return;
        restoreFocus();
        if(!remotePlay && Chiaki.settings.remotePlayAsk)
        {
            if(!Chiaki.settings.psnRefreshToken || !Chiaki.settings.psnAuthToken || !Chiaki.settings.psnAuthTokenExpiry || !Chiaki.settings.psnAccountId)
                root.showRemindDialog(qsTr("Remote Play via PSN"), qsTr("Would you like to connect to PSN?\nThis enables:\n- Automatic registration\n- Playing outside of your home network without port forwarding?") + "\n\n" + qsTr("(If you say no now and want it later: Settings › Away from home (PSN).)"), true, () => root.showPSNTokenDialog(false));
            else
                Chiaki.settings.remotePlayAsk = false;
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
            Layout.preferredWidth: Math.min(implicitWidth, 480)
            wrapMode: Text.WordWrap
            lineHeight: 1.2
            color: C.Theme.textSoft
            font.pixelSize: 14
            Keys.onEscapePressed: dialog.reject()
            Keys.onReturnPressed: dialog.accept()
            Keys.onYesPressed: dialog.close()
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 10

            C.GlyphButton {
                text: qsTr("Remind me later")
                glyph: root.controllerButton("pyramid")
                onClicked: dialog.close()
            }

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
