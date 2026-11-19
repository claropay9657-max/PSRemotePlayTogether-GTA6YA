import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// A group of settings on a card: a heading, an optional line about it, and its
// rows (SettingRow) one under the other.
Rectangle {
    id: section
    property string title
    property string description
    default property alias content: rows.data

    Layout.fillWidth: true
    implicitHeight: column.implicitHeight + 34
    radius: 16
    color: Qt.rgba(1, 1, 1, 0.035)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.07)

    ColumnLayout {
        id: column
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
            topMargin: 16
            leftMargin: 22
            rightMargin: 22
        }
        spacing: 3

        Label {
            visible: text.length > 0
            text: section.title
            font.pixelSize: 12
            font.bold: true
            font.letterSpacing: 1.3
            font.capitalization: Font.AllUppercase
            color: "#b18cff"
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: section.description
            wrapMode: Text.WordWrap
            opacity: 0.55
            font.pixelSize: 13
        }

        ColumnLayout {
            id: rows
            Layout.fillWidth: true
            Layout.topMargin: 6
            spacing: 0
        }
    }
}
