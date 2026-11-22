import QtQuick
import QtQuick.Shapes

import "Film.js" as F

// What the intro film plays in front of: the app's deep space with slow lights and far stars.
// Fills whatever size it is given (the film's stage is 16:9, the window may not be), and like
// everything in the film it is drawn from the film's time alone.
Item {
    id: backdrop

    /** The film's time in seconds. */
    property real t: 0
    /** 0 to 1: how much the lights glow; the title and the end glow more. */
    property real glow: 0.5
    /** Where the lights gather, 0 to 1 across and down: the film moves it with what it shows. */
    property real focusX: 0.5
    property real focusY: 0.42

    readonly property real unit: Math.max(width, height)

    Rectangle {
        anchors.fill: parent
        color: "#06050f"
    }

    component Light: Shape {
        id: light
        property color tint: "white"
        property real cx: 0
        property real cy: 0
        property real radius: 100
        anchors.fill: parent

        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: light.cx
                centerY: light.cy
                focalX: light.cx
                focalY: light.cy
                centerRadius: light.radius
                GradientStop { position: 0.0; color: light.tint }
                GradientStop { position: 0.45; color: Qt.rgba(light.tint.r, light.tint.g, light.tint.b, light.tint.a * 0.42) }
                GradientStop { position: 1.0; color: Qt.rgba(light.tint.r, light.tint.g, light.tint.b, 0) }
            }
            PathRectangle {
                width: backdrop.width
                height: backdrop.height
            }
        }
    }

    // The middle of space, where the film looks
    Light {
        tint: Qt.rgba(0.17, 0.10, 0.40, 0.55 + 0.25 * backdrop.glow)
        cx: backdrop.width * backdrop.focusX
        cy: backdrop.height * backdrop.focusY
        radius: backdrop.unit * 0.62
    }

    // Three slow lights in the app's colours, each on a path of its own
    Light {
        tint: Qt.rgba(140 / 255, 82 / 255, 1, 0.16 + 0.12 * backdrop.glow)
        cx: backdrop.width * (0.16 + 0.07 * Math.sin(backdrop.t * 0.11 + 0.4))
        cy: backdrop.height * (0.14 + 0.09 * Math.cos(backdrop.t * 0.09))
        radius: backdrop.unit * 0.46
    }

    Light {
        tint: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.09 + 0.09 * backdrop.glow)
        cx: backdrop.width * (0.88 + 0.06 * Math.cos(backdrop.t * 0.08 + 1.3))
        cy: backdrop.height * (0.92 + 0.08 * Math.sin(backdrop.t * 0.12 + 2.1))
        radius: backdrop.unit * 0.44
    }

    Light {
        tint: Qt.rgba(0, 242 / 255, 1, 0.035 + 0.05 * backdrop.glow)
        cx: backdrop.width * (0.78 + 0.10 * Math.sin(backdrop.t * 0.07 + 3.0))
        cy: backdrop.height * (0.22 + 0.10 * Math.cos(backdrop.t * 0.10 + 0.9))
        radius: backdrop.unit * 0.30
    }

    // Far stars: each twinkles at its own pace and drifts a little, the near ones faster
    Repeater {
        model: 110

        Rectangle {
            required property int index
            readonly property real depth: 0.25 + 0.75 * F.hash(index * 3 + 1)
            readonly property real size: 1.2 + 2.4 * depth * F.hash(index * 3 + 2)
            readonly property real along: F.hash(index * 7 + 5) + backdrop.t * 0.0035 * depth
            x: (1 - (along - Math.floor(along))) * (backdrop.width + 20) - 10
            y: F.hash(index * 11 + 3) * backdrop.height
            width: size
            height: size
            radius: size / 2
            color: index % 9 === 0 ? "#b9f6ff" : index % 7 === 0 ? "#d9c8ff" : "white"
            opacity: (0.16 + 0.5 * depth) * (0.62 + 0.38 * Math.sin(backdrop.t * (0.6 + 1.7 * F.hash(index * 13)) + index))
        }
    }

    // Two wide orbits far out, turning slowly: the rings of the app's icon, as large as the sky
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        opacity: 0.5 + 0.5 * backdrop.glow

        ShapePath {
            strokeColor: Qt.rgba(140 / 255, 82 / 255, 1, 0.10)
            strokeWidth: 1.5
            fillColor: "transparent"
            PathAngleArc {
                centerX: backdrop.width * 0.5
                centerY: backdrop.height * 1.32
                radiusX: backdrop.unit * 0.74
                radiusY: backdrop.unit * 0.62
                startAngle: 180 + backdrop.t * 0.6
                sweepAngle: 200
            }
        }

        ShapePath {
            strokeColor: Qt.rgba(0, 242 / 255, 1, 0.07)
            strokeWidth: 1.5
            fillColor: "transparent"
            PathAngleArc {
                centerX: backdrop.width * 0.5
                centerY: backdrop.height * 1.36
                radiusX: backdrop.unit * 0.92
                radiusY: backdrop.unit * 0.78
                startAngle: 200 - backdrop.t * 0.45
                sweepAngle: 160
            }
        }
    }

    // Darker towards the edges, so the eye stays in the middle
    Shape {
        anchors.fill: parent

        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: backdrop.width / 2
                centerY: backdrop.height / 2
                focalX: centerX
                focalY: centerY
                centerRadius: backdrop.unit * 0.72
                GradientStop { position: 0.55; color: "transparent" }
                GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.55) }
            }
            PathRectangle {
                width: backdrop.width
                height: backdrop.height
            }
        }
    }
}
