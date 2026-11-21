import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

// A setting that is on or off, as a row: an icon, what it is, a line about it, and
// the switch. A click anywhere on the row switches it.
Rectangle {
    id: row
    property string iconName
    property string title
    property string subtitle
    property alias checked: toggle.checked
    property alias toggle: toggle
    property bool firstInFocusChain: false
    property bool lastInFocusChain: false
    signal toggled()

    Layout.fillWidth: true
    implicitHeight: Math.max(56, texts.implicitHeight + 20)
    radius: 14
    color: Qt.rgba(1, 1, 1, mouse.containsMouse ? 0.05 : 0.03)
    border.width: 1
    border.color: Theme.line
    opacity: enabled ? 1 : 0.5

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: {
            toggle.toggle();
            row.toggled();
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 14
        spacing: 12

        Rectangle {
            visible: row.iconName.length > 0
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            radius: 10
            color: Qt.rgba(0.49, 0.53, 1, 0.14)

            Icon {
                anchors.centerIn: parent
                iconName: row.iconName
                size: 18
                color: Theme.headingSoft
            }
        }

        ColumnLayout {
            id: texts
            Layout.fillWidth: true
            spacing: 1

            Text {
                Layout.fillWidth: true
                text: row.title
                elide: Text.ElideRight
                font.pixelSize: 14
                font.weight: Font.Bold
                color: Theme.text
            }

            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: row.subtitle
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: Theme.textDim
            }
        }

        Toggle {
            id: toggle
            Accessible.name: row.title
            firstInFocusChain: row.firstInFocusChain
            lastInFocusChain: row.lastInFocusChain
            onToggled: row.toggled()
        }
    }
}
