import QtQuick
import QtQuick.Shapes

// The GamerNova backdrop: deep space, lighter in the middle.
Item {
    id: backdrop

    Shape {
        anchors.fill: parent

        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: backdrop.width / 2
                centerY: backdrop.height * 0.45
                focalX: centerX
                focalY: centerY
                centerRadius: Math.max(backdrop.width, backdrop.height) * 0.7
                GradientStop { position: 0.0; color: "#1e143c" }
                GradientStop { position: 1.0; color: "#050514" }
            }
            startX: 0
            startY: 0
            PathLine { x: backdrop.width; y: 0 }
            PathLine { x: backdrop.width; y: backdrop.height }
            PathLine { x: 0; y: backdrop.height }
            PathLine { x: 0; y: 0 }
        }
    }
}
