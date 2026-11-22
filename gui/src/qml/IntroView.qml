import QtQuick
import QtQuick.Controls

import org.streetpea.chiaking

import "intro" as I

// The intro film as a page of the app (Main.qml puts it on top of the start page): by itself
// at the first start of a new installation, and whenever "How it works" on the start page is
// pressed. Leaving it, by its end, by Skip or through one of its two doors, counts as seen.
Item {
    id: view

    function seen() {
        Chiaki.settings.introState = "seen";
    }

    /** Through a door at the end: straight on to hosting or joining, without the start page's first questions in between. */
    function onward(open) {
        seen();
        root.initialAsk = true;
        stack.pop(StackView.Immediate);
        open();
    }

    StackView.onActivated: player.forceActiveFocus(Qt.TabFocusReason)
    // The page is gone: the film's sound is given back
    Component.onDestruction: Chiaki.introSound.release()

    I.IntroPlayer {
        id: player
        anchors.fill: parent
        focus: true
        sound: Chiaki.introSound
        onClosed: {
            view.seen();
            stack.pop();
        }
        onHostChosen: view.onward(() => root.showHostDialog())
        onJoinChosen: view.onward(() => root.showJoinDialog())
    }
}
