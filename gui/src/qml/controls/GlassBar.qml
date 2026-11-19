import QtQuick

// Background for tool bars: dark glass with a thin glowing line underneath.
Rectangle {
    color: Qt.rgba(15 / 255, 10 / 255, 30 / 255, 0.72)

    Rectangle {
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: 1
        opacity: 0.55
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.25; color: "#8c52ff" }
            GradientStop { position: 0.75; color: "#00f2ff" }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
}
