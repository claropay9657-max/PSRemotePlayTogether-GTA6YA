import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import "controls"

Item {
    id: dialog
    property alias header: headerLabel.text
    property alias title: titleLabel.text
    property alias buttonText: okButton.text
    property alias buttonEnabled: okButton.enabled
    property alias buttonVisible: okButton.visible
    property Item restoreFocusItem
    default property Item mainItem: null

    signal accepted()
    signal rejected()

    function close() {
        root.closeDialog();
    }

    Keys.onEscapePressed: close()

    Keys.onMenuPressed: {
        if (okButton.enabled)
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

    NovaBackground {
        anchors.fill: parent
    }

    ToolBar {
        id: toolBar
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 80
        background: GlassBar { }

        RowLayout {
            anchors {
                fill: parent
                leftMargin: 10
                rightMargin: 10
            }

            Button {
                Layout.fillHeight: true
                Layout.preferredWidth: 100
                flat: true
                text: "❮"
                focusPolicy: Qt.NoFocus
                Material.roundedScale: Material.SmallScale
                onClicked: {
                    dialog.rejected();
                    dialog.close();
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                id: okButton
                Layout.alignment: Qt.AlignVCenter
                Layout.rightMargin: 8
                implicitHeight: 52
                leftPadding: 16
                rightPadding: 24
                spacing: 8
                font.pixelSize: 19
                font.bold: true
                focusPolicy: Qt.NoFocus
                onClicked: dialog.accepted()
                icon.source: "qrc:/icons/options.svg";
                icon.width: 30
                icon.height: 30
                // The one thing this page is for: filled, so that it is found at a glance
                background: Rectangle {
                    radius: height / 2
                    opacity: okButton.enabled ? (okButton.down ? 0.75 : (okButton.hovered ? 1.0 : 0.92)) : 0.28
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#8c52ff" }
                        GradientStop { position: 1.0; color: "#5b6cff" }
                    }
                    border.width: 1
                    border.color: Qt.rgba(1, 1, 1, okButton.hovered ? 0.35 : 0.16)
                }
            }
        }

        Label {
            id: titleLabel
            anchors.centerIn: parent
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            font.bold: true
            font.pixelSize: 26
        }

        Label {
            id: headerLabel
            anchors {
                top: parent.top
                left: titleLabel.right
                right: parent.right
                verticalCenter: parent.verticalCenter
            }
            horizontalAlignment: Qt.AlignHCenter
            verticalAlignment: Qt.AlignVCenter
            font.bold: true
            font.pixelSize: 14
        }
    }

    Item {
        id: contentItem
        anchors {
            top: toolBar.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
    }
}
