import QtQuick
import QtQuick.Layouts

// A group of settings in the older dialogs (the renderer's options, the display,
// controller mapping), in the look of the settings window's SettingsGroup: a small
// capital heading, maybe a line about it, and its rows (SettingRow) on one card.
ColumnLayout {
    id: section
    property string title
    property string description
    default property alias content: rows.data
    readonly property bool isSettingsGroup: true

    Layout.fillWidth: true
    spacing: 8

    SectionLabel {
        visible: text.length > 0
        text: section.title
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: -4
        visible: text.length > 0
        text: section.description
        wrapMode: Text.WordWrap
        color: Theme.textDim
        font.pixelSize: 13
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: rows.implicitHeight + 4
        radius: 18
        color: Theme.card
        border.width: 1
        border.color: Theme.line

        ColumnLayout {
            id: rows
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                topMargin: 2
                leftMargin: 18
                rightMargin: 18
            }
            spacing: 0
        }
    }
}
