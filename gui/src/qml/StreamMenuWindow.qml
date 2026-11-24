import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window

import org.streetpea.chiaking

// The stream menu in a window of its own, for the renderers where it cannot be part of the
// stream window's scene. It covers the whole game picture, so that the menu's lists and the
// player menu open inside it wherever there is room; the menu slides in and out inside it.
Window {
    id: streamMenuWindow

    property bool open: false
    property bool closing: false

    signal closeRequested()
    signal displaySettingsRequested()
    signal placeboSettingsRequested()
    signal mainViewRequested()
    signal displayPanelRequested()
    signal chatRequested()
    signal friendsRequested()
    signal arrangeRequested()
    signal closeAnimationFinished()

    visible: open || closing
    flags: Qt.Tool | Qt.FramelessWindowHint
    color: "transparent"
    modality: Qt.NonModal

    function focusInitialItem() {
        requestActivate();
        content.initialFocusItem.forceActiveFocus(Qt.TabFocusReason);
    }

    onVisibleChanged: {
        if (visible)
            focusTimer.restart();
    }

    onActiveChanged: {
        if (active && visible)
            focusTimer.restart();
    }

    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: streamMenuWindow.closeRequested()
    }

    Shortcut {
        sequence: "Ctrl+O"
        onActivated: {
            if (streamMenuWindow.open)
                streamMenuWindow.closeRequested();
        }
    }

    // The big chat, as in the stream window (the panel's chat says so)
    Shortcut {
        sequence: "Ctrl+T"
        enabled: streamMenuWindow.open && !!Chiaki.session && Chiaki.session.coplayActive
        onActivated: streamMenuWindow.chatRequested()
    }

    Timer {
        id: focusTimer
        interval: 0
        repeat: false
        onTriggered: {
            if (streamMenuWindow.visible)
                streamMenuWindow.focusInitialItem();
        }
    }

    StreamMenuContent {
        id: content
        anchors.fill: parent
        focus: true
        shown: streamMenuWindow.open
        onHidden: streamMenuWindow.closeAnimationFinished()
        onCloseRequested: streamMenuWindow.closeRequested()
        onDisplaySettingsRequested: streamMenuWindow.displaySettingsRequested()
        onPlaceboSettingsRequested: streamMenuWindow.placeboSettingsRequested()
        onDisplayPanelRequested: streamMenuWindow.displayPanelRequested()
        onMainViewRequested: streamMenuWindow.mainViewRequested()
        onChatRequested: streamMenuWindow.chatRequested()
        onFriendsRequested: streamMenuWindow.friendsRequested()
        onArrangeRequested: streamMenuWindow.arrangeRequested()
    }
}
