import QtQuick
import QtQuick.Controls as QQC

// An answer to a question in a PopupCard: the controller button that gives it, then
// the word. Out of the focus chain: the question's text keeps the keys.
QQC.AbstractButton {
    id: control
    /** The controller button's picture, from root.controllerButton("cross"). */
    property url glyph
    /** The answer the question expects; filled. */
    property bool primary: false

    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    implicitHeight: 44
    implicitWidth: Math.max(96, row.implicitWidth + 36)
    font.pixelSize: 15
    font.weight: Font.ExtraBold

    background: Rectangle {
        radius: height / 2
        color: control.primary ? Theme.button : Qt.rgba(1, 1, 1, 0.06)
        border.width: control.primary ? 0 : 1
        border.color: Qt.rgba(1, 1, 1, 0.12)

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "#ffffff"
            opacity: control.down ? 0.14 : control.hovered ? 0.07 : 0
        }
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8

            Image {
                visible: control.glyph.toString().length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: 24
                sourceSize: Qt.size(width, height)
                source: control.glyph
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: control.font
                color: Theme.text
            }
        }
    }
}
