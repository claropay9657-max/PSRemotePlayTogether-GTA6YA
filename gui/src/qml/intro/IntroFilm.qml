import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

import "../controls" as C
import "Film.js" as F

// The intro film: what the app is for and how it works, in under a minute. A stage that
// whoever shows it scales to fit (IntroPlayer.qml): 1920 x 1080, or 1080 x 1920 where it
// stands upright (`tall`: a window higher than wide, a video for a phone). Both show the same
// film at the same times; only where things are differs, and every place says both. Nothing
// here runs by itself: everything is drawn from the time `t`, so the film can be paused,
// jumped through and looked at one moment at a time.
Item {
    id: film

    /** Where the film is, in seconds. */
    property real t: 0
    /**
     * The stage stands upright, 1080 x 1920: the name and what is said at the top, and under
     * them (from 630 down) what the chapter shows. The apps that play upright videos lay their
     * own buttons over them, so nothing that matters is higher than 250 or lower than 1500,
     * and in the lower half (from 1000 down) nothing is further right than 910, where their
     * column of buttons stands. A game picture may reach under that column; words may not.
     */
    property bool tall: false
    /** A video about the app, not the app: the end has nothing to press, so it does not offer to play again. */
    property bool promo: false
    readonly property real midX: width / 2
    /** Where the last chapter has arrived and the film rests. */
    readonly property real duration: chapters[chapters.length - 1].start + 2.6

    /** The chapters, for whoever shows the film: where each starts and what it is called. */
    readonly property var chapters: [
        { start: 0.0, title: qsTr("Welcome") },
        { start: tPlay, title: qsTr("Play together") },
        { start: tShare, title: qsTr("What you share") },
        { start: tInvite, title: qsTr("Invite friends") },
        { start: tModes, title: qsTr("Who plays") },
        { start: tCouch, title: qsTr("Talk and see") },
        { start: tStream, title: qsTr("Streaming") },
        { start: tEnd, title: qsTr("Get started") }
    ]

    // When each part starts. The film runs to its music (scripts/sounds/make_intro_sound.py,
    // which reads these numbers): 100 beats a minute, a beat every 0.6 seconds, four to a bar.
    // The title takes two bars and every other part three, and what happens in them falls on
    // the beats or halfway between two.
    readonly property real beat: 0.6
    readonly property real tPlay: 4.8
    readonly property real tShare: 12.0
    readonly property real tInvite: 19.2
    readonly property real tModes: 26.4
    readonly property real tCouch: 33.6
    readonly property real tStream: 40.8
    readonly property real tEnd: 48.0
    // What you share: each place has a bar
    readonly property real tSource2: tShare + 2.4
    readonly property real tSource3: tShare + 4.8

    width: tall ? 1080 : 1920
    height: tall ? 1920 : 1080

    FontLoader {
        id: jakarta
        source: "qrc:/fonts/PlusJakartaSans.ttf"
    }
    readonly property string family: jakarta.status === FontLoader.Ready ? jakarta.font.family : ""

    readonly property color ink: "#f3f1ff"
    readonly property color soft: "#b9b4d6"
    readonly property color dim: "#8f8aad"
    readonly property color purple: "#8c52ff"
    readonly property color pink: "#f0469b"
    readonly property color cyan: "#00f2ff"
    readonly property color green: "#34d399"
    readonly property color amber: "#f5b942"

    /** Who is in the film: the host and three friends, each with a colour and a controller of their own. */
    readonly property var cast: [
        { name: qsTr("You"), initial: "Y", where: qsTr("Host"), skin: "PS5 (galactic purple)", from: "#8c52ff", to: "#f0469b" },
        { name: "Mika", initial: "M", where: qsTr("Lisbon"), skin: "PS5 (starlight blue)", from: "#38bdf8", to: "#4f6bff" },
        { name: "Leo", initial: "L", where: qsTr("Toronto"), skin: "PS5 (chroma teal)", from: "#34d399", to: "#0e9aa7" },
        { name: "Ava", initial: "A", where: qsTr("Tokyo"), skin: "PS5 (nova pink)", from: "#ff7ac3", to: "#f5873d" }
    ]
    readonly property color cYou: "#a77bff"
    readonly property color cMika: "#4fd0ff"
    readonly property color cLeo: "#3ddba4"
    readonly property color cAva: "#ff7ac3"
    readonly property var castColors: [cYou, cMika, cLeo, cAva]

    /**
     * Every ring the explorer catches: when (on a beat of the music, where it sounds), and who
     * pressed for it. The jumps, the controllers and the lights follow from this.
     */
    readonly property var catches: [
        // One game, everybody plays: first you, then each friend, then one after the other
        { at: 6.6, by: 0 }, { at: 8.4, by: 1 }, { at: 9.0, by: 2 }, { at: 9.6, by: 3 },
        { at: 10.2, by: 0 }, { at: 10.8, by: 1 }, { at: 11.4, by: 2 },
        // What you share: you play on
        { at: 13.2, by: 0 }, { at: 15.6, by: 0 }, { at: 18.0, by: 0 },
        // Invite: alone until they are in
        { at: 19.8, by: 0 }, { at: 23.4, by: 1 }, { at: 24.0, by: 2 }, { at: 24.6, by: 3 }, { at: 25.2, by: 0 },
        // Talk and see
        { at: 34.8, by: 0 }, { at: 36.0, by: 1 }, { at: 37.2, by: 2 }, { at: 38.4, by: 3 }, { at: 39.6, by: 0 },
        // Streaming: faster towards the end
        { at: 42.6, by: 0 }, { at: 43.2, by: 1 }, { at: 44.4, by: 2 }, { at: 45.0, by: 3 },
        { at: 45.6, by: 0 }, { at: 46.2, by: 1 }, { at: 46.8, by: 2 }, { at: 47.4, by: 3 }
    ]
    /** A jump starts this long before its ring is caught: half a jump (IntroGame.jumpTime). */
    readonly property real catchLead: 0.28
    readonly property var jumps: catches.map((c) => ({ t: c.at - catchLead, by: c.by }))

    /** Talk and see: from when to when each of the four talks. */
    readonly property var talks: [[tCouch + 4.2, tCouch + 5.1], [tCouch + 1.2, tCouch + 2.5], [tCouch + 5.4, tCouch + 6.5], [tCouch + 2.8, tCouch + 3.9]]

    /** How long before the jump the button goes down, and how long the light takes to the screen. */
    readonly property real pressLead: 0.36
    readonly property real lightTime: 0.30

    /** The jump `who` is pressing for around now (its time), or -99. */
    function pressOf(who) {
        for (let i = 0; i < jumps.length; ++i) {
            const j = jumps[i];
            if (j.t - pressLead > t)
                break;
            if (j.by === who && t < j.t + 0.6)
                return j.t;
        }
        return -99;
    }

    /** What `who`'s controller is doing, as the app's own controller picture wants it. */
    function padOf(who) {
        const since = t - (pressOf(who) - pressLead);
        return {
            b: since >= 0 && since < 0.2 ? 1 : 0,
            l2: 0,
            r2: 0,
            // Everybody leans the stick the way the explorer runs
            lx: 21000 + 7000 * Math.sin(t * 1.7 + who * 2.1),
            ly: 5000 * Math.sin(t * 1.1 + who),
            rx: 0,
            ry: 0
        };
    }

    // ------------------------------------------------------------ building blocks

    component T: Text {
        color: film.ink
        font.family: film.family
        font.weight: Font.Bold
        textFormat: Text.PlainText
    }

    /** A soft round light. */
    component Halo: Shape {
        id: halo
        property color tint: "white"
        property real radius: width / 2

        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: halo.width / 2
                centerY: halo.height / 2
                focalX: centerX
                focalY: centerY
                centerRadius: halo.radius
                GradientStop { position: 0.0; color: halo.tint }
                GradientStop { position: 1.0; color: Qt.rgba(halo.tint.r, halo.tint.g, halo.tint.b, 0) }
            }
            PathRectangle {
                width: halo.width
                height: halo.height
            }
        }
    }

    component Round: Rectangle {
        property real size: 10
        width: size
        height: size
        radius: size / 2
    }

    /** What a chapter says: a heading (its last part in the brand's colours) and a line under it. */
    component Caption: Item {
        id: caption
        property string head
        property string accent
        property string line
        property real from: 0
        property real to: 1
        property real headY: 782
        readonly property real k: F.enter(film.t, from, 0.6)
        readonly property real k2: F.enter(film.t, from + 0.18, 0.6)
        readonly property real gone: 1 - F.leave(film.t, to - 0.42, 0.42)

        // Wide: below the picture, the heading in one line. Tall: at the top, larger, the
        // heading's two parts one under the other
        readonly property real size: film.tall ? 74 : 66
        readonly property real headTop: (film.tall ? 322 : headY) + (1 - k) * 30 - gone * 16
        readonly property real lineTop: (film.tall ? 322 + 2 * size * 1.135 + 14 : headY + 96) + (1 - k2) * 22 - gone * 10
        /** Between the heading's two parts where they stand in one line. */
        readonly property real gap: head.length > 0 ? 18 : 0

        visible: film.t > from && film.t < to
        width: film.width

        T {
            id: headText
            x: film.tall ? film.midX - width / 2 : film.midX - (width + caption.gap + accentRow.width) / 2
            y: caption.headTop
            text: caption.head
            font.pixelSize: caption.size
            font.weight: Font.ExtraBold
            opacity: caption.k * (1 - caption.gone)
        }

        Row {
            id: accentRow
            x: film.tall ? film.midX - width / 2 : headText.x + headText.width + caption.gap
            y: caption.headTop + (film.tall ? caption.size * 1.135 : 0)
            opacity: (film.tall ? caption.k2 : caption.k) * (1 - caption.gone)

            Repeater {
                model: caption.accent.length

                T {
                    required property int index
                    text: caption.accent.charAt(index)
                    font.pixelSize: caption.size
                    font.weight: Font.ExtraBold
                    color: F.lerpColor(Qt.rgba(167 / 255, 123 / 255, 1, 1), film.pink, caption.accent.length > 1 ? index / (caption.accent.length - 1) : 0)
                }
            }
        }

        // Tall, a line too long for the stage breaks where its sentence does, not where it runs out
        TextMetrics {
            id: lineSize
            font: lineText.font
            text: caption.line
        }

        T {
            id: lineText
            x: film.midX - width / 2
            y: caption.lineTop
            width: Math.min(implicitWidth, film.tall ? 900 : 1280)
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            lineHeight: 1.18
            text: film.tall && lineSize.advanceWidth > 900 ? F.halved(caption.line) : caption.line
            color: film.soft
            font.pixelSize: film.tall ? 37 : 33
            font.weight: Font.Medium
            opacity: caption.k2 * (1 - caption.gone)
        }
    }

    /**
     * The game on a screen: a card of any width, 16:9, showing the one game the film plays
     * (every screen in the film shows the same picture, as every player's does).
     */
    component Screen: Item {
        id: screen
        property real w: 800
        /** How much light it throws around itself, 0 to 1. */
        property real shine: 1
        readonly property real corner: 30 * w / 800
        width: w
        height: w * 9 / 16

        // The light it throws, wider than high
        Halo {
            x: -screen.width * 0.35
            y: screen.height / 2 - width / 2
            width: screen.width * 1.7
            height: width
            tint: Qt.rgba(160 / 255, 80 / 255, 1, 0.26 * screen.shine)
            transform: Scale { origin.y: screen.width * 0.85; yScale: 0.74 }
        }

        // Its shadow, soft and below
        Halo {
            x: -screen.width * 0.06
            y: screen.height - width / 2 + screen.height * 0.05
            width: screen.width * 1.12
            height: width
            tint: Qt.rgba(0, 0, 0, 0.55)
            transform: Scale { origin.y: screen.width * 0.56; yScale: 0.16 }
        }

        Rectangle {
            id: screenMask
            anchors.fill: parent
            radius: screen.corner
            visible: false
            layer.enabled: true
        }

        MultiEffect {
            anchors.fill: parent
            source: gamePicture
            maskEnabled: true
            maskSource: screenMask
        }

        // The glass: an edge, and light from above
        Rectangle {
            anchors.fill: parent
            radius: screen.corner
            color: "transparent"
            border.width: Math.max(1.5, 2.5 * screen.w / 800)
            border.color: Qt.rgba(1, 1, 1, 0.2)
        }
    }

    /** A soft dot with a light around it. */
    component Bead: Item {
        id: bead
        property color tint: "white"
        property real size: 16

        Halo {
            x: -bead.size * 2.6
            y: -bead.size * 2.6
            width: bead.size * 5.2
            height: bead.size * 5.2
            tint: Qt.rgba(bead.tint.r, bead.tint.g, bead.tint.b, 0.75)
        }

        Round {
            size: bead.size
            x: -size / 2
            y: -size / 2
            color: "white"
        }
    }

    /**
     * One of the players, with a controller of their own and a line of light to the screen.
     * When they press (film.jumps), the button goes down and a light runs to the screen.
     */
    component Player: Item {
        id: player
        /** Which of film.cast. */
        property int who: 0
        /** The middle of the controller. */
        property real cx: 0
        property real cy: 0
        /** Where on the screen's edge the light arrives. */
        property real toX: 0
        property real toY: 0
        /** When they arrive and when they leave. */
        property real at: 0
        property real until: 999
        property real padFactor: film.tall ? 0.205 : 0.27
        readonly property var person: film.cast[who]
        readonly property color tint: film.castColors[who]
        readonly property bool fromLeft: cx < toX
        /** Tall: the players sit in a row under the screen, and their lights go up to it. */
        readonly property bool up: film.tall
        readonly property real k: F.pop(film.t, at, 10, 6.5)
        readonly property real there: F.seg(film.t, at, at + 0.3) * F.leave(film.t, until - 0.4, 0.4)
        readonly property real slide: (1 - F.enter(film.t, at, 0.7)) * (fromLeft ? -110 : 110)
            + (1 - F.leave(film.t, until - 0.4, 0.4)) * (fromLeft ? -90 : 90)
        /** They come in from their side, or from below. */
        readonly property real slideX: up ? 0 : slide
        readonly property real slideY: up ? Math.abs(slide) : 0
        /** The jump they are pressing for, and how far its light has run. */
        readonly property real press: film.pressOf(who)
        readonly property real run: F.seg(film.t, press - film.pressLead + 0.04, press - film.pressLead + 0.04 + film.lightTime)
        readonly property real padW: 807 * padFactor
        readonly property real padH: 651 * padFactor
        // The line of light: from the controller's side towards the screen, in a soft bow
        readonly property real fromX: up ? cx : cx + (fromLeft ? padW / 2 + 6 : -padW / 2 - 6)
        readonly property real fromY: up ? cy - padH / 2 - 10 : cy + 6
        readonly property real bowX: up ? fromX : (fromX + toX) / 2
        readonly property real bowY: up ? toY + 64 : Math.min(fromY, toY) - 46

        visible: film.t > at && film.t < until
        opacity: there

        // The line
        Shape {
            preferredRendererType: Shape.CurveRenderer
            opacity: F.seg(film.t, player.at + 0.25, player.at + 0.7)

            ShapePath {
                strokeColor: Qt.rgba(player.tint.r, player.tint.g, player.tint.b, 0.26 + 0.6 * F.decay(film.t, player.press - film.pressLead, 3.5))
                strokeWidth: 3
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: player.fromX + player.slideX
                startY: player.fromY + player.slideY
                PathQuad {
                    x: player.toX
                    y: player.toY
                    controlX: player.bowX + player.slideX / 2
                    controlY: player.bowY
                }
            }
        }

        // Where it reaches the screen
        Halo {
            x: player.toX - 70
            y: player.toY - 70
            width: 140
            height: 140
            tint: Qt.rgba(player.tint.r, player.tint.g, player.tint.b, 0.85 * F.decay(film.t, player.press - film.pressLead + film.lightTime, 4.5))
        }

        // The light on its way, with what it leaves behind
        Repeater {
            model: player.run > 0 && player.run < 1 ? 7 : 0

            Round {
                required property int index
                readonly property real u: Math.max(0, F.inOutSine(player.run) - index * 0.045)
                size: 13 - index * 1.5
                x: F.quadX(player.fromX, player.bowX, player.toX, u) - size / 2
                y: F.quadX(player.fromY, player.bowY, player.toY, u) - size / 2
                color: index === 0 ? "white" : player.tint
                opacity: 1 - index * 0.13
            }
        }

        Bead {
            visible: player.run > 0 && player.run < 1
            x: F.quadX(player.fromX, player.bowX, player.toX, F.inOutSine(player.run))
            y: F.quadX(player.fromY, player.bowY, player.toY, F.inOutSine(player.run))
            tint: player.tint
            size: 14
        }

        Item {
            x: player.cx + player.slideX
            y: player.cy + player.slideY
            scale: 0.7 + 0.3 * player.k

            // The controller in their colour's light, brighter when they press
            Halo {
                x: -player.padW * 0.9
                y: -player.padH * 0.9
                width: player.padW * 1.8
                height: player.padH * 1.8
                tint: Qt.rgba(player.tint.r, player.tint.g, player.tint.b, 0.16 + 0.34 * F.decay(film.t, player.press - film.pressLead, 4))
            }

            C.PadView {
                x: -width / 2
                y: -height / 2 + 3 * F.decay(film.t, player.press - film.pressLead, 9)
                factor: player.padFactor
                skin: player.person.skin
                pad: film.padOf(player.who)
            }

            // Who it is: over the controller, or under it where the light leaves upwards
            Row {
                x: player.up ? -width / 2 : -player.padW / 2 + 4
                y: player.up ? player.padH / 2 + 12 : -player.padH / 2 - 78
                spacing: 14

                Rectangle {
                    width: 58
                    height: 58
                    radius: 29
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: player.person.from }
                        GradientStop { position: 1.0; color: player.person.to }
                    }

                    T {
                        anchors.centerIn: parent
                        text: player.person.initial
                        font.pixelSize: 27
                        font.weight: Font.ExtraBold
                    }
                }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0

                    T {
                        text: player.person.name
                        font.pixelSize: 28
                        font.weight: Font.ExtraBold
                    }

                    T {
                        text: player.person.where
                        color: player.who === 0 ? film.cyan : film.dim
                        font.pixelSize: 19
                        font.weight: player.who === 0 ? Font.ExtraBold : Font.DemiBold
                        font.capitalization: player.who === 0 ? Font.AllUppercase : Font.MixedCase
                        font.letterSpacing: player.who === 0 ? 1.2 : 0
                    }
                }
            }
        }
    }

    /** A card of glass: what the film puts things on. */
    component Glass: Rectangle {
        /** 0 to 1: lit, for the one the film is talking about. */
        property real lit: 0
        property color tint: film.purple
        radius: 28
        color: Qt.rgba(0.085 + 0.04 * lit, 0.07 + 0.03 * lit, 0.17 + 0.09 * lit, 0.8 + 0.12 * lit)
        border.width: 1.5 + lit
        border.color: Qt.rgba(F.mix(1, tint.r, lit), F.mix(1, tint.g, lit), F.mix(1, tint.b, lit), 0.11 + 0.7 * lit)
    }

    /** One of the places a game can come from, in "What you share". */
    component Source: Item {
        id: source
        property string iconName
        property string title
        property string line
        property color tint: film.purple
        property real at: 0
        property real onFrom: 0
        property real onTo: 0
        property real until: 999
        readonly property real k: F.enter(film.t, at, 0.65)
        readonly property real on: F.during(film.t, onFrom, onTo, 0.3, 0.3)
        readonly property real gone: 1 - F.leave(film.t, until - 0.4, 0.4)

        width: 610
        height: 136
        opacity: k * (1 - gone)
        transform: Translate { x: (1 - source.k) * -90 - source.gone * 70 }
        scale: 1 + 0.025 * on

        Halo {
            x: -120
            y: parent.height / 2 - width / 2
            width: parent.width + 240
            height: width
            tint: Qt.rgba(source.tint.r, source.tint.g, source.tint.b, 0.2 * source.on)
            transform: Scale { origin.y: (source.width + 240) / 2; yScale: 0.34 }
        }

        Glass {
            anchors.fill: parent
            lit: source.on
            tint: source.tint
        }

        Rectangle {
            x: 28
            y: 28
            width: 80
            height: 80
            radius: 24
            color: Qt.rgba(source.tint.r, source.tint.g, source.tint.b, 0.14 + 0.16 * source.on)

            C.Icon {
                anchors.centerIn: parent
                iconName: source.iconName
                size: 42
                color: F.lerpColor(Qt.rgba(0.82, 0.8, 0.93, 1), source.tint, 0.35 + 0.65 * source.on)
            }
        }

        T {
            x: 134
            y: 30
            text: source.title
            font.pixelSize: 32
            font.weight: Font.ExtraBold
        }

        T {
            x: 134
            y: 76
            text: source.line
            color: film.soft
            font.pixelSize: 22
            font.weight: Font.Medium
        }

        // On: this is where the game comes from right now
        Round {
            size: 14
            x: source.width - 44
            y: source.height / 2 - 7
            color: source.tint
            opacity: source.on
            scale: 1 + 0.25 * Math.sin(film.t * 6)
        }
    }

    /**
     * A line of light from one place to another, in a bow. When `who` presses (film.jumps), a
     * light runs along it.
     */
    component Wire: Item {
        id: wire
        property int who: 0
        property real fromX: 0
        property real fromY: 0
        property real toX: 0
        property real toY: 0
        /** How far the bow rises above the straight way. */
        property real bow: 60
        property real at: 0
        property real until: 999
        readonly property color tint: film.castColors[who]
        readonly property real bowX: (fromX + toX) / 2
        readonly property real bowY: Math.min(fromY, toY) - bow
        readonly property real press: film.pressOf(who)
        readonly property real run: F.seg(film.t, press - film.pressLead + 0.02, press - film.pressLead + 0.02 + film.lightTime * 1.15)

        visible: film.t > at && film.t < until
        opacity: F.during(film.t, at, until, 0.5, 0.35)

        Shape {
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: Qt.rgba(wire.tint.r, wire.tint.g, wire.tint.b, 0.34 + 0.55 * F.decay(film.t, wire.press - film.pressLead, 3.5))
                strokeWidth: 3
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: wire.fromX
                startY: wire.fromY
                PathQuad {
                    x: wire.toX
                    y: wire.toY
                    controlX: wire.bowX
                    controlY: wire.bowY
                }
            }
        }

        Halo {
            x: wire.toX - 60
            y: wire.toY - 60
            width: 120
            height: 120
            tint: Qt.rgba(wire.tint.r, wire.tint.g, wire.tint.b, 0.85 * F.decay(film.t, wire.press - film.pressLead + film.lightTime * 1.15, 4.5))
        }

        Repeater {
            model: wire.run > 0 && wire.run < 1 ? 7 : 0

            Round {
                required property int index
                readonly property real u: Math.max(0, F.inOutSine(wire.run) - index * 0.035)
                size: 13 - index * 1.5
                x: F.quadX(wire.fromX, wire.bowX, wire.toX, u) - size / 2
                y: F.quadX(wire.fromY, wire.bowY, wire.toY, u) - size / 2
                color: index === 0 ? "white" : wire.tint
                opacity: 1 - index * 0.13
            }
        }

        Bead {
            visible: wire.run > 0 && wire.run < 1
            x: F.quadX(wire.fromX, wire.bowX, wire.toX, F.inOutSine(wire.run))
            y: F.quadX(wire.fromY, wire.bowY, wire.toY, F.inOutSine(wire.run))
            tint: wire.tint
            size: 14
        }
    }

    /**
     * A friend's device in "Invite friends": a laptop, a phone or a tablet. Dark until the
     * invite arrives (`joined`), then it shows the game like every other screen.
     */
    component Device: Item {
        id: device
        property string kind: "laptop"
        /** Width of its picture; x and y are the picture's corner. */
        property real w: 400
        property int who: 1
        property string how
        property real at: 0
        property real joined: 0
        property real until: 999
        /** Where its name goes: under it, or to its right. */
        property bool labelRight: false
        /** How far under the picture's top the name stands, for devices in a row whose names stand in one line; as its kind wants it if negative. */
        property real labelTop: -1
        readonly property real h: w * 9 / 16
        readonly property real rim: kind === "laptop" ? 13 : kind === "phone" ? 11 : 17
        readonly property var person: film.cast[who]
        readonly property color tint: film.castColors[who]
        readonly property real k: F.pop(film.t, at, 10, 6.5)
        readonly property real inK: F.seg(film.t, joined, joined + 0.45)

        visible: film.t > at && film.t < until
        opacity: F.seg(film.t, at, at + 0.25) * F.leave(film.t, until - 0.4, 0.4)
        scale: 0.8 + 0.2 * k
        transformOrigin: Item.Center
        width: w
        height: h

        // The moment it gets in
        Halo {
            x: device.w / 2 - width / 2
            y: device.h / 2 - width / 2
            width: device.w * 2
            height: width
            tint: Qt.rgba(device.tint.r, device.tint.g, device.tint.b, 0.5 * F.decay(film.t, device.joined, 2.6))
        }

        // The body
        Rectangle {
            x: -device.rim
            y: -device.rim
            width: device.w + 2 * device.rim
            height: device.h + 2 * device.rim
            radius: device.kind === "phone" ? 26 : 22
            color: "#0c0a1a"
            border.width: 2
            border.color: Qt.rgba(1, 1, 1, 0.24)
        }

        // A laptop stands on its lower half
        Rectangle {
            visible: device.kind === "laptop"
            x: -device.rim - 38
            y: device.h + device.rim - 1
            width: device.w + 2 * device.rim + 76
            height: 17
            radius: 8.5
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#3b3566" }
                GradientStop { position: 1.0; color: "#1c1838" }
            }

            Rectangle {
                x: parent.width / 2 - 44
                width: 88
                height: 6
                radius: 3
                color: "#15122b"
            }
        }

        // Not in yet
        Rectangle {
            width: device.w
            height: device.h
            radius: 30 * device.w / 800
            color: "#141029"

            C.Icon {
                anchors.centerIn: parent
                iconName: "link"
                size: Math.min(46, device.h * 0.3)
                color: Qt.rgba(1, 1, 1, 0.22)
            }
        }

        Screen {
            w: device.w
            shine: 0.35
            opacity: device.inK
        }

        // On a phone, the controller is on the screen
        Item {
            visible: device.kind === "phone"
            opacity: device.inK * 0.85

            Round { size: 34; x: 14; y: device.h - 48; color: Qt.rgba(1, 1, 1, 0.16); border.width: 1.5; border.color: Qt.rgba(1, 1, 1, 0.5) }
            Round { size: 14; x: 24; y: device.h - 38; color: Qt.rgba(1, 1, 1, 0.55) }
            Round { size: 20; x: device.w - 52; y: device.h - 34; color: Qt.rgba(1, 1, 1, 0.16); border.width: 1.5; border.color: Qt.rgba(1, 1, 1, 0.5) }
            Round { size: 20; x: device.w - 30; y: device.h - 52; color: Qt.rgba(1, 1, 1, 0.16); border.width: 1.5; border.color: Qt.rgba(1, 1, 1, 0.5) }
        }

        // Who it is and how they came in
        Row {
            x: device.labelRight ? device.w + device.rim + 26 : device.w / 2 - width / 2
            y: device.labelRight ? device.h / 2 - height / 2
               : device.labelTop >= 0 ? device.labelTop : device.h + device.rim + (device.kind === "laptop" ? 36 : 20)
            spacing: 14
            opacity: F.enter(film.t, device.joined + 0.1, 0.5)

            Rectangle {
                width: 50
                height: 50
                radius: 25
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: device.person.from }
                    GradientStop { position: 1.0; color: device.person.to }
                }

                T {
                    anchors.centerIn: parent
                    text: device.person.initial
                    font.pixelSize: 23
                    font.weight: Font.ExtraBold
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter

                T {
                    text: device.person.name
                    font.pixelSize: 25
                    font.weight: Font.ExtraBold
                }

                T {
                    text: device.how
                    color: film.soft
                    font.pixelSize: 19
                    font.weight: Font.DemiBold
                }
            }
        }
    }

    /** One way of sharing the controller, in "Who plays": a small picture of it, its name and what it means. */
    component Mode: Item {
        id: mode
        /** 0 together, 1 one at a time, 2 each their own. */
        property int kind: 0
        property string title
        property string line
        property real at: 0
        property real onFrom: 0
        property real onTo: 0
        property real until: 999
        readonly property real k: F.enter(film.t, at, 0.7)
        readonly property real on: F.during(film.t, onFrom, onTo, 0.35, 0.35)
        readonly property real gone: 1 - F.leave(film.t, until - 0.4, 0.4)
        /** One at a time: whose turn it is, two beats each; theirs from when their first light sets off. */
        readonly property int turn: Math.floor((film.t + 0.28) / (2 * film.beat)) % 3
        readonly property var tints: [film.cMika, film.cLeo, film.cAva]
        /**
         * The lights arrive on the film's beat (and sound there): together and each their own,
         * one controller after the other half a beat apart, every two beats; one at a time,
         * whoever's turn it is on every beat.
         */
        readonly property real every: kind === 1 ? film.beat : 2 * film.beat
        readonly property real travel: 0.28

        /** How long ago controller `i`'s light arrived, counted from the film's start. */
        function into(i) {
            const phase = kind === 1 ? 0 : i * film.beat / 2;
            return ((film.t - phase) % every + every) % every;
        }

        /** 0 to 1 while controller `i`'s light is on its way up; -1 otherwise. */
        function run(i) {
            if (kind === 1 && i !== turn)
                return -1;
            const left = every - into(i);
            return left < travel ? 1 - left / travel : -1;
        }

        /** 0 and up: how long ago controller `i`'s light arrived. */
        function since(i) {
            if (kind === 1 && i !== turn)
                return 9;
            return into(i);
        }

        width: 520
        height: 400
        opacity: k * (1 - gone) * (0.62 + 0.38 * on)
        scale: (film.tall ? 1.6 : 1) * (1 + 0.03 * on)
        transform: Translate { y: (1 - mode.k) * 60 + mode.gone * 30 }

        Halo {
            x: -90
            y: 200 - width / 2
            width: mode.width + 180
            height: width
            tint: Qt.rgba(140 / 255, 82 / 255, 1, 0.22 * mode.on)
            transform: Scale { origin.y: (mode.width + 180) / 2; yScale: 0.7 }
        }

        Glass {
            anchors.fill: parent
            lit: mode.on
            tint: film.cYou
        }

        // The game, small: sky, ground, and who is in it
        Rectangle {
            id: miniGame
            x: 150
            y: 34
            width: 220
            height: 124
            radius: 16
            border.width: 1.5
            border.color: Qt.rgba(1, 1, 1, 0.2)
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#1a1048" }
                GradientStop { position: 0.74; color: "#8a3890" }
                GradientStop { position: 0.75; color: "#150d35" }
            }

            Rectangle {
                x: 1.5
                y: 92
                width: 217
                height: 2
                color: film.pink
            }

            // One explorer for all, or one for each
            Repeater {
                model: mode.kind === 2 ? 3 : 1

                Item {
                    id: mini
                    required property int index
                    /** How long since somebody last made this one jump. */
                    readonly property real last: mode.kind === 2 ? mode.since(index)
                                                 : Math.min(mode.since(0), mode.since(1), mode.since(2))
                    readonly property color glow: mode.kind === 2 ? mode.tints[index]
                                                  : mode.since(0) <= mode.since(1) && mode.since(0) <= mode.since(2) ? mode.tints[0]
                                                  : mode.since(1) <= mode.since(2) ? mode.tints[1] : mode.tints[2]
                    x: mode.kind === 2 ? 54 + index * 56 : 110
                    y: 92 - 28 - 30 * F.bump(last, 0, 0.36)

                    Round {
                        size: 40
                        x: -20
                        y: -6
                        color: Qt.rgba(mini.glow.r, mini.glow.g, mini.glow.b, 0.5 * F.decay(mini.last, 0, 5))
                    }

                    Rectangle {
                        x: -14
                        width: 28
                        height: 28
                        radius: 10
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#ffffff" }
                            GradientStop { position: 1.0; color: "#cdb8ff" }
                        }

                        Rectangle {
                            x: 6
                            y: 7
                            width: 20
                            height: 11
                            radius: 5.5
                            color: "#1b1242"

                            Rectangle { x: 6; y: 3; width: 3.5; height: 5; radius: 1.7; color: mode.kind === 2 ? mini.glow : F.lerpColor(mini.glow, film.cyan, F.seg(mini.last, 0.3, 0.8)) }
                            Rectangle { x: 12.5; y: 3; width: 3.5; height: 5; radius: 1.7; color: mode.kind === 2 ? mini.glow : F.lerpColor(mini.glow, film.cyan, F.seg(mini.last, 0.3, 0.8)) }
                        }
                    }
                }
            }
        }

        // Three controllers, their lines up to the game, and the lights on them
        Repeater {
            model: 3

            Item {
                id: seat
                required property int index
                readonly property color tint: mode.tints[index]
                readonly property real cx: 130 + index * 130
                /** Where its line ends: at the one explorer, or at its own. */
                readonly property real upX: mode.kind === 2 ? 204 + index * 56 : 236 + index * 24
                readonly property bool resting: mode.kind === 1 && index !== mode.turn
                readonly property real u: mode.run(index)

                Shape {
                    preferredRendererType: Shape.CurveRenderer

                    ShapePath {
                        strokeColor: Qt.rgba(seat.tint.r, seat.tint.g, seat.tint.b, seat.resting ? 0.12 : 0.42)
                        strokeWidth: 2.5
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        startX: seat.cx
                        startY: 196
                        PathCubic {
                            x: seat.upX
                            y: 160
                            control1X: seat.cx
                            control1Y: 176
                            control2X: seat.upX
                            control2Y: 184
                        }
                    }
                }

                Bead {
                    visible: seat.u >= 0
                    x: F.mix(seat.cx, seat.upX, F.inOutSine(Math.max(0, seat.u)))
                    y: F.mix(196, 160, Math.max(0, seat.u))
                    tint: seat.tint
                    size: 9
                }

                // The turn: a ring around whoever has it
                Round {
                    visible: mode.kind === 1
                    size: 86
                    x: seat.cx - 43
                    y: 196
                    color: "transparent"
                    border.width: 2.5
                    border.color: seat.tint
                    opacity: seat.resting ? 0 : 0.9
                    scale: 1 + 0.06 * Math.sin(film.t * 5)
                }

                Round {
                    size: 70
                    x: seat.cx - 35
                    y: 204
                    color: Qt.rgba(seat.tint.r, seat.tint.g, seat.tint.b, seat.resting ? 0.07 : 0.2)
                    border.width: 2
                    border.color: Qt.rgba(seat.tint.r, seat.tint.g, seat.tint.b, seat.resting ? 0.25 : 0.85)
                    scale: 1 + 0.1 * Math.max(0, seat.u) * (seat.resting ? 0 : 1)

                    C.Icon {
                        anchors.centerIn: parent
                        iconName: "gamepad"
                        size: 38
                        color: seat.tint
                        opacity: seat.resting ? 0.35 : 1
                    }
                }

                // Each their own: which player they are
                T {
                    visible: mode.kind === 2
                    x: seat.cx + 26
                    y: 196
                    text: "P" + (seat.index + 1)
                    color: seat.tint
                    font.pixelSize: 18
                    font.weight: Font.ExtraBold
                }
            }
        }

        T {
            x: 36
            y: 292
            text: mode.title
            font.pixelSize: 31
            font.weight: Font.ExtraBold
        }

        T {
            x: 36
            y: 336
            width: mode.width - 72
            wrapMode: Text.WordWrap
            text: mode.line
            color: film.soft
            font.pixelSize: 21
            font.weight: Font.Medium
        }
    }

    /** How much `who` is talking right now, 0 to 1 (film.talks). */
    function voiceOf(who) {
        const talk = talks[who];
        const on = F.during(t, talk[0], talk[1], 0.12, 0.2);
        return on * (0.3 + 0.7 * Math.abs(Math.sin(t * 13 + who * 2) * Math.sin(t * 5.3 + who)));
    }

    /**
     * Somebody's webcam in "Talk and see": a small figure in their colour that looks around,
     * blinks, and moves its mouth while it talks; whoever talks gets the green ring the app gives them.
     */
    component Cam: Item {
        id: cam
        property int who: 0
        property real at: 0
        property real until: 999
        readonly property var person: film.cast[who]
        readonly property color tint: film.castColors[who]
        readonly property real k: F.pop(film.t, at, 10, 6.5)
        readonly property real voice: film.voiceOf(who)
        readonly property bool talking: voice > 0.02
        readonly property color skin: F.lerpColor(Qt.rgba(1, 1, 1, 1), tint, 0.22)
        readonly property color dark: F.lerpColor(tint, Qt.rgba(0.05, 0.04, 0.12, 1), 0.55)

        /** Smaller or larger than its 280 x 158, around its middle. */
        property real zoom: 1

        width: 280
        height: 158
        visible: film.t > at && film.t < until
        opacity: F.seg(film.t, at, at + 0.25) * F.leave(film.t, until - 0.4, 0.4)
        scale: (0.8 + 0.2 * k) * zoom

        // Talking: light around the picture
        Halo {
            x: cam.width / 2 - width / 2
            y: cam.height / 2 - width / 2
            width: cam.width * 1.5
            height: width
            tint: Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.3 * Math.min(1, cam.voice * 3))
            transform: Scale { origin.y: cam.width * 0.75; yScale: 0.62 }
        }

        Rectangle {
            id: camBack
            anchors.fill: parent
            radius: 22
            gradient: Gradient {
                GradientStop { position: 0.0; color: F.lerpColor(cam.tint, Qt.rgba(0.07, 0.05, 0.16, 1), 0.62) }
                GradientStop { position: 1.0; color: F.lerpColor(cam.tint, Qt.rgba(0.05, 0.04, 0.12, 1), 0.86) }
            }
        }

        // The figure: it sways a little, and nods along with what it says. It never comes
        // near the picture's round corners, so cutting it to the picture's box is enough.
        Item {
            anchors.fill: parent
            clip: true

        Item {
            id: figure
            x: cam.width / 2
            y: cam.height
            rotation: 2.2 * Math.sin(film.t * 0.9 + cam.who * 1.7) + 2.5 * cam.voice * Math.sin(film.t * 7)

            // Shoulders
            Rectangle {
                x: -70
                y: -52
                width: 140
                height: 90
                radius: 40
                color: F.lerpColor(cam.tint, Qt.rgba(1, 1, 1, 1), 0.12)
            }

            // Whoever plays with a headset wears it: the band goes behind the head
            Rectangle {
                visible: cam.who === 1
                x: -46
                y: -136
                width: 92
                height: 80
                radius: 44
                color: "transparent"
                border.width: 7
                border.color: cam.dark
            }

            // Hair behind the head, for the one with long hair
            Rectangle {
                visible: cam.who === 3
                x: -45
                y: -128
                width: 90
                height: 96
                radius: 36
                color: cam.dark
            }

            // The head
            Round {
                size: 76
                x: -38
                y: -124
                color: cam.skin
            }

            // Hair, a cap, a knot: everybody looks like themselves
            Rectangle {
                visible: cam.who === 0
                x: -40
                y: -130
                width: 80
                height: 38
                radius: 19
                color: cam.dark
            }

            Round {
                visible: cam.who === 0
                size: 18
                x: -9
                y: -144
                color: cam.dark
            }

            Rectangle {
                visible: cam.who === 2
                x: -40
                y: -128
                width: 80
                height: 34
                radius: 17
                color: cam.dark
            }

            Rectangle {
                visible: cam.who === 2
                x: 6
                y: -104
                width: 54
                height: 10
                radius: 5
                color: cam.dark
            }

            Rectangle {
                visible: cam.who === 3
                x: -38
                y: -128
                width: 76
                height: 30
                radius: 15
                color: cam.dark
            }

            Round {
                visible: cam.who === 3
                size: 26
                x: -13
                y: -150
                color: cam.dark
            }

            // The headset's ear cups and its microphone
            Repeater {
                model: cam.who === 1 ? 2 : 0

                Rectangle {
                    required property int index
                    x: index === 0 ? -50 : 34
                    y: -98
                    width: 16
                    height: 30
                    radius: 8
                    color: cam.dark
                }
            }

            Rectangle {
                visible: cam.who === 1
                x: -44
                y: -70
                width: 30
                height: 6
                radius: 3
                rotation: 24
                color: cam.dark
            }

            // Eyes that blink, and a mouth that says what the voice says
            Repeater {
                model: 2

                Rectangle {
                    required property int index
                    readonly property real blink: ((film.t + cam.who * 0.7) % 3.4) < 0.13 ? 0.15 : 1
                    x: -16 + index * 24
                    y: -92 + 4 * (1 - blink)
                    width: 8
                    height: 10 * blink
                    radius: 4
                    color: "#1b1242"
                }
            }

            Rectangle {
                x: -9 - 3 * cam.voice
                y: -72
                width: 18 + 6 * cam.voice
                height: 4 + 13 * cam.voice
                radius: Math.min(width, height) / 2
                color: "#1b1242"
            }
        }

        }

        Rectangle {
            anchors.fill: parent
            radius: 22
            color: "transparent"
            border.width: cam.talking ? 3.5 : 1.5
            border.color: cam.talking ? film.green : Qt.rgba(1, 1, 1, 0.16)
        }

        // The name, and the voice as it goes up and down
        Rectangle {
            x: 10
            y: cam.height - 42
            width: camName.width + (cam.talking ? 56 : 24)
            height: 32
            radius: 16
            color: Qt.rgba(0.03, 0.02, 0.09, 0.72)

            T {
                id: camName
                x: 12
                anchors.verticalCenter: parent.verticalCenter
                text: cam.person.name
                font.pixelSize: 18
                font.weight: Font.ExtraBold
            }

            Repeater {
                model: cam.talking ? 4 : 0

                Rectangle {
                    required property int index
                    readonly property real level: 0.25 + 0.75 * Math.abs(Math.sin(film.t * (9 + index * 2.3) + index * 1.7)) * cam.voice
                    x: camName.x + camName.width + 10 + index * 7
                    y: 16 - 9 * level
                    width: 4
                    height: 18 * level
                    radius: 2
                    color: film.green
                }
            }
        }
    }

    /** A small thing the app can do, by its icon and name: for lists. */
    component Chip: Item {
        id: chip
        property string iconName
        property string text
        property color tint: film.cYou
        property real at: 0
        property real until: 999
        readonly property real k: F.pop(film.t, at, 10, 6.5)
        /** Larger than its 300 x 62, around its middle. */
        property real zoom: 1

        width: 300
        height: 62
        visible: film.t > at && film.t < until
        opacity: F.seg(film.t, at, at + 0.25) * F.leave(film.t, until - 0.4, 0.4)
        transform: Translate { x: (1 - F.enter(film.t, chip.at, 0.6)) * 60 }
        scale: (0.9 + 0.1 * k) * zoom

        Glass {
            anchors.fill: parent
            radius: 31
            lit: 0.35 * F.decay(film.t, chip.at, 2)
            tint: chip.tint
        }

        Round {
            size: 42
            x: 10
            y: 10
            color: Qt.rgba(chip.tint.r, chip.tint.g, chip.tint.b, 0.2)

            C.Icon {
                anchors.centerIn: parent
                iconName: chip.iconName
                size: 23
                color: chip.tint
            }
        }

        T {
            x: 66
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            font.pixelSize: 22
            font.weight: Font.ExtraBold
        }
    }

    /** A line of text whose letters come up one after the other, in a colour that runs from `from` to `to`. */
    component Letters: Item {
        id: letters
        property string text
        property real size: 100
        property color from: "#a77bff"
        property color to: film.pink
        /** When the first letter starts, and how long after it each next one. */
        property real at: 0
        property real each: 0.035
        /** When the whole line leaves; never if negative. */
        property real until: -1

        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        Row {
            id: row

            Repeater {
                model: letters.text.length

                T {
                    required property int index
                    readonly property real k: F.enter(film.t, letters.at + index * letters.each, 0.55)
                    readonly property real gone: letters.until < 0 ? 0 : F.inCubic(F.seg(film.t, letters.until + index * 0.008, letters.until + 0.4 + index * 0.008))
                    text: letters.text.charAt(index)
                    font.pixelSize: letters.size
                    font.weight: Font.ExtraBold
                    color: F.lerpColor(letters.from, letters.to, letters.text.length > 1 ? index / (letters.text.length - 1) : 0)
                    opacity: k * (1 - gone)
                    transform: Translate { y: (1 - k) * letters.size * 0.42 - gone * letters.size * 0.2 }
                }
            }
        }
    }

    // ------------------------------------------------------------ the game every screen shows

    IntroGame {
        id: gamePicture
        visible: false
        layer.enabled: true
        layer.smooth: true
        t: film.t
        jumps: film.jumps
        colors: film.castColors
        family: film.family
        // What you share: another world for each place a game can come from
        world: F.track(film.t, [[film.tSource2 - 0.3, 0], [film.tSource2 + 0.3, 1], [film.tSource3 - 0.3, 1], [film.tSource3 + 0.3, 2],
                                [film.tInvite - 0.2, 2], [film.tInvite + 0.6, 3], [film.tModes + 0.15, 3], [film.tModes + 0.2, 0]])
        tag: film.t < film.tShare + 0.7 || film.t > film.tInvite ? ""
             : film.t < film.tSource2 ? "PS5" : film.t < film.tSource3 ? "PC" : "Xbox"
    }

    // The screen the film follows: where it is and how wide, chapter by chapter
    /** Where the screen is in each chapter, as [x, y, width]. */
    readonly property var screenAt: tall
        ? ({ play: [90, 630, 900], share: [190, 1090, 700], invite: [220, 630, 640], couch: [90, 769, 900], stream: [90, 630, 900] })
        : ({ play: [520, 178, 880], share: [930, 215, 800], invite: [150, 232, 640], couch: [180, 168, 960], stream: [440, 150, 1040] })

    /** One of the three ([x, y, width]) along the film. */
    function screenTrack(i) {
        const at = screenAt;
        return F.track(t, [[tShare - 0.2, at.play[i]], [tShare + 0.7, at.share[i]], [tInvite - 0.1, at.share[i]], [tInvite + 0.8, at.invite[i]],
                           [tModes + 0.15, at.invite[i]], [tModes + 0.2, at.couch[i]], [tStream - 0.2, at.couch[i]], [tStream + 0.7, at.stream[i]]]);
    }

    readonly property real screenX: screenTrack(0)
    readonly property real screenY: screenTrack(1)
    readonly property real screenW: screenTrack(2)
    /** It steps aside while the film talks about who plays, and at the end. */
    readonly property real screenThere: F.seg(t, tPlay + 0.05, tPlay + 0.4) * (1 - F.seg(t, tModes - 0.35, tModes + 0.1))
                                        + F.seg(t, tCouch - 0.05, tCouch + 0.45) * (1 - F.seg(t, tEnd - 0.45, tEnd))

    Screen {
        id: mainScreen
        readonly property real k: F.pop(film.t, film.tPlay + 0.05, 9, 6)
        visible: film.screenThere > 0
        w: film.screenW
        x: film.screenX
        y: film.screenY
        opacity: film.screenThere
        scale: (0.82 + 0.18 * k) * (0.94 + 0.06 * film.screenThere)
    }

    // ------------------------------------------------------------ one game, everybody plays

    Item {
        id: playScene
        visible: film.t > film.tPlay && film.t < film.tShare + 0.4
        /** Tall, the screen comes down to where the players are: they have left by then. */
        readonly property real until: film.tShare + (film.tall ? -0.1 : 0.15)

        Player {
            who: 0
            cx: film.tall ? 255 : 240
            cy: film.tall ? 1330 : 322
            toX: film.tall ? 300 : 520
            toY: film.tall ? 1136 : 350
            at: film.tPlay + 0.4
            until: playScene.until
        }

        Player {
            who: 1
            cx: film.tall ? 445 : 1680
            cy: film.tall ? 1330 : 322
            toX: film.tall ? 460 : 1400
            toY: film.tall ? 1136 : 350
            at: film.tPlay + 2.1
            until: playScene.until + 0.05
        }

        Player {
            who: 2
            cx: film.tall ? 635 : 240
            cy: film.tall ? 1330 : 590
            toX: film.tall ? 620 : 520
            toY: film.tall ? 1136 : 540
            at: film.tPlay + 2.4
            until: playScene.until + 0.1
        }

        Player {
            who: 3
            cx: film.tall ? 825 : 1680
            cy: film.tall ? 1330 : 590
            toX: film.tall ? 780 : 1400
            toY: film.tall ? 1136 : 540
            at: film.tPlay + 2.7
            until: playScene.until + 0.15
        }

        Caption {
            head: qsTr("One game.")
            accent: qsTr("Everybody plays.")
            line: qsTr("Your friends' controllers play your game, as if they sat next to you.")
            from: film.tPlay + 0.7
            to: film.tShare - 0.05
        }
    }

    // ------------------------------------------------------------ what you share

    Item {
        id: shareScene
        visible: film.t > film.tShare && film.t < film.tInvite + 0.5
        /** Tall, the screen goes up through where the places stand: they have left by then. */
        readonly property real until: film.tInvite + (film.tall ? -0.05 : 0.25)
        /** Which place has the word, 0 to 2, and anything between while it passes on. */
        readonly property real turn: F.track(film.t, [[film.tSource2 - 0.25, 0], [film.tSource2 + 0.25, 1], [film.tSource3 - 0.25, 1], [film.tSource3 + 0.25, 2]])
        readonly property color turnTint: turn <= 1 ? F.lerpColor(film.cYou, film.cyan, turn) : F.lerpColor(film.cyan, Qt.rgba(0.56, 0.89, 0.42, 1), turn - 1)
        /** From the card that has the word to the screen: beside the cards, or under them. */
        readonly property real fromY: film.tall ? 698 + turn * 146 : 283 + turn * 158
        readonly property real fromX: 800
        readonly property real toX: film.tall ? 830 : 930
        readonly property real toY: film.tall ? 1090 : 440
        readonly property real bow1X: film.tall ? 905 : 880
        readonly property real bow2X: film.tall ? 905 : 850
        readonly property real bow2Y: film.tall ? 1020 : 440
        readonly property real there: F.during(film.t, film.tShare + 0.7, film.tInvite - 0.1, 0.5, 0.4)

        // The way the picture takes: a line from the place to the screen, with light running along it
        Shape {
            preferredRendererType: Shape.CurveRenderer
            opacity: shareScene.there

            ShapePath {
                strokeColor: Qt.rgba(shareScene.turnTint.r, shareScene.turnTint.g, shareScene.turnTint.b, 0.55)
                strokeWidth: 3.5
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: shareScene.fromX
                startY: shareScene.fromY
                PathCubic {
                    x: shareScene.toX
                    y: shareScene.toY
                    control1X: shareScene.bow1X
                    control1Y: shareScene.fromY
                    control2X: shareScene.bow2X
                    control2Y: shareScene.bow2Y
                }
            }
        }

        Repeater {
            model: 3

            Bead {
                required property int index
                readonly property real u: (film.t * 0.9 + index / 3) % 1
                readonly property real v: 1 - u
                // The same curve as the line
                x: v * v * v * shareScene.fromX + 3 * v * v * u * shareScene.bow1X + 3 * v * u * u * shareScene.bow2X + u * u * u * shareScene.toX
                y: v * v * v * shareScene.fromY + 3 * v * v * u * shareScene.fromY + 3 * v * u * u * shareScene.bow2Y + u * u * u * shareScene.toY
                tint: shareScene.turnTint
                size: 11
                opacity: shareScene.there * Math.sin(Math.PI * u)
            }
        }

        Source {
            x: film.tall ? 180 : 190
            y: film.tall ? 630 : 215
            iconName: "console"
            title: qsTr("Your PlayStation")
            line: qsTr("A PS5 or PS4, over Remote Play")
            tint: film.cYou
            at: film.tShare + 0.3
            onFrom: film.tShare + 0.6
            onTo: film.tSource2 + 0.15
            until: shareScene.until
        }

        Source {
            x: film.tall ? 180 : 190
            y: film.tall ? 776 : 373
            iconName: "monitor"
            title: qsTr("This computer")
            line: qsTr("A game on Windows, Linux or macOS")
            tint: film.cyan
            at: film.tShare + 0.45
            onFrom: film.tSource2 - 0.15
            onTo: film.tSource3 + 0.15
            until: shareScene.until + 0.06
        }

        Source {
            x: film.tall ? 180 : 190
            y: film.tall ? 922 : 531
            iconName: "gamepad"
            title: qsTr("Your Xbox")
            line: qsTr("Through the Xbox app on this computer")
            tint: Qt.rgba(0.56, 0.89, 0.42, 1)
            at: film.tShare + 0.6
            onFrom: film.tSource3 - 0.15
            onTo: film.tInvite - 0.2
            until: shareScene.until + 0.12
        }

        Caption {
            head: qsTr("Share")
            accent: qsTr("what you play.")
            line: qsTr("From your PS5 or PS4, from this computer, or from an Xbox.")
            from: film.tShare + 0.7
            to: film.tInvite - 0.05
        }
    }

    // ------------------------------------------------------------ invite friends

    Item {
        id: inviteScene
        visible: film.t > film.tInvite && film.t < film.tModes + 0.5
        readonly property real until: film.tModes + 0.2
        /** When the link is copied, and when each friend gets it and is in. */
        readonly property real tCopy: film.tInvite + 1.2
        readonly property var tSent: [0, film.tInvite + 1.8, film.tInvite + 2.1, film.tInvite + 2.4]
        readonly property var tIn: [0, film.tInvite + 2.7, film.tInvite + 3.0, film.tInvite + 3.3]
        /** Where each friend's device is: the middle of its picture. */
        readonly property var spot: film.tall
            ? [Qt.point(0, 0), Qt.point(305, 1175), Qt.point(590, 1175), Qt.point(825, 1175)]
            : [Qt.point(0, 0), Qt.point(1130, 302), Qt.point(1440, 589), Qt.point(1620, 282)]
        /** Where the link sets off: the Copy button. */
        readonly property point sentFrom: film.tall ? Qt.point(732, 1430) : Qt.point(660, 686)

        // Whose game it is (wide; tall, the heading stands right over the screen)
        Row {
            visible: !film.tall
            x: 154
            y: 164
            spacing: 14
            opacity: F.during(film.t, film.tInvite + 0.6, inviteScene.until, 0.5, 0.4)

            Rectangle {
                width: 50
                height: 50
                radius: 25
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: film.cast[0].from }
                    GradientStop { position: 1.0; color: film.cast[0].to }
                }

                T {
                    anchors.centerIn: parent
                    text: film.cast[0].initial
                    font.pixelSize: 23
                    font.weight: Font.ExtraBold
                }
            }

            T {
                anchors.verticalCenter: parent.verticalCenter
                text: film.cast[0].name
                font.pixelSize: 26
                font.weight: Font.ExtraBold
            }

            T {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("HOST")
                color: film.cyan
                font.pixelSize: 18
                font.weight: Font.ExtraBold
                font.letterSpacing: 1.2
            }
        }

        // Straight to the host: each friend's line, and their lights when they play
        Wire {
            who: 1
            fromX: film.tall ? 305 : 913
            fromY: film.tall ? 1080 : 320
            toX: film.tall ? 380 : 790
            toY: film.tall ? 990 : 320
            bow: film.tall ? -40 : 44
            at: inviteScene.tIn[1] + 0.2
            until: inviteScene.until
        }

        Wire {
            who: 2
            fromX: film.tall ? 590 : 1269
            fromY: film.tall ? 1096 : 600
            toX: film.tall ? 540 : 790
            toY: film.tall ? 990 : 500
            bow: film.tall ? -48 : -46
            at: inviteScene.tIn[2] + 0.2
            until: inviteScene.until
        }

        Wire {
            who: 3
            fromX: film.tall ? 825 : 1485
            fromY: film.tall ? 1110 : 262
            toX: film.tall ? 700 : 790
            toY: film.tall ? 990 : 262
            bow: film.tall ? -54 : 176
            at: inviteScene.tIn[3] + 0.2
            until: inviteScene.until
        }

        Device {
            kind: "laptop"
            x: film.tall ? 170 : 930
            y: film.tall ? 1099 : 190
            w: film.tall ? 270 : 400
            who: 1
            how: qsTr("in the app")
            at: film.tInvite + 0.75
            joined: inviteScene.tIn[1]
            until: inviteScene.until
        }

        Device {
            kind: "tablet"
            x: film.tall ? 490 : 1290
            y: film.tall ? 1119 : 505
            w: film.tall ? 200 : 300
            labelTop: film.tall ? 181 : -1
            who: 2
            how: qsTr("in a browser")
            labelRight: !film.tall
            at: film.tInvite + 0.9
            joined: inviteScene.tIn[2]
            until: inviteScene.until + 0.05
        }

        Device {
            kind: "phone"
            x: film.tall ? 740 : 1500
            y: film.tall ? 1127 : 215
            w: film.tall ? 170 : 240
            labelTop: film.tall ? 173 : -1
            who: 3
            how: qsTr("on a phone")
            at: film.tInvite + 1.05
            joined: inviteScene.tIn[3]
            until: inviteScene.until + 0.1
        }

        // The invite: a link, copied with one click
        Item {
            id: invite
            readonly property real k: F.pop(film.t, film.tInvite + 0.7, 10, 6.5)
            readonly property real copied: F.seg(film.t, inviteScene.tCopy, inviteScene.tCopy + 0.2)
            x: film.tall ? 260 : 190
            y: film.tall ? 1384 : 640
            width: 560
            height: 92
            // Tall, it gives its place to what is said about the way the friends are joined
            opacity: F.seg(film.t, film.tInvite + 0.7, film.tInvite + 0.95)
                     * F.leave(film.t, film.tall ? film.tInvite + 3.5 : inviteScene.until - 0.4, film.tall ? 0.35 : 0.4)
            scale: 0.85 + 0.15 * k

            Glass {
                anchors.fill: parent
                radius: 46
                lit: 0.5 * F.decay(film.t, inviteScene.tCopy, 2.5)
            }

            Round {
                size: 60
                x: 16
                y: 16
                color: Qt.rgba(140 / 255, 82 / 255, 1, 0.22)

                C.Icon {
                    anchors.centerIn: parent
                    iconName: "link"
                    size: 30
                    color: "#cdb6ff"
                }
            }

            T {
                x: 94
                y: 17
                text: qsTr("Your invite link")
                font.pixelSize: 25
                font.weight: Font.ExtraBold
            }

            T {
                x: 94
                y: 50
                text: "psrpt://…/k7Qm2xNova"
                color: film.dim
                font.pixelSize: 20
                font.weight: Font.Medium
            }

            // Copy: pressed, then it says so
            Rectangle {
                x: parent.width - width - 14
                y: 16
                width: 148
                height: 60
                radius: 30
                scale: 1 - 0.07 * F.bump(film.t, inviteScene.tCopy - 0.12, 0.3)
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: F.lerpColor(Qt.rgba(122 / 255, 60 / 255, 245 / 255, 1), Qt.rgba(31 / 255, 157 / 255, 111 / 255, 1), invite.copied) }
                    GradientStop { position: 1.0; color: F.lerpColor(Qt.rgba(201 / 255, 45 / 255, 123 / 255, 1), Qt.rgba(52 / 255, 211 / 255, 153 / 255, 1), invite.copied) }
                }

                Row {
                    anchors.centerIn: parent
                    spacing: 8

                    C.Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: invite.copied > 0.5 ? "check" : "copy"
                        size: 22
                        color: "white"
                    }

                    T {
                        anchors.verticalCenter: parent.verticalCenter
                        text: invite.copied > 0.5 ? qsTr("Copied") : qsTr("Copy")
                        font.pixelSize: 22
                        font.weight: Font.ExtraBold
                    }
                }
            }
        }

        // The link on its way to each friend
        Repeater {
            model: 3

            Item {
                id: sent
                required property int index
                readonly property int who: index + 1
                readonly property color tint: film.castColors[who]
                readonly property real u: F.seg(film.t, inviteScene.tSent[who], inviteScene.tIn[who])
                readonly property real e: F.inOutCubic(u)
                readonly property point to: inviteScene.spot[who]
                visible: u > 0 && u < 1
                x: F.quadX(inviteScene.sentFrom.x, (inviteScene.sentFrom.x + to.x) / 2, to.x, e)
                y: F.quadX(inviteScene.sentFrom.y, film.tall ? to.y + 120 - index * 30 : Math.min(686, to.y) - 190 + index * 40, to.y, e)

                Halo {
                    x: -60
                    y: -60
                    width: 120
                    height: 120
                    tint: Qt.rgba(sent.tint.r, sent.tint.g, sent.tint.b, 0.6)
                }

                Rectangle {
                    x: -34
                    y: -22
                    width: 68
                    height: 44
                    radius: 22
                    color: "#1b1735"
                    border.width: 2.5
                    border.color: sent.tint
                    scale: 0.7 + 0.5 * Math.sin(Math.PI * sent.u)

                    C.Icon {
                        anchors.centerIn: parent
                        iconName: "link"
                        size: 24
                        color: "white"
                    }
                }
            }
        }

        // No server in between
        Item {
            readonly property real k: F.pop(film.t, film.tInvite + 3.9, 10, 6.5)
            x: film.tall ? film.midX - width / 2 : 792
            y: film.tall ? 1401 : 664
            width: direct.width
            height: direct.height
            opacity: F.seg(film.t, film.tInvite + 3.9, film.tInvite + 4.15) * F.leave(film.t, inviteScene.until - 0.4, 0.4)
            scale: (0.85 + 0.15 * k) * (film.tall ? 1.12 : 1)

            Rectangle {
                id: direct
                width: directRow.width + 44
                height: 58
                radius: 29
                color: Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.13)
                border.width: 1.5
                border.color: Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.6)

                Row {
                    id: directRow
                    anchors.centerIn: parent
                    spacing: 10

                    C.Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "shield"
                        size: 24
                        color: "#9ff0cc"
                    }

                    T {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Straight to you, no server in between")
                        color: "#9ff0cc"
                        font.pixelSize: 21
                        font.weight: Font.ExtraBold
                    }
                }
            }
        }

        Caption {
            head: qsTr("Invite")
            accent: qsTr("with a link.")
            line: qsTr("Friends join in the app or in a web browser, on a phone or tablet too.")
            from: film.tInvite + 0.7
            to: film.tModes - 0.05
        }
    }

    // ------------------------------------------------------------ who plays

    Item {
        id: modesScene
        visible: film.t > film.tModes - 0.1 && film.t < film.tCouch + 0.5
        /** Tall, the screen comes back right where the cards stand: they have left by then. */
        readonly property real until: film.tCouch + (film.tall ? -0.1 : 0.15)
        /**
         * Tall: one card at a time, large, in the middle; the next one comes in from the right
         * when the word passes on. Which card is in the middle, 0 to 2.
         */
        readonly property real turn: F.track(film.t, [[film.tModes + 2.15, 0], [film.tModes + 2.65, 1], [film.tModes + 4.55, 1], [film.tModes + 5.05, 2]])

        Mode {
            x: film.tall ? 280 - modesScene.turn * 920 : 150
            y: film.tall ? 780 : 236
            kind: 0
            title: qsTr("Together")
            line: qsTr("Everybody's buttons count at the same time.")
            at: film.tModes + 0.25
            onFrom: film.tModes + 0.6
            onTo: film.tModes + 2.55
            until: modesScene.until
        }

        Mode {
            x: film.tall ? 280 + (1 - modesScene.turn) * 920 : 700
            y: film.tall ? 780 : 236
            kind: 1
            title: qsTr("One at a time")
            line: qsTr("Whoever moves takes over, the others rest.")
            at: film.tModes + 0.45
            onFrom: film.tModes + 2.25
            onTo: film.tModes + 4.95
            until: modesScene.until + 0.06
        }

        Mode {
            x: film.tall ? 280 + (2 - modesScene.turn) * 920 : 1250
            y: film.tall ? 780 : 236
            kind: 2
            title: qsTr("Each their own")
            line: qsTr("Player 1, 2, 3 and 4, for a game on this computer.")
            at: film.tModes + 0.65
            onFrom: film.tModes + 4.65
            onTo: film.tCouch - 0.25
            until: modesScene.until + 0.12
        }

        // Tall: which of the three is in the middle
        Row {
            visible: film.tall
            x: film.midX - width / 2
            y: 1346
            spacing: 14
            opacity: F.during(film.t, film.tModes + 0.9, modesScene.until, 0.5, 0.4)

            Repeater {
                model: 3

                Rectangle {
                    required property int index
                    readonly property real here: Math.max(0, 1 - Math.abs(modesScene.turn - index))
                    width: 14 + 30 * here
                    height: 14
                    radius: 7
                    color: F.lerpColor(Qt.rgba(1, 1, 1, 0.22), film.cYou, here)
                }
            }
        }

        Caption {
            head: qsTr("You decide")
            accent: qsTr("who plays.")
            line: qsTr("Change it any time while you play. Friends can also just watch.")
            from: film.tModes + 0.6
            to: film.tCouch - 0.05
        }
    }

    // ------------------------------------------------------------ talk and see

    Item {
        id: couchScene
        visible: film.t > film.tCouch && film.t < film.tStream + 0.5
        /** Tall, the screen goes up to where the faces are: they have left by then. */
        readonly property real until: film.tStream + (film.tall ? -0.05 : 0.2)
        /** Tall: how large the faces are, and where the first one's corner is so that the row of four is as wide as the screen. */
        readonly property real camZoom: film.tall ? 0.78 : 1
        readonly property real camX: 90 - 280 * (1 - camZoom) / 2
        readonly property real camY: 630 - 158 * (1 - camZoom) / 2
        readonly property real camStep: (900 - 280 * camZoom) / 3

        // Wide: two by two beside the screen. Tall: in a row over it, as wide as it is
        Cam { who: 1; x: film.tall ? couchScene.camX : 1190; y: film.tall ? couchScene.camY : 168; zoom: couchScene.camZoom; at: film.tCouch + 0.45; until: couchScene.until }
        Cam { who: 3; x: film.tall ? couchScene.camX + couchScene.camStep : 1490; y: film.tall ? couchScene.camY : 168; zoom: couchScene.camZoom; at: film.tCouch + 0.6; until: couchScene.until + 0.04 }
        Cam { who: 0; x: film.tall ? couchScene.camX + 2 * couchScene.camStep : 1190; y: film.tall ? couchScene.camY : 346; zoom: couchScene.camZoom; at: film.tCouch + 0.75; until: couchScene.until + 0.08 }
        Cam { who: 2; x: film.tall ? couchScene.camX + 3 * couchScene.camStep : 1490; y: film.tall ? couchScene.camY : 346; zoom: couchScene.camZoom; at: film.tCouch + 0.9; until: couchScene.until + 0.12 }

        // The chat: what they write while they play
        Item {
            id: chat
            x: film.tall ? 250 : 1190
            y: film.tall ? 1311 : 528
            width: 580
            height: 180
            scale: film.tall ? 1.2 : 1
            opacity: F.during(film.t, film.tCouch + 1.0, couchScene.until, 0.5, 0.4)

            Glass {
                anchors.fill: parent
            }

            Repeater {
                model: [
                    { who: 1, text: qsTr("Jump, jump!"), at: film.tCouch + 2.7 },
                    { who: 3, text: qsTr("Got the ring!"), at: film.tCouch + 4.2 },
                    { who: 2, text: qsTr("One more round?"), at: film.tCouch + 6.3 }
                ]

                Row {
                    id: said
                    required property var modelData
                    required property int index
                    readonly property real k: F.pop(film.t, modelData.at, 11, 7)
                    x: 24
                    y: 24 + index * 48 + (1 - F.enter(film.t, modelData.at, 0.5)) * 18
                    spacing: 12
                    opacity: F.seg(film.t, modelData.at, modelData.at + 0.2)
                    scale: 0.92 + 0.08 * k
                    transformOrigin: Item.Left

                    Rectangle {
                        width: 34
                        height: 34
                        radius: 17
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: film.cast[said.modelData.who].from }
                            GradientStop { position: 1.0; color: film.cast[said.modelData.who].to }
                        }

                        T {
                            anchors.centerIn: parent
                            text: film.cast[said.modelData.who].initial
                            font.pixelSize: 16
                            font.weight: Font.ExtraBold
                        }
                    }

                    T {
                        anchors.verticalCenter: parent.verticalCenter
                        text: film.cast[said.modelData.who].name
                        color: film.castColors[said.modelData.who]
                        font.pixelSize: 22
                        font.weight: Font.ExtraBold
                    }

                    T {
                        anchors.verticalCenter: parent.verticalCenter
                        text: said.modelData.text
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                    }
                }
            }
        }

        Caption {
            head: qsTr("Like on")
            accent: qsTr("the same couch.")
            line: qsTr("Voice chat, webcams and a chat are built in.")
            from: film.tCouch + 0.6
            to: film.tStream - 0.05
        }
    }

    // ------------------------------------------------------------ streaming

    Item {
        id: streamScene
        visible: film.t > film.tStream && film.t < film.tEnd + 0.4
        readonly property real until: film.tEnd + 0.05
        /** When Go live is pressed. */
        readonly property real tLive: film.tStream + 1.2
        readonly property real live: F.seg(film.t, tLive, tLive + 0.25)
        readonly property real sinceLive: Math.max(0, film.t - tLive)
        readonly property real gone: F.leave(film.t, until - 0.45, 0.45)
        /** The screen's own scale: what lies on the picture is placed in its 1040 wide frame. */
        readonly property real s: film.screenW / 1040

        // What lies on the game picture, as on your stream
        Item {
            x: film.screenX
            y: film.screenY
            width: 1040
            height: 585
            // Tall, the heading stands right over the screen: what drops into the picture comes out of its edge
            clip: film.tall
            scale: streamScene.s
            transformOrigin: Item.TopLeft
            opacity: film.screenThere

            // Everybody's controller, along the bottom
            Repeater {
                model: 4

                Item {
                    id: seen
                    required property int index
                    readonly property real at: film.tStream + 2.1 + index * 0.15
                    readonly property real k: F.pop(film.t, at, 10, 6.5)
                    x: 196 + index * 216
                    y: 474
                    opacity: F.seg(film.t, at, at + 0.2) * streamScene.gone
                    scale: 0.6 + 0.4 * k

                    C.PadView {
                        x: -width / 2
                        factor: 0.115
                        skin: film.cast[seen.index].skin
                        pad: film.padOf(seen.index)
                    }

                    Rectangle {
                        x: -width / 2
                        y: 79
                        width: seenName.width + 20
                        height: 23
                        radius: 11.5
                        color: Qt.rgba(0, 0, 0, 0.6)

                        T {
                            id: seenName
                            anchors.centerIn: parent
                            text: film.cast[seen.index].name
                            font.pixelSize: 14
                            font.weight: Font.ExtraBold
                        }
                    }
                }
            }

            // Live, for how long, and how many watch
            Row {
                x: 1040 - width - 30
                y: 30
                spacing: 10
                opacity: streamScene.live * streamScene.gone
                scale: 0.8 + 0.2 * F.pop(film.t, streamScene.tLive, 11, 6)
                transformOrigin: Item.Right

                Rectangle {
                    width: liveRow.width + 28
                    height: 44
                    radius: 22
                    color: "#e91916"

                    Row {
                        id: liveRow
                        anchors.centerIn: parent
                        spacing: 8

                        Round {
                            anchors.verticalCenter: parent.verticalCenter
                            size: 10
                            color: "white"
                            opacity: 0.55 + 0.45 * Math.sin(film.t * 5)
                        }

                        T {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("LIVE")
                            font.pixelSize: 20
                            font.weight: Font.ExtraBold
                            font.letterSpacing: 1
                        }

                        T {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "0:" + String(Math.floor(streamScene.sinceLive * 9) % 60).padStart(2, "0")
                            font.pixelSize: 20
                            font.weight: Font.Bold
                            opacity: 0.9
                        }
                    }
                }

                Rectangle {
                    width: watchRow.width + 28
                    height: 44
                    radius: 22
                    color: Qt.rgba(0.03, 0.02, 0.09, 0.66)
                    border.width: 1.5
                    border.color: Qt.rgba(1, 1, 1, 0.16)

                    Row {
                        id: watchRow
                        anchors.centerIn: parent
                        spacing: 8

                        C.Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            iconName: "eye"
                            size: 21
                            color: "white"
                        }

                        T {
                            anchors.verticalCenter: parent.verticalCenter
                            text: String(Math.round(1 + 127 * F.outCubic(F.seg(streamScene.sinceLive, 0.2, 3.6))))
                            font.pixelSize: 20
                            font.weight: Font.ExtraBold
                        }
                    }
                }
            }

            // An alert: somebody follows
            Item {
                id: alert
                readonly property real from: film.tStream + 3.0
                readonly property real k: F.pop(film.t, from, 9, 6) * F.leave(film.t, from + 2.3, 0.35)
                x: 520 - 215
                y: 26 - (1 - Math.min(1, k)) * 150
                width: 430
                height: 96
                visible: film.t > from && film.t < from + 2.8
                opacity: Math.min(1, k * 2) * streamScene.gone

                Rectangle {
                    anchors.fill: parent
                    radius: 26
                    color: Qt.rgba(0.07, 0.05, 0.16, 0.9)
                    border.width: 2
                    border.color: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.75)
                }

                Round {
                    size: 60
                    x: 18
                    y: 18
                    color: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.24)
                    scale: 1 + 0.12 * F.decay(film.t, alert.from + 0.15, 5) * Math.sin((film.t - alert.from) * 22)

                    C.Icon {
                        anchors.centerIn: parent
                        iconName: "heart"
                        size: 32
                        color: film.pink
                    }
                }

                T {
                    x: 96
                    y: 18
                    text: qsTr("NEW FOLLOWER")
                    color: "#ffb3d6"
                    font.pixelSize: 16
                    font.weight: Font.ExtraBold
                    font.letterSpacing: 1.4
                }

                T {
                    x: 96
                    y: 42
                    text: "pixel_nova"
                    font.pixelSize: 30
                    font.weight: Font.ExtraBold
                }
            }
        }

        // Go live: one button
        Item {
            id: goLive
            readonly property real k: F.pop(film.t, film.tStream + 0.6, 10, 6.5)
            x: film.tall ? 192 : 100
            y: film.tall ? 1208 : 300
            width: 300
            height: 230
            opacity: F.seg(film.t, film.tStream + 0.6, film.tStream + 0.85) * streamScene.gone
            scale: (0.85 + 0.15 * k) * (film.tall ? 1.15 : 1)

            Glass {
                anchors.fill: parent
                lit: 0.6 * F.decay(film.t, streamScene.tLive, 1.6)
                tint: film.pink
            }

            T {
                x: 28
                y: 24
                text: qsTr("Your stream")
                color: film.soft
                font.pixelSize: 20
                font.weight: Font.ExtraBold
            }

            T {
                x: 28
                y: 52
                text: "Twitch · 1080p60"
                font.pixelSize: 24
                font.weight: Font.ExtraBold
            }

            Rectangle {
                x: 28
                y: 112
                width: 244
                height: 78
                radius: 39
                scale: 1 - 0.07 * F.bump(film.t, streamScene.tLive - 0.14, 0.32)
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: F.lerpColor(Qt.rgba(122 / 255, 60 / 255, 245 / 255, 1), Qt.rgba(233 / 255, 25 / 255, 22 / 255, 1), streamScene.live) }
                    GradientStop { position: 1.0; color: F.lerpColor(Qt.rgba(201 / 255, 45 / 255, 123 / 255, 1), Qt.rgba(200 / 255, 20 / 255, 60 / 255, 1), streamScene.live) }
                }

                Row {
                    anchors.centerIn: parent
                    spacing: 12

                    C.Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "broadcast"
                        size: 30
                        color: "white"
                    }

                    T {
                        anchors.verticalCenter: parent.verticalCenter
                        text: streamScene.live > 0.5 ? qsTr("You are live") : qsTr("Go live")
                        font.pixelSize: 26
                        font.weight: Font.ExtraBold
                    }
                }
            }

            // The press goes out in a ring
            Round {
                readonly property real out: F.outCubic(F.seg(film.t, streamScene.tLive, streamScene.tLive + 0.7))
                visible: film.t > streamScene.tLive && film.t < streamScene.tLive + 0.7
                size: 90 + 320 * out
                x: 150 - size / 2
                y: 151 - size / 2
                color: "transparent"
                border.width: 4 * (1 - out) + 0.5
                border.color: Qt.rgba(1, 0.4, 0.5, 0.8 * (1 - out))
            }
        }

        // What comes with it
        Chip { x: film.tall ? 595 : 1520; y: film.tall ? 1175 : 262; zoom: film.tall ? 1.1 : 1; iconName: "layers"; text: qsTr("Overlay for OBS"); tint: film.cMika; at: film.tStream + 3.6; until: streamScene.until }
        Chip { x: film.tall ? 595 : 1520; y: film.tall ? 1253 : 338; zoom: film.tall ? 1.1 : 1; iconName: "heart"; text: qsTr("Twitch alerts"); tint: film.cAva; at: film.tStream + 3.9; until: streamScene.until + 0.04 }
        Chip { x: film.tall ? 595 : 1520; y: film.tall ? 1331 : 414; zoom: film.tall ? 1.1 : 1; iconName: "monitor"; text: qsTr("Stream scenes"); tint: film.cLeo; at: film.tStream + 4.2; until: streamScene.until + 0.08 }
        Chip { x: film.tall ? 595 : 1520; y: film.tall ? 1409 : 490; zoom: film.tall ? 1.1 : 1; iconName: "gamepad"; text: qsTr("Viewer actions"); tint: film.cYou; at: film.tStream + 4.5; until: streamScene.until + 0.12 }

        Caption {
            head: qsTr("Made for")
            accent: qsTr("streaming.")
            line: qsTr("Go live on Twitch from the app, or put the overlay into OBS.")
            from: film.tStream + 0.7
            to: film.tEnd - 0.1
            headY: 792
        }
    }

    // ------------------------------------------------------------ the end: two doors

    /** Which of the end's three choices has the keys: 0 Host, 1 Join, 2 Watch again; -1 none. */
    property int endFocus: -1
    /** The end has arrived: its choices can be taken. */
    readonly property bool atEnd: t >= tEnd + 1.5
    signal hostChosen()
    signal joinChosen()
    signal againChosen()

    Item {
        id: endScene
        visible: film.t > film.tEnd + 0.3

        Letters {
            x: film.midX - implicitWidth / 2
            y: film.tall ? 540 : 428
            text: qsTr("Ready when you are.")
            size: 84
            from: "#ffffff"
            to: "#e7dcff"
            at: film.tEnd + 0.75
            each: 0.028
        }

        // Host: the door in the brand's colours, as on the start page
        Item {
            id: hostDoor
            readonly property real k: F.pop(film.t, film.tEnd + 1.25, 9, 6)
            // Wide: the two doors side by side. Tall: one under the other, larger
            x: film.tall ? 305 : 470
            y: film.tall ? 732 : 584
            width: 470
            height: 212
            opacity: F.seg(film.t, film.tEnd + 1.25, film.tEnd + 1.5)
            scale: (0.86 + 0.14 * k) * (hostArea.pressed ? 0.98 : 1) * (film.tall ? 1.3 : 1)

            Rectangle {
                anchors.fill: parent
                radius: 28
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#6a32e8" }
                    GradientStop { position: 0.45; color: "#8a36d6" }
                    GradientStop { position: 1.0; color: "#c02f7d" }
                }
            }

            Rectangle {
                anchors.fill: parent
                radius: 28
                color: Qt.rgba(1, 1, 1, hostArea.containsMouse ? 0.08 : 0)
                border.width: 1.5
                border.color: Qt.rgba(1, 1, 1, 0.22)
            }

            Rectangle {
                x: 30
                y: 30
                width: 60
                height: 60
                radius: 18
                color: Qt.rgba(1, 1, 1, 0.16)

                C.Icon {
                    anchors.centerIn: parent
                    iconName: "broadcast"
                    size: 32
                    color: "white"
                }
            }

            T {
                x: 108
                y: 38
                text: qsTr("Host a game")
                font.pixelSize: 36
                font.weight: Font.ExtraBold
            }

            T {
                x: 30
                y: 114
                width: 410
                wrapMode: Text.WordWrap
                lineHeight: 1.15
                text: qsTr("Share your console or a game on this computer, and invite your friends.")
                color: Qt.rgba(1, 1, 1, 0.9)
                font.pixelSize: 22
                font.weight: Font.Medium
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -7
                radius: 35
                color: "transparent"
                border.width: 3
                border.color: film.cyan
                visible: film.endFocus === 0 && film.atEnd
            }

            MouseArea {
                id: hostArea
                anchors.fill: parent
                enabled: film.atEnd
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: film.hostChosen()
            }
        }

        // Join: the dark door
        Item {
            id: joinDoor
            readonly property real k: F.pop(film.t, film.tEnd + 1.4, 9, 6)
            x: film.tall ? 305 : 980
            y: film.tall ? 1032 : 584
            width: 470
            height: 212
            opacity: F.seg(film.t, film.tEnd + 1.4, film.tEnd + 1.65)
            scale: (0.86 + 0.14 * k) * (joinArea.pressed ? 0.98 : 1) * (film.tall ? 1.3 : 1)

            Rectangle {
                anchors.fill: parent
                radius: 28
                color: joinArea.containsMouse ? "#1e1940" : "#17132e"
                border.width: 1.5
                border.color: Qt.rgba(124 / 255, 134 / 255, 1, 0.45)
            }

            Rectangle {
                x: 30
                y: 30
                width: 60
                height: 60
                radius: 18
                color: Qt.rgba(124 / 255, 134 / 255, 1, 0.18)

                C.Icon {
                    anchors.centerIn: parent
                    iconName: "enter"
                    size: 32
                    color: "#a9b0ff"
                }
            }

            T {
                x: 108
                y: 38
                text: qsTr("Join a friend")
                font.pixelSize: 36
                font.weight: Font.ExtraBold
            }

            T {
                x: 30
                y: 114
                width: 410
                wrapMode: Text.WordWrap
                lineHeight: 1.15
                text: qsTr("Got an invite? Paste it and you are in. Or find a public room.")
                color: film.soft
                font.pixelSize: 22
                font.weight: Font.Medium
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -7
                radius: 35
                color: "transparent"
                border.width: 3
                border.color: film.cyan
                visible: film.endFocus === 1 && film.atEnd
            }

            MouseArea {
                id: joinArea
                anchors.fill: parent
                enabled: film.atEnd
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: film.joinChosen()
            }
        }

        // Once more from the start
        Item {
            id: again
            visible: !film.promo
            x: film.midX - width / 2
            y: film.tall ? 1330 : 844
            width: againRow.width + 44
            height: 56
            opacity: F.enter(film.t, film.tEnd + 1.9, 0.6)

            Rectangle {
                anchors.fill: parent
                radius: 28
                color: Qt.rgba(1, 1, 1, againArea.containsMouse ? 0.1 : 0.05)
                border.width: 1.5
                border.color: Qt.rgba(1, 1, 1, 0.14)
            }

            Row {
                id: againRow
                anchors.centerIn: parent
                spacing: 10

                C.Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "refresh"
                    size: 22
                    color: film.soft
                }

                T {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Watch again")
                    color: film.ink
                    font.pixelSize: 22
                    font.weight: Font.ExtraBold
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -6
                radius: 34
                color: "transparent"
                border.width: 3
                border.color: film.cyan
                visible: film.endFocus === 2 && film.atEnd
            }

            MouseArea {
                id: againArea
                anchors.fill: parent
                enabled: film.atEnd
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: film.againChosen()
            }
        }
    }

    // ------------------------------------------------------------ the title

    // The icon: put together in the middle, then it waits in the corner for the end
    /**
     * Where the icon waits with the name while the film runs: the stage's top left corner.
     * Tall: the middle of its top, over what is said, the two together in the middle.
     */
    readonly property real cornerSize: tall ? 52 : 68
    readonly property real cornerX: tall ? midX - (cornerSize + 14 + markName.width) / 2 + cornerSize / 2 : 112
    readonly property real cornerY: tall ? 280 : 92
    readonly property real iconX: F.track(t, [[2.0, midX], [2.7, midX], [4.25, midX], [5.15, cornerX], [tEnd - 0.2, cornerX], [tEnd + 0.9, midX]])
    readonly property real iconY: tall
        ? F.track(t, [[2.0, 730], [2.7, 650], [4.25, 650], [5.15, cornerY], [tEnd - 0.2, cornerY], [tEnd + 0.9, 400]])
        : F.track(t, [[2.0, 470], [2.7, 372], [4.25, 372], [5.15, cornerY], [tEnd - 0.2, cornerY], [tEnd + 0.9, 286]])
    readonly property real iconSize: tall
        ? F.track(t, [[2.0, 400], [2.7, 320], [4.25, 320], [5.15, cornerSize], [tEnd - 0.2, cornerSize], [tEnd + 0.9, 200]])
        : F.track(t, [[2.0, 400], [2.7, 300], [4.25, 300], [5.15, cornerSize], [tEnd - 0.2, cornerSize], [tEnd + 0.9, 200]])
    /** When the two rings meet. */
    readonly property real tLink: 1.2

    // The light the icon stands in, and the flash when the rings link
    Halo {
        x: film.iconX - width / 2
        y: film.iconY - height / 2
        width: film.iconSize * 3.4
        height: width
        tint: Qt.rgba(140 / 255, 82 / 255, 1, 0.30 * F.seg(film.t, 0.2, 1.4) + 0.45 * F.decay(film.t, film.tLink, 3.2))
        opacity: F.seg(film.iconSize, 70, 180)
        visible: film.iconSize > 70
    }

    // A ring of light going out from where they linked, and sparks
    Item {
        id: linkBurst
        readonly property real k: film.t - film.tLink
        visible: k > 0 && k < 1.6
        x: film.iconX
        y: film.iconY - film.iconSize * 0.25

        Rectangle {
            readonly property real r: 60 + 520 * F.outCubic(F.seg(linkBurst.k, 0, 1.1))
            x: -r
            y: -r
            width: 2 * r
            height: 2 * r
            radius: r
            color: "transparent"
            border.width: 3 * (1 - F.seg(linkBurst.k, 0, 1.1)) + 0.5
            border.color: Qt.rgba(0.8, 0.9, 1, 0.55 * (1 - F.seg(linkBurst.k, 0, 1.1)))
        }

        Repeater {
            model: 18

            Rectangle {
                required property int index
                readonly property real angle: index / 18 * 2 * Math.PI + F.hash(index) * 0.5
                readonly property real reach: (150 + 260 * F.hash(index + 40)) * (1 - Math.exp(-3.2 * linkBurst.k))
                readonly property real s: (3 + 5 * F.hash(index + 80)) * (1 - F.seg(linkBurst.k, 0.2, 1.3))
                x: Math.cos(angle) * reach - s / 2
                y: Math.sin(angle) * reach - s / 2 + 60 * linkBurst.k * linkBurst.k
                width: s
                height: s
                radius: s / 2
                color: index % 2 ? film.cyan : "#c9a8ff"
                opacity: 1 - F.seg(linkBurst.k, 0.5, 1.4)
            }
        }
    }

    IntroIcon {
        id: appIcon
        size: film.iconSize
        x: film.iconX - size / 2
        y: film.iconY - size / 2
        // Two rings fly in from the sides, pass each other a little and link
        ringsApart: (1 - F.outBack(F.seg(film.t, 0.3, film.tLink + 0.25), 2.2)) * 620
        ringsOpacity: F.seg(film.t, 0.3, 0.7)
        link: F.seg(film.t, film.tLink + 0.05, film.tLink + 0.2)
        // Then the controller comes up under them, and the plate closes behind
        pad: F.seg(film.t, film.tLink + 0.1, film.tLink + 0.5)
        padDrop: (1 - F.outBack(F.seg(film.t, film.tLink + 0.1, film.tLink + 0.75), 1.4)) * 150
        plate: F.outCubic(F.seg(film.t, film.tLink + 0.35, film.tLink + 0.95))
    }

    // The name next to the icon once it sits in the corner
    T {
        id: markName
        x: film.cornerX + film.cornerSize / 2 + 14
        y: film.cornerY - height / 2
        text: "PSRemotePlayTogether"
        color: "#cdb6ff"
        font.pixelSize: film.tall ? 25 : 27
        font.weight: Font.ExtraBold
        opacity: F.during(film.t, 5.0, film.tEnd - 0.1, 0.6, 0.4) * 0.9
    }

    Letters {
        x: film.midX - implicitWidth / 2
        y: film.tall ? 850 : 572
        text: "PSRemotePlayTogether"
        size: film.tall ? 76 : 112
        at: 2.4
        until: 4.2
    }

    T {
        readonly property real k: F.enter(film.t, 3.3, 0.7)
        x: film.midX - width / 2
        y: (film.tall ? 960 : 730) + (1 - k) * 22 - (1 - F.leave(film.t, 4.2, 0.4)) * 16
        text: qsTr("Play your games together, from anywhere.")
        color: film.soft
        font.pixelSize: 42
        font.weight: Font.DemiBold
        opacity: k * F.leave(film.t, 4.2, 0.4)
    }
}
