import QtQuick
import QtQuick.Controls as QQC

// A square button with only an icon: settings, more, refresh, back. Says what it
// does in a tooltip and to screen readers.
QQC.AbstractButton {
    id: control
    property string iconName
    property string tip
    property bool round: false
    property bool small: false
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: small ? 34 : Theme.touch
    implicitHeight: implicitWidth
    Accessible.name: tip
    QQC.ToolTip.visible: hovered && tip.length > 0
    QQC.ToolTip.text: tip
    QQC.ToolTip.delay: 600

    Keys.onPressed: (event) => FocusChain.handle(control, event)

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: control.round ? height / 2 : 12
            color: Qt.rgba(1, 1, 1, control.down ? 0.12 : control.hovered ? 0.09 : 0.05)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.09)
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: control.round ? height / 2 : 15
            color: "transparent"
            border.width: 2
            border.color: Theme.focus
            visible: control.visualFocus
        }
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            iconName: control.iconName
            size: control.small ? 16 : 20
            color: "#d9d5f2"
        }
    }
}
