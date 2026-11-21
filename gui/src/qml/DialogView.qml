import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Shapes

import org.streetpea.chiaking

import "controls"

// A page over the start page (register a console, a PIN, the renderer's options …),
// in the look of the settings window: the dark ground with a purple glow, a title
// bar with Back, the title, an optional line, and on the right the one thing the page
// is for (`buttonText`) as a filled button. Escape goes back, Options (Menu) on a
// controller presses the button, and the focus comes back where it was.
//
// The content is the item put into the page; it fills the room under the title bar.
Item {
    id: dialog
    property alias header: headerLabel.text
    property alias title: titleLabel.text
    property alias buttonText: okButton.text
    property alias buttonEnabled: okButton.enabled
    /** Shown only with a buttonText as well. */
    property bool buttonVisible: true
    property Item restoreFocusItem
    default property Item mainItem: null

    signal accepted()
    signal rejected()

    function close() {
        root.closeDialog();
    }

    Keys.onEscapePressed: close()

    Keys.onMenuPressed: {
        if (okButton.visible && okButton.enabled)
            okButton.clicked()
    }

    StackView.onDeactivating: {
        restoreFocusItem = Window.window.activeFocusItem;
    }

    StackView.onActivated: {
        if (!restoreFocusItem) {
            let item = mainItem.nextItemInFocusChain();
            if (item)
                item.forceActiveFocus(Qt.TabFocusReason);
        } else {
            restoreFocusItem.forceActiveFocus(Qt.TabFocusReason);
            restoreFocusItem = null;
        }
    }

    onMainItemChanged: {
        if (mainItem) {
            mainItem.parent = contentItem;
            mainItem.anchors.fill = contentItem;
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.ground
    }

    // A soft purple glow from the top left corner, as on the settings window
    Shape {
        width: parent.width
        height: 900
        transform: Scale { yScale: 380 / 900 }

        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: 0
                centerY: -90
                focalX: 0
                focalY: -90
                centerRadius: 900
                GradientStop { position: 0.0; color: Qt.rgba(140 / 255, 82 / 255, 1, 0.14) }
                GradientStop { position: 0.7; color: Qt.rgba(140 / 255, 82 / 255, 1, 0) }
            }
            startX: 0
            startY: 0
            PathLine { x: dialog.width; y: 0 }
            PathLine { x: dialog.width; y: 900 }
            PathLine { x: 0; y: 900 }
            PathLine { x: 0; y: 0 }
        }
    }

    Item {
        id: titleBar
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 64

        readonly property int gutter: dialog.width < 600 ? 16 : 32

        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            color: Qt.rgba(1, 1, 1, 0.06)
        }

        RowLayout {
            anchors {
                fill: parent
                leftMargin: titleBar.gutter
                rightMargin: titleBar.gutter
            }
            spacing: 14

            // Escape does the same, so it stays out of the focus chain
            IconButton {
                iconName: "back"
                tip: qsTr("Back")
                focusPolicy: Qt.NoFocus
                onClicked: {
                    dialog.rejected();
                    dialog.close();
                }
            }

            Text {
                id: titleLabel
                Layout.maximumWidth: Math.max(120, dialog.width * 0.45)
                elide: Text.ElideRight
                color: Theme.text
                font.pixelSize: 22
                font.weight: Font.ExtraBold
            }

            Text {
                id: headerLabel
                Layout.fillWidth: true
                visible: text.length > 0 && dialog.width >= 640
                elide: Text.ElideRight
                color: Theme.textSoft
                font.pixelSize: 13
            }

            Item {
                Layout.fillWidth: !headerLabel.visible
            }

            // Options on a controller presses the button, as Return does on it
            Image {
                visible: okButton.visible && okButton.enabled && Chiaki.controllers.length > 0
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                sourceSize: Qt.size(26, 26)
                source: "qrc:/icons/options.svg"
            }

            // The one thing the page is for; out of the focus chain, as before (Options or the mouse)
            PillButton {
                id: okButton
                visible: dialog.buttonVisible && text.length > 0
                kind: "brand"
                focusPolicy: Qt.NoFocus
                onClicked: dialog.accepted()
            }
        }
    }

    Item {
        id: contentItem
        anchors {
            top: titleBar.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
    }
}
