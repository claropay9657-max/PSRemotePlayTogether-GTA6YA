import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

// A number with − and +. Bind `value`; change the setting in onMoved.
RowLayout {
    id: stepper
    property int value: 0
    property int from: 0
    property int to: 10
    property string lessText: qsTr("Fewer")
    property string moreText: qsTr("More")
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false
    signal moved(int value)

    spacing: 4

    component StepButton: QQC.AbstractButton {
        id: step
        property string iconName
        property bool firstInFocusChain: false
        property bool lastInFocusChain: false
        implicitWidth: 34
        implicitHeight: 34
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        opacity: enabled ? 1 : 0.35
        Keys.onPressed: (event) => FocusChain.handle(step, event)
        background: Rectangle {
            radius: 10
            color: Qt.rgba(1, 1, 1, step.down ? 0.14 : step.hovered ? 0.1 : 0.07)
            border.width: step.visualFocus ? 2 : 0
            border.color: Theme.focus
        }
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                iconName: step.iconName
                size: 16
            }
        }
    }

    StepButton {
        iconName: "minus"
        Accessible.name: stepper.lessText
        enabled: stepper.value > stepper.from
        firstInFocusChain: stepper.firstInFocusChain
        onClicked: stepper.moved(stepper.value - 1)
    }

    Text {
        Layout.minimumWidth: 34
        horizontalAlignment: Text.AlignHCenter
        text: stepper.value
        font.pixelSize: 18
        font.weight: Font.ExtraBold
        color: Theme.text
    }

    StepButton {
        iconName: "plus"
        Accessible.name: stepper.moreText
        enabled: stepper.value < stepper.to
        lastInFocusChain: stepper.lastInFocusChain
        onClicked: stepper.moved(stepper.value + 1)
    }
}
