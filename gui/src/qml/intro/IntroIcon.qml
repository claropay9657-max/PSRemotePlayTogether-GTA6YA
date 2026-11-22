import QtQuick
import QtQuick.Shapes

// The app's icon (res/ui/app-icon.svg) in parts, with the very shapes and colours of the
// drawing, so that the film can put it together: two rings that link, the controller under
// them, the plate behind. With every part at 1 it is the icon.
Item {
    id: icon

    /** Side length in pixels; the drawing is 896 units. */
    property real size: 300
    /** 0 to 1 each: the plate behind, the controller, the bridge where the rings link. */
    property real plate: 1
    property real pad: 1
    property real link: 1
    /** How far each ring is from its place, in units of the drawing (the left one to the left). */
    property real ringsApart: 0
    property real ringsOpacity: 1
    /** The controller comes up from below by this many units. */
    property real padDrop: 0

    width: size
    height: size

    Item {
        id: drawing
        width: 896
        height: 896
        scale: icon.size / 896
        transformOrigin: Item.TopLeft

        // The plate: space, lighter where the rings are
        Item {
            anchors.fill: parent
            opacity: icon.plate
            scale: 0.9 + 0.1 * icon.plate

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeWidth: 0
                    strokeColor: "transparent"
                    fillGradient: RadialGradient {
                        centerX: 448
                        centerY: 296
                        focalX: 448
                        focalY: 296
                        centerRadius: 720
                        GradientStop { position: 0.0; color: "#2b1a66" }
                        GradientStop { position: 0.55; color: "#120c2c" }
                        GradientStop { position: 1.0; color: "#06060e" }
                    }
                    PathRectangle {
                        width: 896
                        height: 896
                        radius: 204
                    }
                }

                ShapePath {
                    strokeWidth: 4
                    fillColor: "transparent"
                    strokeColor: Qt.rgba(1, 1, 1, 0.13)
                    PathRectangle {
                        x: 2
                        y: 2
                        width: 892
                        height: 892
                        radius: 202
                    }
                }
            }

            // A few far-away stars
            Repeater {
                model: [[132, 172, 5, 0.55], [764, 236, 4, 0.45], [696, 112, 6, 0.6], [208, 308, 3, 0.4],
                        [116, 696, 4, 0.35], [788, 652, 5, 0.4], [546, 76, 3, 0.4]]

                Rectangle {
                    required property var modelData
                    x: modelData[0] - modelData[2]
                    y: modelData[1] - modelData[2]
                    width: modelData[2] * 2
                    height: width
                    radius: width / 2
                    color: "white"
                    opacity: modelData[3]
                }
            }
        }

        // One controller
        Item {
            width: 896
            height: 896
            y: icon.padDrop
            opacity: icon.pad

            Shape {
                x: -64
                y: -64
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeWidth: 0
                    strokeColor: "transparent"
                    fillGradient: LinearGradient {
                        x1: 250
                        y1: 0
                        x2: 774
                        y2: 0
                        GradientStop { position: 0.0; color: "#8c52ff" }
                        GradientStop { position: 0.5; color: "#c24dd6" }
                        GradientStop { position: 1.0; color: "#00f2ff" }
                    }
                    PathSvg {
                        path: "M 300 520 C 300 474 338 448 400 448 L 624 448 C 686 448 724 474 724 520 L 772 724 C 786 786 752 828 706 828 C 668 828 648 806 626 770 L 600 728 L 424 728 L 398 770 C 376 806 356 828 318 828 C 272 828 238 786 252 724 Z"
                    }
                }
            }

            // Direction pad, buttons, sticks and the light bar
            Rectangle { x: 272; y: 492; width: 104; height: 34; radius: 10; color: "#0b0a1c" }
            Rectangle { x: 307; y: 457; width: 34; height: 104; radius: 10; color: "#0b0a1c" }
            Rectangle { x: 521; y: 511; width: 50; height: 50; radius: 25; color: "#0b0a1c" }
            Rectangle { x: 579; y: 459; width: 50; height: 50; radius: 25; color: "#0b0a1c" }
            Rectangle { x: 352; y: 568; width: 72; height: 72; radius: 36; color: "#0b0a1c" }
            Rectangle { x: 472; y: 568; width: 72; height: 72; radius: 36; color: "#0b0a1c" }
            Rectangle { x: 373; y: 589; width: 30; height: 30; radius: 15; color: "#8c52ff" }
            Rectangle { x: 493; y: 589; width: 30; height: 30; radius: 15; color: "#00f2ff" }
            Rectangle { x: 406; y: 428; width: 84; height: 16; radius: 8; color: "white"; opacity: 0.9 }
        }

        // Two players, linked: the left ring passes over the right one at the bottom
        Shape {
            x: -64 - icon.ringsApart
            y: -64
            opacity: icon.ringsOpacity
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: "#8c52ff"
                strokeWidth: 34
                fillColor: "transparent"
                PathAngleArc { centerX: 452; centerY: 286; radiusX: 82; radiusY: 82; startAngle: 0; sweepAngle: 360 }
            }
        }

        Shape {
            x: -64 + icon.ringsApart
            y: -64
            opacity: icon.ringsOpacity
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: "#00f2ff"
                strokeWidth: 34
                fillColor: "transparent"
                PathAngleArc { centerX: 572; centerY: 286; radiusX: 82; radiusY: 82; startAngle: 0; sweepAngle: 360 }
            }
        }

        Shape {
            x: -64
            y: -64
            opacity: icon.link * icon.ringsOpacity
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: "#8c52ff"
                strokeWidth: 34
                fillColor: "transparent"
                capStyle: ShapePath.FlatCap
                PathSvg { path: "M 512 342.1 A 82 82 0 0 1 452 368" }
            }
        }
    }
}
