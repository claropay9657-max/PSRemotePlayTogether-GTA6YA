import QtQuick
import QtQuick.Layouts

// One page of the settings window (SettingsView.qml): its heading, a line about it,
// then its groups (SettingsGroup) one under the other. The window gives it its width
// and scrolls it.
ColumnLayout {
    id: page
    property string title
    property string description
    default property alias content: body.data

    spacing: 16

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4

        Text {
            Layout.fillWidth: true
            text: page.title
            wrapMode: Text.WordWrap
            color: Theme.text
            font.pixelSize: 24
            font.weight: Font.ExtraBold
        }

        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.description
            wrapMode: Text.WordWrap
            lineHeight: 1.15
            color: Theme.textSoft
            font.pixelSize: 14
        }
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: 18
    }
}
