import QtQuick
import "../../gui/src/qml/intro" as I
import "../../gui/src/qml/intro/Film.js" as F

// The intro film on its backdrop, scaled to the picture: what the app's player shows, without
// its buttons. For filmshot, which sets `t`, and whatever else its command line names (as
// text: "1" is yes).
Item {
    id: root
    property real t: 0
    /** "1": the sky stands still, so that a looping picture (the README's) stays small and clean. */
    property string still: ""
    /** "1": the film upright, 1080 x 1920, for a picture higher than wide: a phone's. */
    property string tall: ""
    /**
     * "1": a video about the app, to post somewhere. Nothing in it can be pressed, the end says
     * where the app is to be had, and the upright one opens with a line of its own: there the
     * first second decides whether anybody watches the next.
     */
    property string promo: ""

    readonly property bool upright: tall === "1"
    readonly property bool video: promo === "1"

    FontLoader {
        id: jakarta
        source: "qrc:/fonts/PlusJakartaSans.ttf"
    }

    I.IntroBackdrop {
        anchors.fill: parent
        t: root.still === "1" ? 7 : root.t
        glow: 0.6
    }

    // The film's stage, and what a video adds to it in the stage's own measures
    Item {
        id: stage
        width: film.width
        height: film.height
        scale: Math.min(root.width / width, root.height / height)
        transformOrigin: Item.TopLeft
        x: (root.width - width * scale) / 2
        y: (root.height - height * scale) / 2

        I.IntroFilm {
            id: film
            t: root.t
            tall: root.upright
            promo: root.video
        }

        // The line the upright video opens with: there from its first picture, gone when the name comes
        Item {
            id: hook
            readonly property real gone: 1 - F.leave(root.t, 2.15, 0.4)
            readonly property real size: 88
            readonly property string second: "without the couch."
            visible: root.video && root.upright && root.t < 2.6
            width: stage.width
            y: 262 - gone * 26
            opacity: 1 - gone

            Text {
                x: stage.width / 2 - width / 2
                y: (1 - F.enter(root.t, -0.25, 0.7)) * 30
                text: "Couch co-op,"
                color: "white"
                font.family: jakarta.font.family
                font.pixelSize: hook.size
                font.weight: Font.ExtraBold
            }

            Row {
                x: stage.width / 2 - width / 2
                y: hook.size * 1.16 + (1 - F.enter(root.t, -0.1, 0.7)) * 30

                Repeater {
                    model: hook.second.length

                    Text {
                        required property int index
                        text: hook.second.charAt(index)
                        color: F.lerpColor(Qt.rgba(167 / 255, 123 / 255, 1, 1), Qt.rgba(240 / 255, 70 / 255, 155 / 255, 1), index / (hook.second.length - 1))
                        font.family: jakarta.font.family
                        font.pixelSize: hook.size
                        font.weight: Font.ExtraBold
                    }
                }
            }
        }

        // Where the app is to be had: under the doors
        Column {
            id: where
            readonly property real at: film.tEnd + 2.1
            readonly property real k: F.enter(root.t, at, 0.7)
            visible: root.video && root.t > at
            x: stage.width / 2 - width / 2
            y: (root.upright ? 1318 : 836) + (1 - k) * 20
            opacity: k
            spacing: root.upright ? 8 : 6

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.upright ? "Free and open source" : "Free and open source  ·  Windows, macOS, Linux"
                color: "white"
                font.family: jakarta.font.family
                font.pixelSize: root.upright ? 44 : 34
                font.weight: Font.ExtraBold
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.upright
                text: "for Windows, macOS and Linux"
                color: "#b9b4d6"
                font.family: jakarta.font.family
                font.pixelSize: 32
                font.weight: Font.DemiBold
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "github.com/Crypto90/PSRemotePlayTogether"
                color: "#cdb6ff"
                font.family: jakarta.font.family
                font.pixelSize: root.upright ? 32 : 30
                font.weight: Font.Bold
            }
        }
    }
}
