import QtQuick
import QtQuick.Shapes

import "Film.js" as F

// The game the intro film plays: a small runner made for it, 960 x 540. A little explorer
// runs across a far planet and jumps for rings of light whenever somebody presses: the film
// says when (`jumps`), and who pressed colours the jump. Like the rest of the film it is
// drawn from the time alone.
Item {
    id: game

    /** The film's time in seconds. */
    property real t: 0
    /** [{ t, by }], times rising: when the explorer jumps, and which player pressed (an index into `colors`). */
    property var jumps: []
    /** One colour for each player (colours, not their names). */
    property var colors: [Qt.rgba(140 / 255, 82 / 255, 1, 1)]
    /** Which world it is: 0, 1 or 2, and anything between while one turns into the next (3 is 0 again). */
    property real world: 0
    /** A word in the corner, such as what the game runs on; nothing if empty. */
    property string tag: ""
    property string family: ""

    readonly property real speed: 300
    readonly property real groundY: 432
    readonly property real heroX: 250
    readonly property real jumpTime: 0.56
    readonly property real jumpHeight: 150
    /** The rings caught before the film looks. */
    readonly property int ringsBefore: 7

    /** The last jump that has started, -1 before the first. */
    readonly property int lastIndex: {
        let last = -1;
        for (let i = 0; i < jumps.length; ++i) {
            if (jumps[i].t <= t)
                last = i;
            else
                break;
        }
        return last;
    }
    readonly property real sinceJump: lastIndex >= 0 ? t - jumps[lastIndex].t : 99
    /** 0 to 1 while in the air. */
    readonly property real airK: sinceJump / jumpTime
    readonly property bool inAir: airK >= 0 && airK < 1
    readonly property real lift: inAir ? 4 * jumpHeight * airK * (1 - airK) : 0
    readonly property real sinceLanding: sinceJump - jumpTime
    readonly property int caught: {
        let n = 0;
        for (let i = 0; i < jumps.length; ++i) {
            if (jumps[i].t + jumpTime / 2 <= t)
                n++;
            else
                break;
        }
        return n;
    }
    readonly property real sinceCatch: caught > 0 ? t - jumps[caught - 1].t - jumpTime / 2 : 99
    /** The colour of whoever pressed last, fading back to the explorer's own. */
    readonly property color glowColor: lastIndex >= 0
        ? F.lerpColor(colorOf(jumps[lastIndex].by), cyan, F.seg(sinceJump, 0.5, 1.3)) : cyan
    readonly property color cyan: "#00f2ff"

    function colorOf(index) {
        const c = colors[Math.max(0, Math.min(colors.length - 1, index))];
        return Qt.rgba(c.r, c.g, c.b, 1);
    }

    /** One colour for each world, as [r, g, b] up to 255: the one for now. After the third world comes the first again. */
    function pal(a, b, c) {
        const from = world <= 1 ? a : world <= 2 ? b : c;
        const to = world <= 1 ? b : world <= 2 ? c : a;
        const k = world <= 1 ? world : world <= 2 ? world - 1 : Math.min(1, world - 2);
        return Qt.rgba((from[0] + (to[0] - from[0]) * k) / 255, (from[1] + (to[1] - from[1]) * k) / 255,
                       (from[2] + (to[2] - from[2]) * k) / 255, 1);
    }

    readonly property color skyTop: pal([0x12, 0x0b, 0x38], [0x05, 0x14, 0x2c], [0x07, 0x1a, 0x12])
    readonly property color skyMid: pal([0x3d, 0x1e, 0x80], [0x0d, 0x3d, 0x6b], [0x10, 0x4d, 0x36])
    readonly property color skyLow: pal([0xc2, 0x46, 0x9c], [0x1f, 0xb5, 0xba], [0x8f, 0xd9, 0x6a])
    readonly property color farColor: pal([0x33, 0x1b, 0x72], [0x0c, 0x33, 0x5e], [0x0f, 0x42, 0x30])
    readonly property color nearColor: pal([0x1f, 0x12, 0x4f], [0x08, 0x22, 0x44], [0x0a, 0x2d, 0x21])
    readonly property color groundColor: pal([0x10, 0x0a, 0x2b], [0x05, 0x12, 0x26], [0x06, 0x18, 0x12])
    readonly property color edgeColor: pal([0xf0, 0x46, 0x9b], [0x00, 0xf2, 0xff], [0xb6, 0xf0, 0x5a])
    readonly property color sunTop: pal([0xff, 0xd7, 0x8a], [0xd6, 0xff, 0xf6], [0xf8, 0xff, 0xb8])
    readonly property color sunLow: pal([0xf0, 0x46, 0x9b], [0x2a, 0xb9, 0xd6], [0x62, 0xc7, 0x54])

    /** A skyline that repeats after 960: `peaks` sharp or round, between `low` and `high`. */
    function ridge(seed, steps, high, low, sharp) {
        let points = [Qt.point(0, 540)];
        for (let i = 0; i <= steps; ++i) {
            const u = i / steps;
            let h = 0;
            for (let f = 1; f <= 4; ++f) {
                const wave = Math.sin(2 * Math.PI * (f + (f > 1 ? 1 : 0)) * u + F.hash(seed + f) * 6.28);
                h += (sharp ? 1 - 2 * Math.abs(wave) : wave) * F.hash(seed + 10 + f) / f;
            }
            points.push(Qt.point(u * 960, F.mix(low, high, 0.5 + 0.42 * h)));
        }
        points.push(Qt.point(960, 540));
        points.push(Qt.point(0, 540));
        return points;
    }

    width: 960
    height: 540
    clip: true

    component Sky: Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: game.skyTop }
            GradientStop { position: 0.5; color: game.skyMid }
            GradientStop { position: 0.8; color: game.skyLow }
        }
    }

    component Round: Rectangle {
        property real size: 10
        width: size
        height: size
        radius: size / 2
    }

    /** A skyline, twice side by side, passing at a part of the ground's speed. */
    component Skyline: Item {
        id: skyline
        property var points: []
        property color fill: "black"
        property real pace: 0.2
        readonly property real shift: (game.t * game.speed * pace) % 960
        width: 960
        height: 540

        Repeater {
            model: 2

            Shape {
                required property int index
                x: index * 960 - skyline.shift

                ShapePath {
                    strokeWidth: 0
                    strokeColor: "transparent"
                    fillColor: skyline.fill
                    PathPolyline { path: skyline.points }
                }
            }
        }
    }

    Sky { }

    // Stars, only high up
    Repeater {
        model: 34

        Round {
            required property int index
            size: 2 + 3 * F.hash(index + 500)
            x: F.hash(index + 600) * 960
            y: F.hash(index + 700) * 250
            color: "white"
            opacity: (0.25 + 0.6 * F.hash(index + 800)) * (0.6 + 0.4 * Math.sin(game.t * (1 + 2 * F.hash(index + 900)) + index))
        }
    }

    // The planet over the horizon, with its ring: the back half, the planet, the front half
    Item {
        id: planet
        x: 668
        y: 236 + 5 * Math.sin(game.t * 0.5)

        Shape {
            x: -260
            y: -260
            width: 520
            height: 520

            ShapePath {
                strokeWidth: 0
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: 260
                    centerY: 260
                    focalX: 260
                    focalY: 260
                    centerRadius: 260
                    GradientStop { position: 0.3; color: Qt.rgba(game.sunLow.r, game.sunLow.g, game.sunLow.b, 0.5) }
                    GradientStop { position: 1.0; color: Qt.rgba(game.sunLow.r, game.sunLow.g, game.sunLow.b, 0) }
                }
                PathRectangle { width: 520; height: 520 }
            }
        }

        Shape {
            rotation: -16
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: Qt.rgba(1, 1, 1, 0.35)
                strokeWidth: 7
                fillColor: "transparent"
                PathAngleArc { radiusX: 196; radiusY: 44; startAngle: 180; sweepAngle: 180 }
            }
        }

        Rectangle {
            x: -112
            y: -112
            width: 224
            height: 224
            radius: 112
            gradient: Gradient {
                GradientStop { position: 0.0; color: game.sunTop }
                GradientStop { position: 1.0; color: game.sunLow }
            }
        }

        Shape {
            rotation: -16
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: Qt.rgba(1, 1, 1, 0.8)
                strokeWidth: 7
                fillColor: "transparent"
                PathAngleArc { radiusX: 196; radiusY: 44; startAngle: 0; sweepAngle: 180 }
            }
        }
    }

    Skyline {
        points: game.ridge(11, 48, 215, 420, true)
        fill: game.farColor
        pace: 0.1
    }

    Skyline {
        points: game.ridge(37, 64, 318, 440, false)
        fill: game.nearColor
        pace: 0.32
    }

    // The ground, lit along its edge, with marks that show how fast it goes
    Rectangle {
        y: game.groundY
        width: 960
        height: 540 - game.groundY
        color: game.groundColor
    }

    Rectangle {
        y: game.groundY
        width: 960
        height: 26
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(game.edgeColor.r, game.edgeColor.g, game.edgeColor.b, 0.42) }
            GradientStop { position: 1.0; color: Qt.rgba(game.edgeColor.r, game.edgeColor.g, game.edgeColor.b, 0) }
        }
    }

    Rectangle {
        y: game.groundY - 1.5
        width: 960
        height: 3
        color: game.edgeColor
    }

    Repeater {
        model: 9

        Rectangle {
            required property int index
            readonly property real along: (index * 150 + F.hash(index + 30) * 90 - game.t * game.speed) % 1350
            x: (along < 0 ? along + 1350 : along) - 200
            y: game.groundY + 30 + (index % 3) * 22
            width: 46 + 60 * F.hash(index + 60)
            height: 4
            radius: 2
            color: Qt.rgba(1, 1, 1, 0.07 + 0.05 * (index % 3))
        }
    }

    // What there is to jump over and to catch: a crystal in the way, a ring of light above it
    Repeater {
        model: game.jumps

        Item {
            id: hurdle
            required property var modelData
            required property int index
            readonly property real meet: modelData.t + game.jumpTime / 2
            readonly property real since: game.t - meet
            x: game.heroX + game.speed * (meet - game.t)
            visible: x > -80 && x < 1040

            // The crystal
            Rectangle {
                x: -15
                y: game.groundY - 21
                width: 30
                height: 30
                rotation: 45
                radius: 5
                color: Qt.rgba(game.edgeColor.r, game.edgeColor.g, game.edgeColor.b, 0.9)

                Rectangle {
                    x: 5
                    y: 5
                    width: 9
                    height: 9
                    radius: 2
                    color: Qt.rgba(1, 1, 1, 0.75)
                }
            }

            Rectangle {
                x: -40
                y: game.groundY - 3
                width: 80
                height: 7
                radius: 3.5
                color: game.groundColor
            }

            // The ring: waits bobbing, then goes out wide when it is caught
            Item {
                y: game.groundY - game.jumpHeight - 30 + (hurdle.since < 0 ? 6 * Math.sin(game.t * 4 + hurdle.index) : 0)

                Round {
                    readonly property real out: F.outCubic(F.seg(hurdle.since, 0, 0.42))
                    size: 44 + 90 * out
                    x: -size / 2
                    y: -size / 2
                    color: "transparent"
                    border.width: 8 * (1 - out) + 0.5
                    border.color: hurdle.since < 0 ? game.cyan : game.colorOf(hurdle.modelData.by)
                    opacity: 1 - F.seg(hurdle.since, 0.1, 0.42)
                }

                Round {
                    size: 12
                    x: -6
                    y: -6
                    color: "white"
                    visible: hurdle.since < 0
                }

                // Sparks, in the colour of whoever caught it
                Repeater {
                    model: hurdle.since >= 0 && hurdle.since < 0.7 ? 8 : 0

                    Round {
                        required property int index
                        readonly property real angle: index / 8 * 6.283 + hurdle.index
                        readonly property real reach: (46 + 50 * F.hash(index + hurdle.index * 9)) * (1 - Math.exp(-6 * hurdle.since))
                        size: 9 * (1 - F.seg(hurdle.since, 0.15, 0.7))
                        x: Math.cos(angle) * reach - size / 2
                        y: Math.sin(angle) * reach - size / 2 + 160 * hurdle.since * hurdle.since
                        color: game.colorOf(hurdle.modelData.by)
                    }
                }

                Text {
                    visible: hurdle.since >= 0 && hurdle.since < 0.9
                    x: 26
                    y: -34 - 46 * F.outCubic(F.seg(hurdle.since, 0, 0.9))
                    text: "+1"
                    color: "white"
                    font.family: game.family
                    font.pixelSize: 30
                    font.weight: Font.ExtraBold
                    opacity: 1 - F.seg(hurdle.since, 0.5, 0.9)
                }
            }
        }
    }

    // The explorer's shadow: smaller the higher the jump
    Rectangle {
        readonly property real k: 1 - game.lift / 420
        x: game.heroX - 30 * k
        y: game.groundY + 5
        width: 60 * k
        height: 10
        radius: 5
        color: Qt.rgba(0, 0, 0, 0.38)
    }

    // Dust where the jump began and where it ended
    Repeater {
        model: 2

        Item {
            id: dust
            required property int index
            readonly property real since: index === 0 ? game.sinceJump : game.sinceLanding
            visible: since >= 0 && since < 0.45
            // It stays behind on the ground while the explorer runs on
            x: game.heroX - 24 - game.speed * Math.max(0, since)
            y: game.groundY - 6

            Repeater {
                model: 4

                Round {
                    required property int index
                    size: (10 + 7 * index) * (0.5 + F.seg(dust.since, 0, 0.45))
                    x: -index * 15 - size / 2 + 20
                    y: -size / 2 - 16 * F.seg(dust.since, 0, 0.45) * (index % 2 ? 1 : 0.4)
                    color: Qt.rgba(1, 1, 1, 0.34 * (1 - F.seg(dust.since, 0.05, 0.45)))
                }
            }
        }
    }

    // A ring of the presser's colour going out from the explorer when the jump starts
    Round {
        readonly property real out: F.outCubic(F.seg(game.sinceJump, 0, 0.5))
        visible: game.sinceJump >= 0 && game.sinceJump < 0.5
        size: 70 + 150 * out
        x: game.heroX - size / 2
        y: game.groundY - 30 - game.lift - size / 2
        color: "transparent"
        border.width: 5 * (1 - out) + 0.5
        border.color: game.glowColor
        opacity: 0.9 * (1 - out)
    }

    // What the explorer leaves in the air
    Repeater {
        model: game.inAir ? 5 : 0

        Round {
            required property int index
            readonly property real before: Math.max(0, game.airK - 0.07 * (index + 1))
            size: 22 - 3.4 * index
            x: game.heroX - 16 * (index + 1) - size / 2 - 6
            y: game.groundY - 30 - 4 * game.jumpHeight * before * (1 - before) - size / 2
            color: Qt.rgba(game.glowColor.r, game.glowColor.g, game.glowColor.b, 0.34 - 0.06 * index)
        }
    }

    // The explorer: tall when leaving the ground, flat for a moment when landing
    Item {
        id: hero
        readonly property real stretch: game.inAir ? 0.16 * Math.sin(Math.PI * Math.min(1, game.airK * 1.6))
                                                   : -0.24 * F.decay(game.sinceLanding, 0, 13) * (game.sinceLanding >= 0 ? 1 : 0)
        /** One step of the run, -1 to 1. */
        readonly property real step: Math.sin(game.t * 19)
        x: game.heroX - 30
        y: game.groundY - 60 - game.lift - (game.inAir ? 0 : 3 * Math.abs(step))
        width: 60
        height: 60
        transform: Scale {
            origin.x: 30
            origin.y: 60
            xScale: 1 - hero.stretch * 0.7
            yScale: 1 + hero.stretch
        }

        // Feet
        Rectangle {
            x: 8 + (game.inAir ? 2 : 5 * hero.step)
            y: 54 - (game.inAir ? 5 : Math.max(0, 5 * hero.step))
            width: 18
            height: 10
            radius: 5
            color: "#9a7cf0"
        }

        Rectangle {
            x: 34 - (game.inAir ? 2 : 5 * hero.step)
            y: 54 - (game.inAir ? 5 : Math.max(0, -5 * hero.step))
            width: 18
            height: 10
            radius: 5
            color: "#9a7cf0"
        }

        // The light on its head, in the colour of whoever plays it right now
        Rectangle {
            x: 28.5
            y: -13
            width: 3
            height: 16
            color: "#d9ccff"
        }

        Round {
            size: 34
            x: 13
            y: -37
            color: Qt.rgba(game.glowColor.r, game.glowColor.g, game.glowColor.b, 0.3)
        }

        Round {
            size: 14
            x: 23
            y: -27
            color: game.glowColor
        }

        // The body
        Rectangle {
            width: 60
            height: 58
            radius: 21
            gradient: Gradient {
                GradientStop { position: 0.0; color: "#ffffff" }
                GradientStop { position: 1.0; color: "#cdb8ff" }
            }
        }

        // The visor and the eyes, which look ahead and blink now and then
        Rectangle {
            x: 13
            y: 14
            width: 42
            height: 24
            radius: 12
            color: "#1b1242"

            Repeater {
                model: 2

                Rectangle {
                    required property int index
                    readonly property real blink: (game.t % 2.9) < 0.12 ? 0.15 : 1
                    x: 13 + index * 14
                    y: 7 + 5 * (1 - blink)
                    width: 7
                    height: 10 * blink
                    radius: 3.5
                    color: game.glowColor
                }
            }
        }
    }

    // How many rings so far: counts up with a little jump of its own
    Rectangle {
        x: 28
        y: 26
        width: countRow.width + 40
        height: 52
        radius: 26
        color: Qt.rgba(0.04, 0.03, 0.12, 0.55)
        border.width: 1.5
        border.color: Qt.rgba(1, 1, 1, 0.16)

        Row {
            id: countRow
            x: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12

            Round {
                anchors.verticalCenter: parent.verticalCenter
                size: 26
                color: "transparent"
                border.width: 5
                border.color: game.cyan
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: String(game.ringsBefore + game.caught)
                color: "white"
                font.family: game.family
                font.pixelSize: 30
                font.weight: Font.ExtraBold
                scale: 1 + 0.35 * F.decay(game.sinceCatch, 0, 9) * (game.sinceCatch >= 0 ? 1 : 0)
            }
        }
    }

    // What it runs on
    Rectangle {
        visible: game.tag.length > 0
        x: 960 - width - 28
        y: 26
        width: tagText.width + 36
        height: 52
        radius: 26
        color: Qt.rgba(0.04, 0.03, 0.12, 0.55)
        border.width: 1.5
        border.color: Qt.rgba(1, 1, 1, 0.16)

        Text {
            id: tagText
            anchors.centerIn: parent
            text: game.tag
            color: "white"
            font.family: game.family
            font.pixelSize: 26
            font.weight: Font.ExtraBold
        }
    }
}
