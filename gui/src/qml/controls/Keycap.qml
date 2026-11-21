import QtQuick

// A key or a controller button, drawn as a key: "L1", "⌘O", "Space".
Rectangle {
    id: cap
    property alias text: label.text
    property bool small: false

    implicitWidth: Math.max(implicitHeight, label.implicitWidth + (small ? 12 : 16))
    implicitHeight: small ? 22 : 27
    radius: 7
    color: "#39325e"

    Rectangle {
        anchors {
            fill: parent
            leftMargin: 1
            rightMargin: 1
            topMargin: 1
            bottomMargin: 3
        }
        radius: 6
        color: "#221d3e"

        Text {
            id: label
            anchors.centerIn: parent
            color: Theme.text
            font.pixelSize: cap.small ? 11 : 12
            font.weight: Font.ExtraBold
        }
    }
}
