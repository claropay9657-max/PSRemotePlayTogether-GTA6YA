import QtQuick
import QtQuick.Controls as QQC

// An amount, of the new look: a slider with its value beside it ("100 %", "50 ms").
// As Slider.qml underneath: Left / Right change it, Up / Down move on. Bind `value`
// and change the setting in onMoved; set `valueText` to what the value means.
Slider {
    id: control
    property string valueText
    /** Room for the value on the right. */
    property int valueWidth: 64

    implicitWidth: 260
    implicitHeight: 32
    // Whole steps only, also when dragged: the settings behind are whole numbers
    snapMode: QQC.Slider.SnapAlways
    padding: 0
    leftPadding: 9
    rightPadding: (valueText.length > 0 ? valueWidth + 12 : 0) + 9
    hoverEnabled: true
    // Greyed out on its own; inside something greyed out already (a row), not a second time
    opacity: enabled || (parent && !parent.enabled) ? 1 : 0.5

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 6
        radius: 3
        color: Qt.rgba(1, 1, 1, 0.14)

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 3
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.brandFrom }
                GradientStop { position: 1.0; color: Theme.brandTo }
            }
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * control.availableWidth - width / 2
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 18
        height: 18
        radius: 9
        color: "#ffffff"
        border.color: Theme.brandFrom
        border.width: control.pressed ? 5 : 3

        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.focus
            visible: control.visualFocus
        }
    }

    Text {
        visible: control.valueText.length > 0
        anchors {
            right: parent.right
            verticalCenter: parent.verticalCenter
        }
        width: control.valueWidth
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideLeft
        text: control.valueText
        color: Theme.text
        font.pixelSize: 13
        font.weight: Font.ExtraBold
    }
}
