import QtQuick
import QtQuick.Controls as QQC

// A text field of the new look. As TextField.qml underneath: with a controller or the
// keys, Return starts typing and Up / Down move on; a click types at once.
// `hint` shows while it is empty: what goes in, or what is used without it.
TextField {
    id: field
    property string hint
    /** A line icon at the start, such as "search". */
    property string iconName

    implicitHeight: 40
    leftPadding: iconName ? 36 : 12
    rightPadding: 12
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    hoverEnabled: true
    color: Theme.text
    selectionColor: Theme.button
    selectedTextColor: "#ffffff"
    placeholderTextColor: Theme.textDim
    font.pixelSize: 14
    font.weight: Font.DemiBold
    opacity: enabled ? 1 : 0.5

    // The style's own placeholder would float onto the border: drawn here instead
    background: Rectangle {
        implicitWidth: 160
        implicitHeight: 40
        radius: Theme.radiusField
        color: Theme.field
        border.width: field.activeFocus ? 2 : 1
        border.color: !field.activeFocus ? Qt.rgba(1, 1, 1, field.hovered ? 0.2 : 0.12)
                    : field.readOnly ? Theme.focus : Theme.brandFrom

        Icon {
            visible: field.iconName.length > 0
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            iconName: field.iconName
            size: 16
            color: Theme.textSoft
        }

        Text {
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
                leftMargin: field.leftPadding
                rightMargin: field.rightPadding
            }
            visible: field.length === 0 && field.preeditText.length === 0
            elide: Text.ElideRight
            text: field.hint
            color: Theme.textDim
            font: field.font
        }
    }
}
