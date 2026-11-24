import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window

import org.streetpea.chiaking

import "controls" as C

// Play Together text chat, beside the game picture. A window of its own like the
// overlay options, so that it shows with every renderer and takes the keyboard
// while it is open; the game gets it back when it closes. What is in it: RoomChat.qml.
Window {
    id: panel

    /** The stream view: gives the place of the game picture on screen. */
    required property Item view
    property bool open: false
    readonly property var session: Chiaki.session

    transientParent: view.hostWindow
    flags: Qt.Tool | Qt.FramelessWindowHint
    color: "transparent"
    width: Math.min(400, Math.max(300, view.width - 40))
    height: Math.max(320, view.height - 80)
    x: view.streamGlobalX + view.width - width - 20
    y: view.streamGlobalY + 40
    visible: open && !!session

    onOpenChanged: {
        if (open) {
            raise();
            requestActivate();
            chat.focusInput();
            if (session)
                session.coplayMarkChatRead();
            chat.toEnd();
        } else if (view.hostWindow) {
            view.hostWindow.requestActivate();
        }
    }

    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: {
            if (!chat.closePopups())
                panel.open = false;
        }
    }

    RoomChat {
        id: chat
        anchors.fill: parent
        session: panel.session
        open: panel.open
        onCloseRequested: panel.open = false
    }
}
