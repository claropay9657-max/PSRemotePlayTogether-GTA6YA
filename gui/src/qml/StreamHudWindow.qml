import QtQuick
import QtQuick.Window

import org.streetpea.chiaking

// The display on top of the game picture (StreamHud) in a see-through window of
// its own: on macOS, and with the OpenGL renderer everywhere. Otherwise the
// display is part of the stream window's scene (see StreamView.hudInline).
Window {
    id: hudWindow

    /** The stream view: gives the place of the game picture on screen. */
    required property Item view
    /** The display itself: `editing`, `allowed` and `chatOpen` are set on it. */
    readonly property Item hud: content

    // A window that never takes the focus only stays in front of the stream window if it
    // belongs to it. view.hostWindow is the scene's own window, which is never on screen:
    // macOS keeps this kind of window in front anyway, elsewhere it has to be the real one.
    transientParent: Qt.platform.os === "osx" ? view.hostWindow : Chiaki.window
    color: "transparent"
    // While playing every click and key goes through to the game
    flags: content.editing
           ? (Qt.Tool | Qt.FramelessWindowHint | Qt.NoDropShadowWindowHint)
           : (Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput | Qt.NoDropShadowWindowHint)
    x: view.streamGlobalX
    y: view.streamGlobalY
    width: Math.max(1, view.width)
    height: Math.max(1, view.height)
    visible: content.shown

    StreamHud {
        id: content
        anchors.fill: parent
        view: hudWindow.view
        onEditingChanged: {
            if (editing) {
                hudWindow.requestActivate();
                forceActiveFocus();
            } else if (hudWindow.view.hostWindow) {
                hudWindow.view.hostWindow.requestActivate();
            }
        }
    }
}
