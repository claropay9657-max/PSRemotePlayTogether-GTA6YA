import QtQuick
import QtQuick.Window

// Friends during a session: the friends panel (FriendsPanel.qml) in a window of its own beside
// the game picture, like the chat (StreamChatPanel.qml), so that it shows with every renderer and
// takes the keyboard while it is open; the game gets it back when it closes.
Window {
    id: win

    /** The stream view: gives the place of the game picture on screen. */
    required property Item view
    property bool open: false

    transientParent: view.hostWindow
    flags: Qt.Tool | Qt.FramelessWindowHint
    color: "transparent"
    width: Math.min(460, Math.max(320, view.width - 40))
    height: Math.max(360, view.height - 80)
    x: view.streamGlobalX + view.width - width - 20
    y: view.streamGlobalY + 40
    visible: open && !!Chiaki.session

    onOpenChanged: {
        if (open) {
            raise();
            requestActivate();
            panel.show("");
        } else {
            panel.close();
            if (view.hostWindow)
                view.hostWindow.requestActivate();
        }
    }

    FriendsPanel {
        id: panel
        parent: win.contentItem
        modal: false
        dim: false
        onClosed: win.open = false
    }
}
