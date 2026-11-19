import QtQuick
import QtQuick.Effects

// Text filled with a colour gradient, as used for headings on GamerNova.
Item {
    id: control
    property alias text: label.text
    property alias font: label.font
    property color from: "#c084fc"
    property color to: "#f0469b"

    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight

    Text {
        id: label
        anchors.fill: parent
        color: "white"
        visible: false
        layer.enabled: true
    }

    Rectangle {
        id: fill
        anchors.fill: parent
        visible: false
        layer.enabled: true
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: control.from }
            GradientStop { position: 1.0; color: control.to }
        }
    }

    MultiEffect {
        anchors.fill: parent
        source: fill
        maskEnabled: true
        maskSource: label
    }
}
