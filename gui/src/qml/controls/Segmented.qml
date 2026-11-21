import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

// Two or three choices, all on screen at once. Bind `currentIndex` to the setting
// and change the setting in onActivated: the control never sets it itself.
Rectangle {
    id: segmented
    /** The labels. */
    property var model: []
    property int currentIndex: 0
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false
    signal activated(int index)

    implicitHeight: 40
    implicitWidth: row.implicitWidth + 6
    radius: Theme.radiusField
    color: Theme.field
    border.width: 1
    border.color: Theme.line

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 3
        spacing: 2

        Repeater {
            id: repeater
            model: segmented.model

            QQC.AbstractButton {
                id: choice
                required property int index
                required property var modelData
                readonly property bool chosen: index === segmented.currentIndex
                property bool firstInFocusChain: segmented.firstInFocusChain && index === 0
                property bool lastInFocusChain: segmented.lastInFocusChain && index === repeater.count - 1

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: label.implicitWidth + 24
                text: modelData
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.RadioButton
                Accessible.checked: chosen
                onClicked: segmented.activated(index)

                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Left && index > 0) {
                        repeater.itemAt(index - 1).forceActiveFocus(Qt.TabFocusReason);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Right && index < repeater.count - 1) {
                        repeater.itemAt(index + 1).forceActiveFocus(Qt.TabFocusReason);
                        event.accepted = true;
                    } else {
                        FocusChain.handle(choice, event);
                    }
                }

                background: Rectangle {
                    radius: 9
                    color: choice.chosen ? Theme.button : Qt.rgba(1, 1, 1, choice.hovered ? 0.06 : 0)
                    border.width: choice.visualFocus ? 2 : 0
                    border.color: Theme.focus
                }

                contentItem: Text {
                    id: label
                    text: choice.text
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    font.weight: choice.chosen ? Font.ExtraBold : Font.Bold
                    color: choice.chosen ? "#ffffff" : "#d9d5f2"
                }
            }
        }
    }
}
