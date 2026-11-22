import QtQuick
import "../../gui/src/qml/intro" as I

// The intro film on its backdrop, scaled to the picture: what the app's player shows, without
// its buttons. For filmshot, which sets `t`.
Item {
    id: root
    property real t: 0
    /** "1": the sky stands still, so that a looping picture (the README's) stays small and clean. */
    property string still: ""

    I.IntroBackdrop {
        anchors.fill: parent
        t: root.still === "1" ? 7 : root.t
        glow: 0.6
    }

    I.IntroFilm {
        t: root.t
        scale: Math.min(root.width / 1920, root.height / 1080)
        transformOrigin: Item.TopLeft
        x: (root.width - 1920 * scale) / 2
        y: (root.height - 1080 * scale) / 2
    }
}
