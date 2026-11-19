import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window

import org.streetpea.chiaking

// The display options while a game is running, opened from the stream menu.
// A window of its own in front of the game picture, so that it works the same
// with every renderer; the game picture stays visible around it, and what is
// switched here shows there at once.
Window {
    id: panel

    /** The stream view: gives the place of the game picture on screen. */
    required property Item view
    property bool open: false
    /** The user wants to drag the elements on the game picture into place. */
    signal arrangeRequested()

    transientParent: view.hostWindow
    flags: Qt.Tool | Qt.FramelessWindowHint
    color: "transparent"
    width: Math.max(360, Math.min(view.width - 60, 1000))
    height: Math.max(300, Math.min(view.height - 60, 780))
    x: view.streamGlobalX + Math.round((view.width - width) / 2)
    y: view.streamGlobalY + Math.round((view.height - height) / 2)
    visible: open

    onOpenChanged: {
        if (open) {
            // In front of the display on the game picture, which is a window too
            raise();
            requestActivate();
        } else if (view.hostWindow) {
            view.hostWindow.requestActivate();
        }
    }

    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: panel.open = false
    }

    Rectangle {
        anchors.fill: parent
        radius: 18
        color: Qt.rgba(13 / 255, 11 / 255, 31 / 255, 0.97)
        border.width: 1
        border.color: "#8c52ff"
        clip: true

        Material.theme: Material.Dark
        Material.accent: "#8c52ff"
        Material.primary: "#161230"
        Material.background: "#0d0b1f"

        RowLayout {
            id: header
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                leftMargin: 22
                rightMargin: 12
                topMargin: 10
            }
            height: 48

            Label {
                Layout.fillWidth: true
                font.pixelSize: 22
                font.bold: true
                text: qsTr("Overlays")
            }

            Label {
                opacity: 0.6
                font.pixelSize: 13
                text: qsTr("Changes apply at once")
            }

            ToolButton {
                text: "×"
                font.pixelSize: 26
                onClicked: panel.open = false
            }
        }

        Flickable {
            id: flick
            anchors {
                left: parent.left
                right: parent.right
                top: header.bottom
                bottom: parent.bottom
                margins: 18
                topMargin: 8
            }
            contentWidth: width
            contentHeight: content.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }

            DisplayOptions {
                id: content
                width: flick.width - 14
                inSession: true
                onArrangeRequested: {
                    panel.open = false;
                    panel.arrangeRequested();
                }
            }
        }
    }
}
