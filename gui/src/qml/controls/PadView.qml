import QtQuick

// A DualSense that shows what is being pressed, for the display on top of the
// game picture. Drawn with the sprites of CryptoPad's PS5 skins (the same ones
// the OBS overlay uses); the positions are those of its ps5-controller.css.
Item {
    id: padView

    /** { b: buttons, l2, r2, lx, ly, rx, ry } as the session reports it, or null. */
    property var pad: null
    /** Folder name of a PS5 skin, e.g. "PS5", "PS5 (black)", "PS5 (cosmic red)". */
    property string skin: "PS5"
    /** Size relative to the sprites' own 807 x 651. */
    property real factor: 0.22

    readonly property string dir: "qrc:/overlay/cryptopad/static/images/" + skin + "/"
    // Only the white, the black and the red one bring every picture themselves. The other
    // colours have a body and face buttons of their own and borrow the rest from the white one.
    readonly property string whiteDir: "qrc:/overlay/cryptopad/static/images/PS5/"
    readonly property bool complete: skin === "PS5" || skin === "PS5 (black)" || skin === "PS5 (cosmic red)"
    readonly property int buttons: pad ? (pad.b || 0) : 0

    // Bits of the controller state, same as everywhere else in the app
    readonly property int bitCross: 1 << 0
    readonly property int bitCircle: 1 << 1
    readonly property int bitSquare: 1 << 2
    readonly property int bitTriangle: 1 << 3
    readonly property int bitLeft: 1 << 4
    readonly property int bitRight: 1 << 5
    readonly property int bitUp: 1 << 6
    readonly property int bitDown: 1 << 7
    readonly property int bitL1: 1 << 8
    readonly property int bitR1: 1 << 9
    readonly property int bitL3: 1 << 10
    readonly property int bitR3: 1 << 11
    readonly property int bitOptions: 1 << 12
    readonly property int bitShare: 1 << 13
    readonly property int bitTouchpad: 1 << 14
    readonly property int bitPs: 1 << 15

    function down(bit) {
        return (buttons & bit) !== 0;
    }

    function axis(value) {
        return Math.max(-1, Math.min(1, (value || 0) / 32767));
    }

    width: Math.round(807 * factor)
    height: Math.round(651 * factor)

    component Sprite: Image {
        property string file
        source: (padView.complete || file === "base.png" || file === "button.png" ? padView.dir : padView.whiteDir) + file
        smooth: true
        mipmap: true
    }

    component FaceButton: Sprite {
        property int column
        property bool pressed
        file: "button.png"
        width: 58
        height: 58
        sourceClipRect: Qt.rect(column * 59, pressed ? 59 : 0, 58, 58)
    }

    component Stick: Sprite {
        property real valueX
        property real valueY
        property bool pressed
        readonly property bool moved: Math.sqrt(valueX * valueX + valueY * valueY) >= 0.12
        file: "stick.png"
        width: 100
        height: 100
        sourceClipRect: Qt.rect(pressed ? 102 : 0, 0, 100, 100)
        transform: Translate {
            x: moved ? valueX * 22 : 0
            y: moved ? valueY * 22 : 0
        }
    }

    Item {
        width: 807
        height: 651
        scale: padView.factor
        transformOrigin: Item.TopLeft

        Sprite {
            file: "base.png"
            width: 807
            height: 651
        }

        // Triggers fade in with how far they are pulled
        Sprite {
            x: 94
            file: "trigger.png"
            width: 111
            height: 108
            sourceClipRect: Qt.rect(0, 0, 111, 108)
            opacity: padView.pad ? (padView.pad.l2 || 0) / 255 : 0
        }

        Sprite {
            x: 94 + 619 - 111
            file: "trigger.png"
            width: 111
            height: 108
            sourceClipRect: Qt.rect(113, 0, 111, 108)
            opacity: padView.pad ? (padView.pad.r2 || 0) / 255 : 0
        }

        Sprite {
            x: 93
            y: 114
            file: "bumper.png"
            visible: padView.down(padView.bitL1)
        }

        Sprite {
            x: 93 + 620 - 110
            y: 114
            file: "bumper.png"
            mirror: true
            visible: padView.down(padView.bitR1)
        }

        Sprite {
            x: 237
            y: 118
            file: "touchpad.png"
            visible: padView.down(padView.bitTouchpad)
        }

        Sprite {
            x: 378
            y: 356
            file: "meta.png"
            visible: padView.down(padView.bitPs)
        }

        // Create and Options
        Sprite {
            x: 195
            y: 140
            file: "back_start.png"
            width: 27
            height: 57
            sourceClipRect: Qt.rect(0, 0, 27, 57)
            visible: padView.down(padView.bitShare)
        }

        Sprite {
            x: 195 + 416 - 27
            y: 140
            file: "back_start.png"
            width: 27
            height: 57
            sourceClipRect: Qt.rect(28, 0, 27, 57)
            visible: padView.down(padView.bitOptions)
        }

        // Cross, Circle, Square, Triangle
        Item {
            x: 573
            y: 178

            FaceButton { x: 61; y: 123; column: 0; pressed: padView.down(padView.bitCross) }
            FaceButton { x: 123; y: 62; column: 1; pressed: padView.down(padView.bitCircle) }
            FaceButton { x: 0; y: 61; column: 2; pressed: padView.down(padView.bitSquare) }
            FaceButton { x: 61; y: 0; column: 3; pressed: padView.down(padView.bitTriangle) }
        }

        // Directional pad: only the pressed directions are drawn
        Item {
            x: 71
            y: 196

            Sprite {
                x: 46
                file: "face.png"
                width: 52
                height: 63
                sourceClipRect: Qt.rect(0, 68, 52, 63)
                visible: padView.down(padView.bitUp)
            }

            Sprite {
                x: 46
                y: 81
                file: "face.png"
                width: 52
                height: 63
                sourceClipRect: Qt.rect(54, 68, 52, 63)
                visible: padView.down(padView.bitDown)
            }

            Sprite {
                x: -1
                y: 47
                file: "face.png"
                width: 64
                height: 52
                sourceClipRect: Qt.rect(108, 68, 64, 52)
                visible: padView.down(padView.bitLeft)
            }

            Sprite {
                x: 81
                y: 46
                file: "face.png"
                width: 63
                height: 52
                sourceClipRect: Qt.rect(175, 68, 63, 52)
                visible: padView.down(padView.bitRight)
            }
        }

        Item {
            x: 220
            y: 333

            Stick {
                valueX: padView.pad ? padView.axis(padView.pad.lx) : 0
                valueY: padView.pad ? padView.axis(padView.pad.ly) : 0
                pressed: padView.down(padView.bitL3)
            }

            Stick {
                x: 267
                valueX: padView.pad ? padView.axis(padView.pad.rx) : 0
                valueY: padView.pad ? padView.axis(padView.pad.ry) : 0
                pressed: padView.down(padView.bitR3)
            }
        }
    }
}
