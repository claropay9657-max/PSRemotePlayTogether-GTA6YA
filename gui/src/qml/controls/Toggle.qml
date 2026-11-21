import QtQuick
import QtQuick.Controls as QQC

// An on/off switch of the new look: purple when on. Use onToggled to change the
// setting; bind `checked` to it.
QQC.Switch {
    id: control
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    spacing: 10
    implicitWidth: text.length > 0 ? 44 + spacing + contentItem.implicitWidth - contentItem.leftPadding : 44
    implicitHeight: 26
    font.pixelSize: 13

    Keys.onPressed: (event) => FocusChain.handle(control, event)

    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 26
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: 13
        color: control.checked ? Theme.button : "#3a3458"
        border.width: control.visualFocus ? 2 : 0
        border.color: Theme.focus

        Behavior on color { ColorAnimation { duration: 120 } }

        Rectangle {
            width: 20
            height: 20
            radius: 10
            y: 3
            x: control.checked ? parent.width - width - 3 : 3
            color: control.checked ? "#ffffff" : "#cfcbe6"
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }

    contentItem: Text {
        leftPadding: control.text.length > 0 ? control.indicator.width + control.spacing : 0
        text: control.text
        font: control.font
        color: "#e7e4fa"
        verticalAlignment: Text.AlignVCenter
        visible: control.text.length > 0
    }
}
