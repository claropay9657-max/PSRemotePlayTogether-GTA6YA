import QtQuick
import QtQuick.Controls as QQC

// A thing that is on or off, as a small button with an icon: purple and outlined
// while on. For a few such things side by side ("Picture", "Sound"). Bind `checked`
// and change the setting in onToggled.
QQC.AbstractButton {
    id: chip
    property string iconName
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false

    checkable: true
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: 36
    implicitWidth: content.implicitWidth + 24
    font.pixelSize: 13
    font.weight: Font.ExtraBold
    // Greyed out on its own; inside something greyed out already (a row), not a second time
    opacity: enabled || (parent && !parent.enabled) ? 1 : 0.5
    Accessible.role: Accessible.CheckBox
    Accessible.checked: checked

    Keys.onPressed: (event) => FocusChain.handle(chip, event)

    background: Rectangle {
        radius: 10
        color: chip.checked ? Qt.rgba(140 / 255, 82 / 255, 1, chip.hovered ? 0.26 : 0.18)
                            : Qt.rgba(1, 1, 1, chip.hovered ? 0.07 : 0.04)
        border.width: chip.visualFocus ? 2 : 1
        border.color: chip.visualFocus ? Theme.focus : chip.checked ? Theme.brandFrom : Theme.lineStrong
    }

    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight

        Row {
            id: content
            anchors.centerIn: parent
            spacing: 7

            Icon {
                visible: chip.iconName.length > 0
                anchors.verticalCenter: parent.verticalCenter
                iconName: chip.iconName
                size: 15
                color: chip.checked ? "#ffffff" : Theme.textSoft
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: chip.text
                font: chip.font
                color: chip.checked ? "#ffffff" : Theme.textSoft
            }
        }
    }
}
