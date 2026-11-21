import QtQuick
import QtQuick.Controls as QQC

// A button of the new look: a pill at least 44 px high. `kind` says what it does:
// "brand" starts something (the gradient, one per screen), "primary" goes on,
// "light" plays now, "ghost" for everything else, "danger" leaves or removes,
// "success" lets somebody in.
QQC.AbstractButton {
    id: control
    property string kind: "ghost"
    property bool small: false
    /** A line icon by name (controls/Icon.qml). */
    property string iconName
    property bool iconAfter: false
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false

    readonly property color foreground: kind === "light" ? Theme.textOnLight
                                       : kind === "danger" ? "#ffc2d1"
                                       : Theme.text

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitHeight: small ? 34 : Theme.touch
    implicitWidth: Math.max(implicitHeight, content.implicitWidth + (small ? 26 : 40))
    font.pixelSize: small ? 13 : 15
    font.weight: kind === "ghost" ? Font.Bold : Font.ExtraBold
    // Greyed out on its own; inside something greyed out already (a row), not a second time
    opacity: enabled || (parent && !parent.enabled) ? 1 : 0.45

    Keys.onPressed: (event) => FocusChain.handle(control, event)

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            visible: control.kind !== "brand"
            color: control.kind === "primary" ? Theme.button
                 : control.kind === "light" ? Theme.text
                 : control.kind === "success" ? Theme.goodButton
                 : control.kind === "danger" ? Qt.rgba(0.94, 0.27, 0.43, 0.16)
                 : Qt.rgba(1, 1, 1, 0.06)
            border.width: control.kind === "ghost" || control.kind === "danger" ? 1 : 0
            border.color: control.kind === "danger" ? Qt.rgba(0.94, 0.27, 0.43, 0.55) : Qt.rgba(1, 1, 1, 0.12)
        }

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            visible: control.kind === "brand"
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.buttonFrom }
                GradientStop { position: 1.0; color: Theme.buttonTo }
            }
        }

        // Hover and press
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: control.kind === "light" ? "#000000" : "#ffffff"
            opacity: control.down ? 0.14 : control.hovered ? 0.07 : 0
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: height / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.focus
            visible: control.visualFocus
        }
    }

    contentItem: Item {
        implicitWidth: content.implicitWidth
        implicitHeight: content.implicitHeight

        Row {
            id: content
            anchors.centerIn: parent
            spacing: 8
            layoutDirection: control.iconAfter ? Qt.RightToLeft : Qt.LeftToRight

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.iconName.length > 0
                iconName: control.iconName
                size: control.small ? 15 : 18
                color: control.foreground
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: text.length > 0
                text: control.text
                font: control.font
                color: control.foreground
            }
        }
    }
}
