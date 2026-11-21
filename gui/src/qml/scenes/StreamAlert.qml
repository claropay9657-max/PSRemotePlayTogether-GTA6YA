import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

import org.streetpea.chiaking

import "../controls" as C

// The alert on top of the stream (Chiaki.alerts.current), 1920 x 1080 like the scenes, at
// the top centre with "N more coming up" under it. An alert with something to read (a
// resub's or a cheer's message, a reward's text) is the wide card of the design "Alert on
// stream"; every other one is its kind's own card of "All alerts", with its emblem and
// colour. The same cards are the OBS page /alerts (res/overlay/alerts.js). Names and
// messages come from viewers: plain text only.
Item {
    id: root
    width: 1920
    height: 1080

    readonly property var a: Chiaki.alerts ? Chiaki.alerts.current : ({})
    readonly property bool up: !!a.kind
    readonly property color c1: a.color || "#8c52ff"
    readonly property color c2: a.color2 || "#f0469b"
    readonly property color ink: a.ink || "#d8c6ff"
    readonly property string face: a.face || "initial"
    /** The wide card ("Alert on stream"), or the kind's own card ("All alerts"). */
    readonly property bool wide: !!a.wide

    FontLoader {
        id: jakarta
        source: "qrc:/fonts/PlusJakartaSans.ttf"
    }
    readonly property string family: jakarta.status === FontLoader.Ready ? jakarta.font.family : ""

    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a);
    }

    component T: Text {
        color: "#f3f1ff"
        font.family: root.family
        font.weight: Font.ExtraBold
        textFormat: Text.PlainText
        elide: Text.ElideRight
        maximumLineCount: 1
    }

    /** A four-pointed sparkle of the design, around (cx, cy). */
    component Sparkle: ShapePath {
        property real cx
        property real cy
        property real r
        strokeColor: "transparent"
        PathSvg {
            path: "M" + cx + " " + (cy - r) + " l" + (r * 0.28) + " " + (r * 0.72) + " l" + (r * 0.72) + " " + (r * 0.28)
                  + " l" + (-r * 0.72) + " " + (r * 0.28) + " l" + (-r * 0.28) + " " + (r * 0.72) + " l" + (-r * 0.28) + " " + (-r * 0.72)
                  + " l" + (-r * 0.72) + " " + (-r * 0.28) + " l" + (r * 0.72) + " " + (-r * 0.28) + " z"
        }
    }

    component Icon: Image {
        property string name
        property int size: 84
        width: size
        height: size
        sourceSize: Qt.size(size * 2, size * 2)
        source: name ? "qrc:/twitch/" + name + ".svg" : ""
    }

    // Darker behind the card, so it reads on any game picture
    Rectangle {
        visible: root.up
        opacity: card.opacity
        width: parent.width
        height: 620
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(8 / 255, 6 / 255, 20 / 255, 0.62) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }

    // Sparkles around the card
    Shape {
        visible: root.up
        opacity: card.opacity
        width: 1920
        height: 620
        Sparkle { cx: 430; cy: 145; r: 25; fillColor: "#ffd6e8" }
        Sparkle { cx: 470; cy: 344; r: 14; fillColor: Qt.rgba(184 / 255, 249 / 255, 1, 0.8) }
        Sparkle { cx: 1488; cy: 127; r: 31; fillColor: "#efe7ff" }
        Sparkle { cx: 1530; cy: 317; r: 17; fillColor: Qt.rgba(253 / 255, 228 / 255, 168 / 255, 0.85) }
        Sparkle { cx: 1420; cy: 431; r: 11; fillColor: Qt.rgba(1, 214 / 255, 232 / 255, 0.7) }
        Sparkle { cx: 520; cy: 460; r: 10; fillColor: Qt.rgba(239 / 255, 231 / 255, 1, 0.6) }
    }

    Item {
        id: card
        x: root.wide ? 470 : (1920 - 440) / 2
        y: root.up ? 64 : 24
        width: root.wide ? 980 : 440
        height: root.wide ? frame.height : setFrame.height
        opacity: root.up ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 220 } }
        Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutBack } }

        // The gradient frame of the design, with a glow in the kind's colour
        Rectangle {
            id: frame
            visible: root.wide
            width: parent.width
            height: body.implicitHeight + 40 + 34 + 6
            radius: 44
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#00f2ff" }
                GradientStop { position: 0.38; color: "#8c52ff" }
                GradientStop { position: 0.72; color: "#f0469b" }
                GradientStop { position: 1.0; color: "#f5b942" }
            }
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: root.alpha(root.c1, 0.42)
                shadowBlur: 1.0
                shadowScale: 1.04
                shadowVerticalOffset: 10
            }

            Rectangle {
                id: inner
                anchors.fill: parent
                anchors.margins: 3
                radius: 41
                color: "#120f27"
                clip: true

                // Glows in the kind's two colours (the design's radial gradients)
                Glow {
                    x: 176 - 700
                    y: -700
                    size: 1400
                    squash: 300 / 700
                    color: root.alpha(root.c1, 0.30)
                }
                Glow {
                    x: parent.width - 600
                    y: parent.height - 600
                    size: 1200
                    squash: 260 / 600
                    color: root.alpha(root.c2, 0.18)
                }

                // The two rings of the app's icon, faint, in the corner
                Shape {
                    x: parent.width - 270
                    y: -20
                    opacity: 0.12
                    ShapePath {
                        strokeColor: "white"
                        strokeWidth: 16
                        fillColor: "transparent"
                        PathAngleArc { centerX: 110; centerY: 100; radiusX: 78; radiusY: 78; startAngle: 0; sweepAngle: 360 }
                    }
                    ShapePath {
                        strokeColor: "white"
                        strokeWidth: 16
                        fillColor: "transparent"
                        PathAngleArc { centerX: 196; centerY: 100; radiusX: 78; radiusY: 78; startAngle: 0; sweepAngle: 360 }
                    }
                }

                Column {
                    id: body
                    x: 48
                    y: 40
                    width: parent.width - 96
                    spacing: 24

                    Row {
                        width: parent.width
                        spacing: 36

                        // Who it is about: the viewer's initial with the emblem as a badge,
                        // the emblem itself, or the hype train's level
                        Item {
                            id: faceBox
                            width: 176
                            height: 176

                            Rectangle {
                                anchors.fill: parent
                                radius: 88
                                gradient: Gradient {
                                    orientation: Gradient.Vertical
                                    GradientStop { position: 0.0; color: root.c1 }
                                    GradientStop { position: 1.0; color: root.c2 }
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 6
                                    radius: width / 2
                                    gradient: Gradient {
                                        GradientStop { position: 0.0; color: root.face === "initial" ? "#3a2a7a" : "#1d1838" }
                                        GradientStop { position: 1.0; color: root.face === "initial" ? "#1a1436" : "#120f27" }
                                    }

                                    T {
                                        visible: root.face === "initial"
                                        anchors.centerIn: parent
                                        text: root.a.initial || "?"
                                        font.pixelSize: 76
                                        color: "#efe7ff"
                                    }

                                    Icon {
                                        visible: root.face === "emblem"
                                        anchors.centerIn: parent
                                        name: root.face === "emblem" ? (root.a.icon || "") : ""
                                        size: 104
                                    }

                                    Column {
                                        visible: root.face === "level"
                                        anchors.centerIn: parent
                                        spacing: 4
                                        T {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            font.pixelSize: 18
                                            font.letterSpacing: 18 * 0.2
                                            color: "#ffd6e8"
                                            text: qsTr("LEVEL")
                                        }
                                        C.GradientText {
                                            anchors.horizontalCenter: parent.horizontalCenter
                                            from: "#f0469b"
                                            to: "#f5b942"
                                            font.family: root.family
                                            font.pixelSize: 76
                                            font.weight: Font.ExtraBold
                                            font.letterSpacing: -3
                                            text: root.a.level || 1
                                        }
                                    }
                                }
                            }

                            // The badge: the sub star with the months, or the kind's emblem on a dark disc
                            Item {
                                visible: root.face === "initial"
                                x: parent.width - 70
                                y: parent.height - 78
                                width: 84
                                height: 84

                                Rectangle {
                                    visible: root.a.icon !== "star-badge"
                                    anchors.fill: parent
                                    radius: 42
                                    color: "#120f27"
                                    border.width: 4
                                    border.color: root.c1
                                }
                                Icon {
                                    anchors.centerIn: parent
                                    name: root.face === "initial" ? (root.a.icon || "") : ""
                                    size: root.a.icon === "star-badge" ? 84 : 50
                                }
                                T {
                                    visible: root.a.icon === "star-badge" && (root.a.count || 0) > 0
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    y: 22
                                    font.pixelSize: (root.a.count || 0) > 99 ? 20 : 28
                                    text: root.a.count || ""
                                }
                                // Gifts: how many
                                Rectangle {
                                    visible: root.a.kind === "gift" && (root.a.count || 0) > 0
                                    x: parent.width - width + 18
                                    y: -16
                                    height: 38
                                    width: giftCount.implicitWidth + 22
                                    radius: 19
                                    color: "#f0469b"
                                    border.width: 3
                                    border.color: "#120f27"
                                    T {
                                        id: giftCount
                                        anchors.centerIn: parent
                                        font.pixelSize: 22
                                        text: "×" + (root.a.count || 0)
                                    }
                                }
                            }
                        }

                        Column {
                            width: parent.width - 176 - 36
                            anchors.verticalCenter: faceBox.verticalCenter
                            spacing: 10

                            Row {
                                spacing: 14
                                T {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: root.a.label || ""
                                    font.pixelSize: 22
                                    font.letterSpacing: 22 * 0.24
                                    font.capitalization: Font.AllUppercase
                                    color: root.ink
                                }
                                Rectangle {
                                    visible: !!root.a.chip
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: 34
                                    width: chipText.implicitWidth + 28
                                    radius: 17
                                    color: root.alpha(root.c1, 0.22)
                                    border.width: 1
                                    border.color: root.alpha(root.c1, 0.6)
                                    T {
                                        id: chipText
                                        anchors.centerIn: parent
                                        text: root.a.chip || ""
                                        font.pixelSize: 18
                                        color: "#efe7ff"
                                    }
                                }
                            }

                            // The big line: bits, a reward, a question, or the viewer's name
                            Row {
                                visible: !!root.a.big
                                spacing: 14
                                T {
                                    id: bigText
                                    text: root.a.big || ""
                                    font.pixelSize: 92
                                    font.letterSpacing: -3
                                    color: "#ffd27a"
                                }
                                T {
                                    anchors.baseline: bigText.baseline
                                    text: root.a.bigUnit || ""
                                    font.pixelSize: 30
                                    color: root.ink
                                }
                            }
                            T {
                                visible: !root.a.big && !root.a.titleGradient
                                width: parent.width
                                text: root.a.title || root.a.name || ""
                                font.pixelSize: (root.a.title || "").length > 28 ? 56 : 92
                                font.letterSpacing: -3
                                lineHeight: 0.98
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                            }
                            C.GradientText {
                                visible: !!root.a.titleGradient
                                width: Math.min(implicitWidth, parent.width)
                                from: "#a77bff"
                                to: "#f0469b"
                                font.family: root.family
                                font.pixelSize: 92
                                font.weight: Font.ExtraBold
                                font.letterSpacing: -3
                                text: root.a.titleGradient ? (root.a.title || "") : ""
                            }

                            Row {
                                spacing: 16
                                topPadding: 4

                                // A number: raid viewers, a donation
                                T {
                                    id: statText
                                    visible: !!root.a.stat
                                    text: root.a.stat || ""
                                    font.pixelSize: 44
                                    color: Qt.lighter(root.c1, 1.25)
                                }
                                T {
                                    visible: !!root.a.stat
                                    anchors.baseline: statText.baseline
                                    width: Math.min(implicitWidth, 560)
                                    text: root.a.statUnit || ""
                                    font.pixelSize: 22
                                    font.weight: Font.Bold
                                    color: root.ink
                                }
                                T {
                                    visible: text.length > 0 && !root.a.stat
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.min(implicitWidth, (root.a.bars || 0) > 0 ? 420 : 640)
                                    text: root.a.detail || ""
                                    font.pixelSize: 26
                                    font.weight: Font.Bold
                                    color: "#cfc9ec"
                                }

                                // A resub's months, as bars
                                Row {
                                    visible: (root.a.bars || 0) > 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 5
                                    Repeater {
                                        model: 12
                                        Rectangle {
                                            required property int index
                                            width: 14
                                            height: 22
                                            radius: 4
                                            color: index < (root.a.bars || 0) ? Qt.tint("#c4a6ff", Qt.rgba(240 / 255, 70 / 255, 155 / 255, index / 12)) : Qt.rgba(1, 1, 1, 0.10)
                                        }
                                    }
                                }

                                // A goal's progress
                                Rectangle {
                                    visible: root.a.progress !== undefined && root.a.progress >= 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 260
                                    height: 16
                                    radius: 8
                                    color: Qt.rgba(1, 1, 1, 0.10)
                                    Rectangle {
                                        width: Math.max(16, parent.width * (root.a.progress || 0))
                                        height: parent.height
                                        radius: 8
                                        gradient: Gradient {
                                            orientation: Gradient.Horizontal
                                            GradientStop { position: 0.0; color: root.c1 }
                                            GradientStop { position: 1.0; color: root.c2 }
                                        }
                                    }
                                }
                            }

                            // A hype train's five steps
                            Row {
                                id: steps
                                visible: root.a.segments !== undefined
                                spacing: 6
                                readonly property int level: root.a.level || 1
                                readonly property int first: Math.max(1, level - 2)
                                Repeater {
                                    model: 5
                                    Rectangle {
                                        required property int index
                                        readonly property int step: steps.first + index
                                        readonly property real fill: step <= steps.level ? 1 : (step === steps.level + 1 ? (root.a.segments || 0) : 0)
                                        width: 96
                                        height: 16
                                        radius: 8
                                        color: Qt.rgba(1, 1, 1, 0.10)
                                        clip: true
                                        Rectangle {
                                            visible: parent.fill > 0
                                            width: Math.max(16, parent.width * parent.fill)
                                            height: 16
                                            radius: 8
                                            gradient: Gradient {
                                                orientation: Gradient.Horizontal
                                                GradientStop { position: 0; color: Qt.tint("#f0469b", Qt.rgba(245 / 255, 165 / 255, 90 / 255, index * 0.22)) }
                                                GradientStop { position: 1; color: Qt.tint("#f36a86", Qt.rgba(245 / 255, 165 / 255, 90 / 255, (index + 1) * 0.22)) }
                                            }
                                        }
                                    }
                                }
                            }

                            T {
                                visible: !!root.a.note
                                text: root.a.note || ""
                                font.pixelSize: 22
                                font.weight: Font.DemiBold
                                color: "#b9b4d6"
                            }
                        }
                    }

                    // What the viewer wrote
                    Item {
                        visible: !!root.a.message
                        x: 212
                        width: parent.width - 212
                        height: bubble.height

                        Rectangle {
                            x: 34
                            y: -9
                            width: 20
                            height: 20
                            rotation: 45
                            radius: 3
                            color: bubble.color
                        }

                        Rectangle {
                            id: bubble
                            width: parent.width
                            height: messageText.implicitHeight + 40
                            radius: 24
                            color: root.a.messageTint ? "#3a2f2a" : "#2a2350"

                            T {
                                id: messageText
                                x: 28
                                y: 20
                                width: parent.width - 56
                                wrapMode: Text.WordWrap
                                maximumLineCount: 3
                                lineHeight: 1.1
                                text: root.a.message || ""
                                font.pixelSize: 30
                                font.weight: Font.DemiBold
                            }
                        }
                    }

                    // How long it stays
                    Rectangle {
                        width: parent.width
                        height: 6
                        radius: 3
                        color: Qt.rgba(1, 1, 1, 0.08)

                        Rectangle {
                            width: parent.width * (Chiaki.alerts ? Chiaki.alerts.left : 0)
                            height: 6
                            radius: 3
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: "#00f2ff" }
                                GradientStop { position: 0.45; color: "#8c52ff" }
                                GradientStop { position: 1.0; color: "#f0469b" }
                            }
                        }
                    }
                }
            }
        }

        // The kind's own card of "All alerts": its emblem large on top, then what happened
        Rectangle {
            id: setFrame
            visible: !root.wide
            width: 440
            height: setColumn.implicitHeight + 24 + 26
            radius: 30
            color: "#120f27"
            border.width: 1.5
            border.color: root.alpha(root.c1, 0.55)
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: root.alpha(root.c1, 0.35)
                shadowBlur: 1.0
                shadowVerticalOffset: 8
            }

            Item {
                anchors.fill: parent
                anchors.margins: 1.5
                clip: true
                Glow {
                    x: 218 - 360
                    y: -360
                    size: 720
                    squash: 220 / 360
                    color: root.alpha(root.c1, root.a.kind === "follow" ? 0.22 : 0.28)
                }
            }

            Column {
                id: setColumn
                x: 28
                y: 24
                width: parent.width - 56
                spacing: 8

                // The emblem: large, with its number; a hype train's level
                Item {
                    width: parent.width
                    height: 132

                    Column {
                        visible: root.a.kind === "hype"
                        anchors.centerIn: parent
                        spacing: 12
                        T {
                            anchors.horizontalCenter: parent.horizontalCenter
                            font.pixelSize: 18
                            font.letterSpacing: 18 * 0.2
                            color: "#ffd6e8"
                            text: qsTr("LEVEL")
                        }
                        C.GradientText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            from: "#f0469b"
                            to: "#f5b942"
                            font.family: root.family
                            font.pixelSize: 76
                            font.weight: Font.ExtraBold
                            font.letterSpacing: -3
                            text: root.a.level || 1
                        }
                    }

                    Icon {
                        id: bigEmblem
                        visible: root.a.kind !== "hype"
                        anchors.centerIn: parent
                        // The kinds without an emblem of their own in the design show their element's icon
                        size: String(root.a.emblem || "").endsWith("-big") ? 132 : 104
                        name: root.a.kind !== "hype" ? (root.a.emblem || "") : ""
                    }

                    // A resub: how far into the year of months, as a ring
                    Shape {
                        visible: root.a.kind === "resub"
                        x: (parent.width - 132) / 2
                        width: 132
                        height: 132
                        ShapePath {
                            strokeColor: "#a77bff"
                            strokeWidth: 7 * 1.1
                            capStyle: ShapePath.RoundCap
                            fillColor: "transparent"
                            PathAngleArc {
                                centerX: 66; centerY: 66; radiusX: 59.4; radiusY: 59.4; startAngle: -90
                                sweepAngle: 360 * Math.max(1, Math.min(12, root.a.bars || 1)) / 12
                            }
                        }
                    }

                    // The number on a sub's star, a resub's months
                    T {
                        visible: (root.a.kind === "sub" || root.a.kind === "resub") && (root.a.count || 0) > 0
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: root.a.kind === "sub" ? 46 : 47
                        font.pixelSize: (root.a.count || 0) > 99 ? 24 : 30
                        text: root.a.count || ""
                    }

                    // Gift subs: how many
                    Rectangle {
                        visible: root.a.kind === "gift" && (root.a.count || 0) > 0
                        x: parent.width / 2 + 66 + 10 - width
                        y: 0
                        height: 44
                        width: giftTimes.implicitWidth + 24
                        radius: 22
                        color: "#f0469b"
                        T {
                            id: giftTimes
                            anchors.centerIn: parent
                            font.pixelSize: 24
                            text: "×" + (root.a.count || 0)
                        }
                    }
                }

                T {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 16
                    font.letterSpacing: 16 * 0.22
                    font.capitalization: Font.AllUppercase
                    color: root.ink
                    text: root.a.setLabel || root.a.label || ""
                }

                // Bits: how many
                Row {
                    visible: !!root.a.big
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 10
                    T {
                        id: setBig
                        font.pixelSize: 52
                        font.letterSpacing: -1.5
                        color: "#ffd27a"
                        text: root.a.big || ""
                    }
                    T {
                        anchors.baseline: setBig.baseline
                        font.pixelSize: 22
                        color: root.ink
                        text: root.a.bigUnit || ""
                    }
                }

                // Who, or what (a reward, a question)
                T {
                    visible: !root.a.big && root.a.kind !== "hype" && !root.a.titleGradient
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    lineHeight: 1.05
                    font.pixelSize: (root.a.title || root.a.name || "").length > 18 ? 30 : 40
                    font.letterSpacing: -0.8
                    text: root.a.title || root.a.name || ""
                }
                C.GradientText {
                    visible: !!root.a.titleGradient
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(implicitWidth, parent.width)
                    from: "#a77bff"
                    to: "#f0469b"
                    font.family: root.family
                    font.pixelSize: 44
                    font.weight: Font.ExtraBold
                    font.letterSpacing: -0.9
                    text: root.a.titleGradient && !root.wide ? (root.a.title || "") : ""
                }

                // A number: raid viewers, a donation
                Row {
                    visible: !!root.a.stat
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 10
                    T {
                        id: setStat
                        font.pixelSize: 44
                        color: Qt.lighter(root.c1, 1.25)
                        text: root.a.stat || ""
                    }
                    T {
                        anchors.baseline: setStat.baseline
                        width: Math.min(implicitWidth, setColumn.width - setStat.width - 10)
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        color: root.ink
                        text: root.a.statUnit || ""
                    }
                }

                // A hype train's five steps
                Row {
                    id: setSteps
                    visible: root.a.segments !== undefined
                    width: parent.width
                    spacing: 6
                    readonly property int level: root.a.level || 1
                    readonly property int first: Math.max(1, level - 2)
                    Repeater {
                        model: 5
                        Rectangle {
                            required property int index
                            readonly property int step: setSteps.first + index
                            readonly property real fill: step <= setSteps.level ? 1 : (step === setSteps.level + 1 ? (root.a.segments || 0) : 0)
                            width: (setSteps.width - 24) / 5
                            height: 16
                            radius: 8
                            color: Qt.rgba(1, 1, 1, 0.10)
                            clip: true
                            Rectangle {
                                visible: parent.fill > 0
                                width: Math.max(16, parent.width * parent.fill)
                                height: 16
                                radius: 8
                                gradient: Gradient {
                                    orientation: Gradient.Horizontal
                                    GradientStop { position: 0; color: Qt.tint("#f0469b", Qt.rgba(245 / 255, 165 / 255, 90 / 255, index * 0.22)) }
                                    GradientStop { position: 1; color: Qt.tint("#f36a86", Qt.rgba(245 / 255, 165 / 255, 90 / 255, (index + 1) * 0.22)) }
                                }
                            }
                        }
                    }
                }

                // What it means: "Welcome to the room!", "from sunny_k", "72 % to level 4"
                T {
                    readonly property bool strong: ["bits", "points", "hype"].indexOf(root.a.kind) >= 0
                    visible: !root.a.stat && text.length > 0
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    font.pixelSize: root.a.kind === "bits" ? 22 : 20
                    font.weight: strong ? Font.Bold : Font.DemiBold
                    color: strong ? "#f3f1ff" : "#b9b4d6"
                    text: root.a.detail || ""
                }
                T {
                    visible: !!root.a.note
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                    color: "#b9b4d6"
                    text: root.a.note || ""
                }

                Item { width: 1; height: 10 }

                // The card's bar in its colour: how long it stays
                Rectangle {
                    width: parent.width
                    height: 5
                    radius: 3
                    color: Qt.rgba(1, 1, 1, 0.08)
                    Rectangle {
                        width: parent.width * (Chiaki.alerts ? Chiaki.alerts.left : 0)
                        height: 5
                        radius: 3
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: root.a.bar1 || root.c1 }
                            GradientStop { position: 1; color: root.a.bar2 || root.c1 }
                        }
                    }
                }
            }
        }

        // More waiting: their initials (or emblems), and how many
        Rectangle {
            visible: (Chiaki.alerts ? Chiaki.alerts.waiting : 0) > 0
            anchors.horizontalCenter: parent.horizontalCenter
            y: card.height + 18
            height: 46
            width: moreRow.implicitWidth + 30
            radius: 23
            color: Qt.rgba(8 / 255, 6 / 255, 20 / 255, 0.72)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.12)

            Row {
                id: moreRow
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: -10
                    Repeater {
                        model: Chiaki.alerts ? Chiaki.alerts.next : []
                        Rectangle {
                            required property var modelData
                            width: 30
                            height: 30
                            radius: 15
                            color: modelData.face === "initial" ? root.alpha(Qt.color(modelData.color || "#cdb6ff"), 0.9) : "#120f27"
                            border.width: 2
                            border.color: "#120f27"
                            T {
                                visible: parent.modelData.face === "initial"
                                anchors.centerIn: parent
                                font.pixelSize: 14
                                color: "#120f27"
                                text: parent.modelData.initial || ""
                            }
                            Icon {
                                visible: parent.modelData.face !== "initial"
                                anchors.centerIn: parent
                                size: 20
                                name: parent.modelData.face !== "initial" ? (parent.modelData.icon || "") : ""
                            }
                        }
                    }
                }

                T {
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 19
                    font.weight: Font.Bold
                    color: "#e7e4fa"
                    text: {
                        const n = Chiaki.alerts ? Chiaki.alerts.waiting : 0;
                        return n === 1 ? qsTr("1 more coming up") : qsTr("%1 more coming up").arg(n);
                    }
                }
            }
        }
    }

    /** A soft glow of one colour fading out, as wide as size and squashed in height. */
    component Glow: Shape {
        id: glow
        property color color
        property real size: 1000
        property real squash: 1
        width: size
        height: size
        transform: Scale { origin.y: glow.size / 2; yScale: glow.squash }
        ShapePath {
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: glow.size / 2
                centerY: glow.size / 2
                centerRadius: glow.size / 2
                focalX: glow.size / 2
                focalY: glow.size / 2
                GradientStop { position: 0; color: glow.color }
                GradientStop { position: 0.7; color: Qt.rgba(glow.color.r, glow.color.g, glow.color.b, 0) }
            }
            PathRectangle { x: 0; y: 0; width: glow.size; height: glow.size }
        }
    }
}
