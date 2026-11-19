import QtQuick
import QtQuick.Effects

// A player's picture as a circle, or their initial on the GamerNova gradient
// when they have none. An optional ring around it, for "is talking".
Item {
    id: avatar

    property string source: ""
    property string name: ""
    property color ringColor: "transparent"
    property real ringWidth: 0

    implicitWidth: 40
    implicitHeight: 40

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: avatar.ringWidth
        border.color: avatar.ringColor
        visible: avatar.ringWidth > 0
    }

    Item {
        id: face
        anchors.fill: parent
        anchors.margins: avatar.ringWidth > 0 ? avatar.ringWidth + 1 : 0

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            visible: picture.status !== Image.Ready
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#8c52ff" }
                GradientStop { position: 1.0; color: "#f0469b" }
            }

            Text {
                anchors.centerIn: parent
                color: "white"
                font.pixelSize: Math.round(parent.height * 0.46)
                font.bold: true
                text: (avatar.name || "?").trim().charAt(0).toUpperCase()
            }
        }

        Image {
            id: picture
            anchors.fill: parent
            source: avatar.source
            fillMode: Image.PreserveAspectCrop
            sourceSize.width: 160
            sourceSize.height: 160
            visible: false
            layer.enabled: true
        }

        Rectangle {
            id: circle
            anchors.fill: parent
            radius: width / 2
            visible: false
            layer.enabled: true
        }

        MultiEffect {
            anchors.fill: parent
            source: picture
            maskEnabled: true
            maskSource: circle
            visible: picture.status === Image.Ready
        }
    }
}
