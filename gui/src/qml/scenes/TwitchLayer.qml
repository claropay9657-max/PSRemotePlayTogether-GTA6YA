import QtQuick
import QtQuick.Effects
import QtQuick.Shapes

import "../controls" as C

import org.streetpea.chiaking

// The Twitch elements (Chiaki.twitchElements, twitchelements.h) on the game picture, after
// the designs "Twitch elements on the game", "Latest and counters", "Live events", "Three
// looks" and "Switch on, arrange". Laid out on a 1920 x 1080 stage that fills the space it
// gets, like an OBS browser source: the places are the same here, in the app's own stream
// and on the OBS overlay page (res/overlay/twitch.js, which draws the same elements).
//
// While arranging every switched-on element shows, with made-up data where nothing real
// came in yet (a "latest" one without data shows where it will be), and can be dragged.
// Names and messages are viewers' text: always plain text.
Item {
    id: board

    property bool editing: false
    /** Something to show right now: whoever holds this goes by it. */
    readonly property bool shown: editing ? anyOn : (active && anyWanted)

    readonly property var tw: Chiaki.twitchElements
    readonly property var cfg: tw ? tw.values : ({})
    readonly property bool active: !!tw && tw.active
    readonly property var real: tw ? tw.data : ({})
    readonly property var fake: tw ? tw.sample : ({})
    // The look of the display on the game picture (Overlays & OBS), for these elements too
    readonly property string look: Chiaki.hud && (Chiaki.hud.values.style === "glass" || Chiaki.hud.values.style === "solid") ? Chiaki.hud.values.style : "transparent"
    readonly property bool plain: look === "transparent"
    readonly property bool joined: !!cfg.uptime_joined

    // The designs' colours
    readonly property color ink: "#f3f1ff"
    readonly property color soft: "#b9b4d6"
    readonly property color boxColor: plain ? "transparent" : (look === "solid" ? "#10101a" : Qt.rgba(14 / 255, 12 / 255, 30 / 255, 0.74))
    readonly property color boxBorder: plain ? "transparent" : (look === "solid" ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.13))
    readonly property color track: plain ? Qt.rgba(0, 0, 0, 0.45) : Qt.rgba(1, 1, 1, 0.12)

    /** The clock for countdowns and "2 min ago", ticking while something shows. */
    property double now: Date.now()
    Timer {
        interval: 1000
        repeat: true
        running: board.shown && board.visible
        triggeredOnStart: true
        onTriggered: board.now = Date.now()
    }

    FontLoader {
        id: jakarta
        source: "qrc:/fonts/PlusJakartaSans.ttf"
    }
    readonly property string family: jakarta.status === FontLoader.Ready ? jakarta.font.family : ""

    // ------------------------------------------------------------ data

    /** Real data of an element; while arranging made-up data where there is none. */
    function pick(key) {
        const v = real[key];
        if (v !== undefined && v !== null && !(Array.isArray(v) && v.length === 0))
            return v;
        return editing ? fake[key] : undefined;
    }
    /** Whether pick(key) gave the made-up data (its times are spans, not moments). */
    function isFake(key) {
        const v = real[key];
        return !(v !== undefined && v !== null && !(Array.isArray(v) && v.length === 0));
    }
    /** Milliseconds until a moment (or the span itself for made-up data). */
    function until(t, made_up) {
        return made_up ? t : t - now;
    }
    function num(n) {
        return Number(n || 0).toLocaleString(Qt.locale("en_US"), "f", 0);
    }
    function compact(n) {
        n = Number(n || 0);
        if (n >= 1e6)
            return (n / 1e6).toFixed(1) + "M";
        if (n >= 1e4)
            return (n / 1e3).toFixed(1) + "K";
        return num(n);
    }
    function clock(ms) {
        const s = Math.max(0, Math.floor(ms / 1000));
        const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, sec = s % 60;
        const two = (v) => (v < 10 ? "0" : "") + v;
        return h > 0 ? h + ":" + two(m) + ":" + two(sec) : m + ":" + two(sec);
    }
    function ago(ms) {
        const s = Math.max(0, Math.floor(ms / 1000));
        if (s < 60)
            return [qsTr("now"), ""];
        if (s < 3600)
            return [qsTr("%1 min").arg(Math.floor(s / 60)), qsTr("ago")];
        if (s < 86400)
            return [qsTr("%1 h").arg(Math.floor(s / 3600)), qsTr("ago")];
        return [qsTr("%1 d").arg(Math.floor(s / 86400)), qsTr("ago")];
    }

    // Every element and whether it has something; "wanted": switched on and with something
    readonly property var places: [follower, subscriber, gift, bits, raid, redemption, topcheer, session, viewers, uptime,
        goalFollow, goalSub, hype, poll, prediction, charity, adbreak, recent, chat]
    readonly property bool anyOn: places.some((p) => p.on)
    readonly property bool anyWanted: places.some((p) => p.wanted)

    // ------------------------------------------------------------ building blocks

    component T: Text {
        color: board.ink
        font.family: board.family
        font.weight: Font.ExtraBold
        textFormat: Text.PlainText
        elide: Text.ElideRight
        maximumLineCount: 1
    }

    /** The small caps line on top of an element: "LATEST FOLLOWER". */
    component Caps: T {
        font.pixelSize: 13
        font.letterSpacing: 13 * 0.18
        font.capitalization: Font.AllUppercase
    }

    component Icon: Image {
        property string name
        property int size: 34
        width: size
        height: size
        sourceSize: Qt.size(size * 2, size * 2)
        source: name ? "qrc:/twitch/" + name + ".svg" : ""
        smooth: true
    }

    /** An element's box in the chosen look. */
    component Box: Rectangle {
        color: board.boxColor
        border.width: board.plain ? 0 : 1
        border.color: board.boxBorder
        radius: 18
    }

    /** A progress bar: track and a glowing fill. */
    component Bar: Rectangle {
        id: bar
        property real value: 0
        property color from: "#8ff7ff"
        property color to: "#00f2ff"
        height: 14
        radius: height / 2
        color: board.track
        clip: true
        Rectangle {
            width: Math.max(bar.height, bar.width * Math.max(0, Math.min(1, bar.value)))
            visible: bar.value > 0
            height: bar.height
            radius: bar.height / 2
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: bar.from }
                GradientStop { position: 1; color: bar.to }
            }
        }
    }

    /**
     * One element: switched on with "<key>_show", placed with "<key>_x"/"<key>_y" (fractions
     * of the room left, 32 from the edges), sized with "<key>_scale". While arranging it gets
     * a dashed frame with its name and can be dragged.
     */
    component Placed: Item {
        id: el
        property string key
        property string title
        /** Real data to show. */
        property bool hasContent: false
        /** Shown while arranging (made-up data or the empty place). */
        property bool canArrange: true
        default property alias content: holder.data
        /** Extra tools in its frame: [{label, action}]; a click sends tool(action). */
        property var tools: []
        signal tool(string action)

        readonly property bool on: !!board.cfg[key + "_show"] && canArrange
        readonly property bool wanted: on && hasContent && board.active
        readonly property real size: Math.max(0.4, Math.min(2.5, (board.cfg[key + "_scale"] || 100) / 100))
        property real dragX: -1
        property real dragY: -1

        visible: board.editing ? on : wanted
        width: Math.max(1, holder.childrenRect.width * size)
        height: Math.max(1, holder.childrenRect.height * size)
        x: dragX >= 0 ? dragX : Math.round(32 + (board.cfg[key + "_x"] || 0) * Math.max(0, 1920 - 64 - width))
        y: dragY >= 0 ? dragY : Math.round(32 + (board.cfg[key + "_y"] || 0) * Math.max(0, 1080 - 64 - height))

        function save() {
            const fx = Math.max(0, Math.min(1, (x - 32) / Math.max(1, 1920 - 64 - width)));
            const fy = Math.max(0, Math.min(1, (y - 32) / Math.max(1, 1080 - 64 - height)));
            board.tw.set(key + "_x", fx);
            board.tw.set(key + "_y", fy);
        }
        function resize(step) {
            board.tw.set(key + "_scale", Math.max(40, Math.min(250, Math.round(size * 100) + step)));
        }

        Item {
            id: holder
            scale: el.size
            transformOrigin: Item.TopLeft
            // Without boxes a soft shadow keeps everything readable on any game picture
            layer.enabled: board.plain
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "black"
                shadowOpacity: 0.95
                shadowBlur: 0.45
                shadowVerticalOffset: 1
                shadowHorizontalOffset: 0
            }
        }

        // ---- arranging: dashed frame, name, tools, dragging
        Shape {
            visible: board.editing
            x: -7
            y: -7
            width: el.width + 14
            height: el.height + 14
            ShapePath {
                strokeColor: mover.pressed ? "#ffffff" : Qt.rgba(0, 242 / 255, 1, 0.85)
                strokeWidth: 3
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 2]
                fillColor: mover.pressed ? Qt.rgba(0, 242 / 255, 1, 0.08) : "transparent"
                PathRectangle {
                    x: 1.5
                    y: 1.5
                    width: el.width + 11
                    height: el.height + 11
                    radius: 22
                }
            }
        }

        MouseArea {
            id: mover
            visible: board.editing
            enabled: board.editing
            anchors.fill: parent
            anchors.margins: -7
            hoverEnabled: true
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property real grabX: 0
            property real grabY: 0
            onPressed: (mouse) => {
                const p = mapToItem(stage, mouse.x, mouse.y);
                grabX = p.x - el.x;
                grabY = p.y - el.y;
                guides.dragging = el;
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return;
                const p = mapToItem(stage, mouse.x, mouse.y);
                let nx = Math.max(32, Math.min(1920 - 32 - el.width, p.x - grabX));
                let ny = Math.max(32, Math.min(1080 - 32 - el.height, p.y - grabY));
                const snapped = guides.snap(el, nx, ny);
                el.dragX = snapped.x;
                el.dragY = snapped.y;
            }
            onReleased: {
                guides.dragging = null;
                if (el.dragX < 0)
                    return;
                el.save();
                el.dragX = -1;
                el.dragY = -1;
            }
            onWheel: (wheel) => el.resize(wheel.angleDelta.y > 0 ? 10 : -10)
        }

        // Its name and tools: above the frame, or below it at the top edge
        Row {
            visible: board.editing
            readonly property bool below: el.y < 70
            x: Math.min(14, el.width - width)
            y: below ? el.height + 14 : -height - 2
            spacing: 6
            z: 2

            Rectangle {
                height: 32
                width: nameRow.implicitWidth + 28
                radius: 16
                color: "#7ff5ff"
                Row {
                    id: nameRow
                    anchors.centerIn: parent
                    spacing: 8
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "move"
                        size: 16
                    }
                    T {
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 18
                        color: "#04262a"
                        text: el.title
                    }
                }
            }

            Repeater {
                model: [{ label: "−", action: "smaller" }, { label: "+", action: "larger" }].concat(el.tools)
                Rectangle {
                    required property var modelData
                    height: 32
                    width: Math.max(32, toolText.implicitWidth + 22)
                    radius: 16
                    color: toolArea.containsMouse ? "#8c52ff" : Qt.rgba(8 / 255, 6 / 255, 20 / 255, 0.88)
                    border.width: 1
                    border.color: Qt.rgba(0, 242 / 255, 1, 0.5)
                    T {
                        id: toolText
                        anchors.centerIn: parent
                        font.pixelSize: 17
                        text: parent.modelData.label
                    }
                    MouseArea {
                        id: toolArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            const a = parent.modelData.action;
                            if (a === "smaller" || a === "larger")
                                el.resize(a === "larger" ? 10 : -10);
                            else
                                el.tool(a);
                        }
                    }
                }
            }
        }
    }

    /** "Latest …": an icon tile, the label and name, a number on the right. */
    component Latest: Item {
        id: lt
        property string icon
        property color tint
        property color labelColor
        property string label
        property string name
        property string value
        property color valueColor: "white"
        property string unit
        /** Nothing came in yet: the place it will be, while arranging. */
        property bool empty: false
        property string emptyText

        implicitWidth: 440
        implicitHeight: 72
        width: 440
        height: 72

        Box {
            anchors.fill: parent
            visible: !lt.empty
        }

        // The empty place: dashed, in the element's colour
        Shape {
            visible: lt.empty
            anchors.fill: parent
            ShapePath {
                strokeColor: Qt.rgba(lt.tint.r, lt.tint.g, lt.tint.b, 0.6)
                strokeWidth: 2
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                fillColor: Qt.rgba(14 / 255, 12 / 255, 30 / 255, 0.4)
                PathRectangle { x: 1; y: 1; width: lt.width - 2; height: lt.height - 2; radius: 18 }
            }
        }

        Rectangle {
            id: tile
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 52
            height: 52
            radius: 14
            color: board.plain ? "transparent" : Qt.rgba(lt.tint.r, lt.tint.g, lt.tint.b, lt.empty ? 0.10 : 0.18)
            opacity: lt.empty ? 0.6 : 1
            Icon {
                anchors.centerIn: parent
                name: lt.icon
            }
        }

        Column {
            anchors.left: tile.right
            anchors.leftMargin: 14
            anchors.right: right.left
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Caps {
                width: parent.width
                color: lt.labelColor
                text: lt.label
            }
            T {
                width: parent.width
                font.pixelSize: lt.empty ? 19 : 24
                font.weight: lt.empty ? Font.DemiBold : Font.ExtraBold
                font.letterSpacing: lt.empty ? 0 : -0.24
                color: lt.empty ? "#9c97bd" : board.ink
                text: lt.empty ? lt.emptyText : lt.name
            }
        }

        Column {
            id: right
            visible: !lt.empty
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 1
            T {
                anchors.right: parent.right
                font.pixelSize: 24
                color: lt.valueColor
                text: lt.value
            }
            T {
                anchors.right: parent.right
                font.pixelSize: 13
                font.weight: Font.Bold
                color: board.soft
                text: lt.unit
            }
        }
    }

    /** A goal as a bar: follower goal, sub goal, charity. */
    component Goal: Item {
        id: g
        property string icon
        property color labelColor
        property string label
        property string amount
        property string target
        property real value
        property color from
        property color to
        property string text
        width: 620
        implicitWidth: 620
        implicitHeight: goalColumn.implicitHeight + 30
        height: implicitHeight

        Box {
            anchors.fill: parent
        }
        Column {
            id: goalColumn
            x: 20
            y: 14
            width: parent.width - 40
            spacing: 10
            Item {
                width: parent.width
                height: 30
                Icon {
                    id: goalIcon
                    anchors.verticalCenter: parent.verticalCenter
                    name: g.icon
                    size: 26
                }
                Caps {
                    anchors.left: goalIcon.right
                    anchors.leftMargin: 12
                    anchors.right: goalAmount.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 14
                    font.letterSpacing: 14 * 0.18
                    color: g.labelColor
                    text: g.label
                }
                Row {
                    id: goalAmount
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    T {
                        id: amountText
                        font.pixelSize: 24
                        text: g.amount
                    }
                    T {
                        anchors.baseline: amountText.baseline
                        visible: g.target.length > 0
                        font.pixelSize: 18
                        color: board.soft
                        text: "/ " + g.target
                    }
                }
            }
            Bar {
                width: parent.width
                value: g.value
                from: g.from
                to: g.to
            }
            T {
                width: parent.width
                font.pixelSize: 16
                font.weight: Font.DemiBold
                color: board.soft
                text: g.text
            }
        }
    }

    /** Viewers, uptime, the ad break: a pill. */
    component Pill: Box {
        height: 50
        radius: 25
    }

    // ------------------------------------------------------------ the stage

    Item {
        id: stage
        width: 1920
        height: 1080
        scale: Math.min(board.width / 1920, board.height / 1080)
        transformOrigin: Item.TopLeft
        x: (board.width - 1920 * scale) / 2
        y: (board.height - 1080 * scale) / 2

        // ---- latest

        Placed {
            id: follower
            key: "follower"
            title: qsTr("Latest follower")
            readonly property var v: board.real.follower
            hasContent: !!v
            Latest {
                readonly property var d: follower.v || {}
                readonly property var when: follower.v ? board.ago(board.now - d.at) : ["", ""]
                icon: "heart"
                tint: "#00f2ff"
                labelColor: "#8ff7ff"
                label: qsTr("Latest follower")
                name: d.name || ""
                value: when[0]
                unit: when[1]
                empty: !follower.v
                emptyText: qsTr("shows up with your first follower")
            }
        }

        Placed {
            id: subscriber
            key: "subscriber"
            title: qsTr("Latest subscriber")
            readonly property var v: board.real.subscriber
            hasContent: !!v
            Latest {
                readonly property var d: subscriber.v || {}
                icon: "star"
                tint: "#a77bff"
                labelColor: "#d8c6ff"
                label: qsTr("Latest subscriber")
                name: d.name || ""
                value: qsTr("%1 mo").arg(d.months || 1)
                valueColor: "#a77bff"
                unit: d.tier || ""
                empty: !subscriber.v
                emptyText: qsTr("shows up with your first sub")
            }
        }

        Placed {
            id: gift
            key: "gift"
            title: qsTr("Latest gift subs")
            readonly property var v: board.real.gift
            hasContent: !!v
            Latest {
                readonly property var d: gift.v || {}
                icon: "gift"
                tint: "#f0469b"
                labelColor: "#ffc3dd"
                label: qsTr("Latest gift subs")
                name: d.name || ""
                value: "×" + (d.count || 1)
                valueColor: "#f0469b"
                unit: d.total > 0 ? qsTr("%1 in total").arg(board.num(d.total)) : ""
                empty: !gift.v
                emptyText: qsTr("shows up with your first gift subs")
            }
        }

        Placed {
            id: bits
            key: "bits"
            title: qsTr("Latest bits")
            readonly property var v: board.real.bits
            hasContent: !!v
            Latest {
                readonly property var d: bits.v || {}
                icon: "gem"
                tint: "#f5b942"
                labelColor: "#fde4a8"
                label: qsTr("Latest bits")
                name: d.name || ""
                value: board.num(d.bits)
                valueColor: "#f5b942"
                unit: d.bits === 1 ? qsTr("bit") : qsTr("bits")
                empty: !bits.v
                emptyText: qsTr("shows up with your first cheer")
            }
        }

        Placed {
            id: raid
            key: "raid"
            title: qsTr("Latest raid")
            readonly property var v: board.real.raid
            hasContent: !!v
            Latest {
                readonly property var d: raid.v || {}
                icon: "raid"
                tint: "#34d399"
                labelColor: "#c6f6e2"
                label: qsTr("Latest raid")
                name: d.name || ""
                value: board.num(d.viewers)
                valueColor: "#34d399"
                unit: d.viewers === 1 ? qsTr("viewer") : qsTr("viewers")
                empty: !raid.v
                emptyText: qsTr("shows up with your first raid")
            }
        }

        Placed {
            id: redemption
            key: "redemption"
            title: qsTr("Latest redemption")
            readonly property var v: board.real.redemption
            hasContent: !!v
            Latest {
                readonly property var d: redemption.v || {}
                icon: "points"
                tint: "#c06cf0"
                labelColor: "#e2c8ff"
                label: qsTr("Latest redemption")
                name: (d.name || "") + (d.reward ? " · " + d.reward : "")
                value: d.cost > 0 ? board.num(d.cost) : ""
                valueColor: "#c06cf0"
                unit: d.cost > 0 ? qsTr("points") : ""
                empty: !redemption.v
                emptyText: qsTr("shows up with the first reward taken")
            }
        }

        // ---- counters

        Placed {
            id: topcheer
            key: "topcheer"
            title: qsTr("Top cheerer")
            readonly property var v: board.pick("topcheer")
            hasContent: !!board.real.topcheer
            Box {
                id: cheerBox
                width: 560
                height: 100
                radius: 20
                readonly property var d: topcheer.v || {}

                Item {
                    id: face
                    x: 20
                    anchors.verticalCenter: parent.verticalCenter
                    width: 68
                    height: 68
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: width / 2
                        color: "#f5b942"
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: "#2a2350"
                        clip: true
                        T {
                            anchors.centerIn: parent
                            font.pixelSize: 30
                            color: "#ffd27a"
                            text: (cheerBox.d.name || "?").charAt(0).toUpperCase()
                        }
                    }
                    // Only pictures from Twitch's own image servers (twitchelements.cpp)
                    Image {
                        id: avatar
                        anchors.fill: parent
                        visible: false
                        source: cheerBox.d.avatar || ""
                        sourceSize: Qt.size(136, 136)
                        fillMode: Image.PreserveAspectCrop
                    }
                    MultiEffect {
                        anchors.fill: parent
                        visible: avatar.status === Image.Ready
                        source: avatar
                        maskEnabled: true
                        maskSource: round
                    }
                    Rectangle {
                        id: round
                        anchors.fill: parent
                        radius: width / 2
                        visible: false
                        layer.enabled: true
                    }
                    Icon {
                        x: 18
                        y: -24
                        name: "crown"
                        size: 32
                    }
                }
                Column {
                    anchors.left: face.right
                    anchors.leftMargin: 16
                    anchors.right: cheerBits.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Caps {
                        width: parent.width
                        color: "#fde4a8"
                        text: cheerBox.d.stream === false ? qsTr("Top cheerer · all time") : qsTr("Top cheerer · this stream")
                    }
                    T {
                        width: parent.width
                        font.pixelSize: 28
                        text: cheerBox.d.name || ""
                    }
                }
                Row {
                    id: cheerBits
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "gem"
                        size: 26
                    }
                    T {
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 28
                        color: "#ffd27a"
                        text: board.num(cheerBox.d.bits)
                    }
                }
            }
        }

        Placed {
            id: session
            key: "session"
            title: qsTr("This stream")
            readonly property var v: board.pick("session")
            // Shows once the stream has its first follower, sub or bits
            hasContent: !!board.real.session && ((board.real.session.followers || 0) + (board.real.session.subs || 0) + (board.real.session.bits || 0)) > 0
            Box {
                id: sessionBox
                width: 560
                height: 128
                radius: 20
                readonly property var d: session.v || {}
                Caps {
                    x: 20
                    y: 16
                    color: board.soft
                    text: qsTr("This stream")
                }
                Row {
                    x: 20
                    y: 46
                    width: parent.width - 40
                    spacing: 10
                    Repeater {
                        model: [
                            { icon: "heart", tint: "#00f2ff", color: "#8ff7ff", n: "+" + board.num(sessionBox.d.followers), what: qsTr("followers") },
                            { icon: "star", tint: "#a77bff", color: "#d8c6ff", n: "+" + board.num(sessionBox.d.subs), what: qsTr("subs") },
                            { icon: "gem", tint: "#f5b942", color: "#fde4a8", n: board.num(sessionBox.d.bits), what: qsTr("bits") },
                        ]
                        Row {
                            required property var modelData
                            width: (560 - 40 - 20) / 3
                            spacing: 10
                            Rectangle {
                                width: 44
                                height: 44
                                radius: 12
                                color: board.plain ? "transparent" : Qt.rgba(Qt.color(parent.modelData.tint).r, Qt.color(parent.modelData.tint).g, Qt.color(parent.modelData.tint).b, 0.18)
                                Icon {
                                    anchors.centerIn: parent
                                    name: parent.parent.modelData.icon
                                    size: 28
                                }
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                T {
                                    font.pixelSize: 26
                                    color: parent.parent.modelData.color
                                    text: parent.parent.modelData.n
                                }
                                T {
                                    font.pixelSize: 14
                                    font.weight: Font.Bold
                                    color: board.soft
                                    text: parent.parent.modelData.what
                                }
                            }
                        }
                    }
                }
            }
        }

        Placed {
            id: viewers
            key: "viewers"
            title: board.joined && uptime.on ? qsTr("Viewers and uptime") : qsTr("Viewers")
            readonly property int count: board.real.viewers >= 0 ? board.real.viewers : (board.editing ? board.fake.viewers : -1)
            readonly property bool withUptime: board.joined && !!board.cfg.uptime_show
            hasContent: board.real.viewers >= 0 || (withUptime && board.real.since > 0)
            tools: board.cfg.uptime_show ? [{ label: board.joined ? qsTr("Split") : qsTr("Join"), action: "join" }] : []
            onTool: board.tw.set("uptime_joined", !board.joined)
            Pill {
                width: viewersRow.implicitWidth + 34
                Row {
                    id: viewersRow
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 14
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 12
                        height: 12
                        radius: 6
                        color: "#ef4444"
                        Rectangle {
                            anchors.centerIn: parent
                            width: 20
                            height: 20
                            radius: 10
                            color: Qt.rgba(239 / 255, 68 / 255, 68 / 255, 0.28)
                            z: -1
                        }
                    }
                    // Alone it says LIVE and shows a viewer
                    T {
                        visible: !viewers.withUptime
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 15
                        font.letterSpacing: 15 * 0.16
                        color: "#fecaca"
                        text: qsTr("LIVE")
                    }
                    Icon {
                        visible: !viewers.withUptime
                        anchors.verticalCenter: parent.verticalCenter
                        name: "viewer"
                        size: 20
                    }
                    Row {
                        visible: viewers.count >= 0
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: viewers.withUptime ? 8 : 12
                        T {
                            id: countText
                            font.pixelSize: 22
                            text: board.num(viewers.count)
                        }
                        T {
                            anchors.baseline: countText.baseline
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                            color: board.soft
                            text: qsTr("watching")
                        }
                    }
                    Rectangle {
                        visible: viewers.withUptime && viewers.count >= 0 && uptimeText.text.length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: 1
                        height: 24
                        color: Qt.rgba(1, 1, 1, 0.18)
                    }
                    Icon {
                        visible: viewers.withUptime && uptimeText.text.length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        name: "clock"
                        size: 20
                    }
                    T {
                        id: uptimeText
                        visible: viewers.withUptime && text.length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 22
                        font.features: ({ "tnum": 1 })
                        text: uptime.since > 0 ? board.clock(uptime.fakeSince ? uptime.since : board.now - uptime.since) : ""
                    }
                }
            }
        }

        Placed {
            id: uptime
            key: "uptime"
            title: qsTr("Uptime")
            readonly property bool fakeSince: !(board.real.since > 0)
            readonly property double since: board.real.since > 0 ? board.real.since : (board.editing ? board.fake.since : 0)
            // Joined: drawn in the viewers' pill (when that one is on)
            canArrange: !(board.joined && !!board.cfg.viewers_show)
            hasContent: board.real.since > 0
            tools: board.cfg.viewers_show ? [{ label: qsTr("Join"), action: "join" }] : []
            onTool: board.tw.set("uptime_joined", true)
            Pill {
                width: upRow.implicitWidth + 36
                Row {
                    id: upRow
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "clock"
                        size: 20
                    }
                    T {
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 22
                        font.features: ({ "tnum": 1 })
                        text: board.clock(uptime.fakeSince ? uptime.since : board.now - uptime.since)
                    }
                    T {
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: board.soft
                        text: qsTr("live")
                    }
                }
            }
        }

        // ---- live events

        Placed {
            id: goalFollow
            key: "goal_follow"
            title: qsTr("Follower goal")
            readonly property var v: board.pick("goal_follow")
            hasContent: !!board.real.goal_follow
            Goal {
                readonly property var d: goalFollow.v || {}
                icon: "target"
                labelColor: "#8ff7ff"
                label: d.reached ? qsTr("Follower goal reached!") : qsTr("Follower goal")
                amount: board.num(d.current)
                target: board.num(d.target)
                value: d.target > 0 ? d.current / d.target : 0
                from: "#8ff7ff"
                to: "#00f2ff"
                text: {
                    const left = Math.max(0, (d.target || 0) - (d.current || 0));
                    const what = left > 0 ? qsTr("%1 to go").arg(board.num(left)) : qsTr("Made it – thank you!");
                    return d.text ? what + " – " + d.text : what;
                }
            }
        }

        Placed {
            id: goalSub
            key: "goal_sub"
            title: qsTr("Sub goal")
            readonly property var v: board.pick("goal_sub")
            hasContent: !!board.real.goal_sub
            Goal {
                readonly property var d: goalSub.v || {}
                icon: "star"
                labelColor: "#d8c6ff"
                label: d.reached ? qsTr("Sub goal reached!") : qsTr("Sub goal")
                amount: board.num(d.current)
                target: board.num(d.target)
                value: d.target > 0 ? d.current / d.target : 0
                from: "#d8c6ff"
                to: "#8c52ff"
                text: {
                    const left = Math.max(0, (d.target || 0) - (d.current || 0));
                    const what = left > 0 ? qsTr("%1 to go").arg(board.num(left)) : qsTr("Made it – thank you!");
                    return d.text ? what + " – " + d.text : what;
                }
            }
        }

        Placed {
            id: charity
            key: "charity"
            title: qsTr("Charity")
            readonly property var v: board.pick("charity")
            hasContent: !!board.real.charity
            Goal {
                readonly property var d: charity.v || {}
                icon: "charity"
                labelColor: "#ffd1d9"
                label: d.name ? qsTr("Charity · %1").arg(d.name) : qsTr("Charity")
                amount: d.current || ""
                target: d.target || ""
                value: d.progress || 0
                from: "#fda4af"
                to: "#fb7185"
                text: qsTr("%1 % raised – thank you, all of you!").arg(Math.round((d.progress || 0) * 100))
            }
        }

        Placed {
            id: hype
            key: "hype"
            title: qsTr("Hype train")
            readonly property var v: board.pick("hype")
            readonly property bool made: board.isFake("hype")
            hasContent: !!board.real.hype
            Box {
                id: hypeBox
                readonly property var d: hype.v || {}
                readonly property int level: d.level || 1
                readonly property real part: d.goal > 0 ? Math.min(1, (d.progress || 0) / d.goal) : 0
                // Five steps: the levels reached full, the next one as far as it got
                readonly property int first: Math.max(1, level - 2)
                width: 480
                height: 116
                Rectangle {
                    id: levelBox
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    width: 84
                    height: 84
                    radius: 16
                    color: board.plain ? "transparent" : Qt.rgba(244 / 255, 122 / 255, 110 / 255, 0.16)
                    Column {
                        anchors.centerIn: parent
                        T {
                            anchors.horizontalCenter: parent.horizontalCenter
                            font.pixelSize: 12
                            font.letterSpacing: 12 * 0.2
                            color: "#ffd6c8"
                            text: qsTr("LEVEL")
                        }
                        C.GradientText {
                            anchors.horizontalCenter: parent.horizontalCenter
                            from: "#f0469b"
                            to: "#f5b942"
                            font.family: board.family
                            font.pixelSize: 52
                            font.weight: Font.ExtraBold
                            font.letterSpacing: -2
                            text: hypeBox.level
                        }
                    }
                }
                Column {
                    anchors.left: levelBox.right
                    anchors.leftMargin: 18
                    anchors.right: parent.right
                    anchors.rightMargin: 20
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 9
                    Item {
                        width: parent.width
                        height: 24
                        Icon {
                            id: bolt
                            anchors.verticalCenter: parent.verticalCenter
                            name: "hype"
                            size: 22
                        }
                        Caps {
                            anchors.left: bolt.right
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 14
                            font.letterSpacing: 14 * 0.18
                            color: "#ffd6c8"
                            text: qsTr("Hype train")
                        }
                        T {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 17
                            font.weight: Font.Bold
                            font.features: ({ "tnum": 1 })
                            readonly property double remaining: board.until(hypeBox.d.ends || 0, hype.made)
                            visible: (hypeBox.d.ends || 0) > 0
                            text: qsTr("%1 left").arg(board.clock(remaining))
                        }
                    }
                    Row {
                        width: parent.width
                        spacing: 5
                        Repeater {
                            model: 5
                            Rectangle {
                                required property int index
                                readonly property int step: hypeBox.first + index
                                readonly property real fill: step <= hypeBox.level ? 1 : (step === hypeBox.level + 1 ? hypeBox.part : 0)
                                width: (parent.width - 20) / 5
                                height: 14
                                radius: 7
                                color: board.track
                                clip: true
                                Rectangle {
                                    visible: parent.fill > 0
                                    width: Math.max(14, parent.width * parent.fill)
                                    height: 14
                                    radius: 7
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
                        width: parent.width
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        color: board.soft
                        text: {
                            const pc = qsTr("%1 % to level %2").arg(Math.round(hypeBox.part * 100)).arg(hypeBox.level + 1);
                            return hypeBox.d.top ? pc + " · " + qsTr("top: %1").arg(hypeBox.d.top) : pc;
                        }
                    }
                }
            }
        }

        Placed {
            id: poll
            key: "poll"
            title: qsTr("Poll")
            readonly property var v: board.pick("poll")
            readonly property bool made: board.isFake("poll")
            hasContent: !!board.real.poll
            Box {
                id: pollBox
                readonly property var d: poll.v || {}
                readonly property var choices: d.choices || []
                readonly property int total: Math.max(1, d.total || 0)
                readonly property int best: {
                    let b = -1, most = -1;
                    for (let i = 0; i < choices.length; i++)
                        if (choices[i].votes > most) { most = choices[i].votes; b = i; }
                    return most > 0 ? b : -1;
                }
                width: 440
                height: pollColumn.implicitHeight + 34
                radius: 20
                Column {
                    id: pollColumn
                    x: 18
                    y: 16
                    width: parent.width - 36
                    spacing: 10
                    Item {
                        width: parent.width
                        height: 28
                        Icon {
                            id: pollIcon
                            anchors.verticalCenter: parent.verticalCenter
                            name: "poll"
                            size: 24
                        }
                        Caps {
                            anchors.left: pollIcon.right
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 14
                            font.letterSpacing: 14 * 0.18
                            color: "#cfe3ff"
                            text: qsTr("Poll")
                        }
                        Rectangle {
                            anchors.right: parent.right
                            height: 28
                            width: pollLeft.implicitWidth + 24
                            radius: 14
                            color: Qt.rgba(96 / 255, 165 / 255, 250 / 255, 0.18)
                            T {
                                id: pollLeft
                                anchors.centerIn: parent
                                font.pixelSize: 15
                                font.features: ({ "tnum": 1 })
                                color: "#cfe3ff"
                                text: pollBox.d.done ? qsTr("Final") : qsTr("%1 left").arg(board.clock(board.until(pollBox.d.ends || 0, poll.made)))
                            }
                        }
                    }
                    T {
                        width: parent.width
                        font.pixelSize: 24
                        lineHeight: 1.2
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        font.letterSpacing: -0.24
                        text: pollBox.d.title || ""
                    }
                    Repeater {
                        model: pollBox.choices.slice(0, 5)
                        Rectangle {
                            required property var modelData
                            required property int index
                            readonly property bool lead: index === pollBox.best
                            readonly property real share: modelData.votes / pollBox.total
                            width: pollColumn.width
                            height: 44
                            radius: 12
                            color: board.plain ? "transparent" : Qt.rgba(1, 1, 1, 0.07)
                            border.width: lead ? 1.5 : 0
                            border.color: Qt.rgba(96 / 255, 165 / 255, 250 / 255, 0.7)
                            clip: true
                            Rectangle {
                                width: parent.width * parent.share
                                height: parent.height
                                radius: 12
                                color: Qt.rgba(96 / 255, 165 / 255, 250 / 255, parent.lead ? 0.55 : 0.22)
                            }
                            T {
                                anchors.left: parent.left
                                anchors.leftMargin: 14
                                anchors.right: pct.left
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                font.pixelSize: 19
                                font.weight: parent.lead ? Font.ExtraBold : Font.Bold
                                text: parent.modelData.title
                            }
                            T {
                                id: pct
                                anchors.right: parent.right
                                anchors.rightMargin: 14
                                anchors.verticalCenter: parent.verticalCenter
                                font.pixelSize: 19
                                font.weight: parent.lead ? Font.ExtraBold : Font.Bold
                                font.features: ({ "tnum": 1 })
                                text: Math.round(parent.share * 100) + " %"
                            }
                        }
                    }
                    T {
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        color: board.soft
                        text: pollBox.d.total === 1 ? qsTr("1 vote") : qsTr("%1 votes").arg(board.num(pollBox.d.total))
                    }
                }
            }
        }

        Placed {
            id: prediction
            key: "prediction"
            title: qsTr("Prediction")
            readonly property var v: board.pick("prediction")
            readonly property bool made: board.isFake("prediction")
            hasContent: !!board.real.prediction
            Box {
                id: predBox
                readonly property var d: prediction.v || {}
                readonly property var outcomes: d.outcomes || []
                readonly property double total: outcomes.reduce((s, o) => s + (o.points || 0), 0)
                readonly property var colors: ["#3b82f6", "#f0469b", "#34d399", "#f5b942", "#a77bff", "#00f2ff", "#fb923c", "#a3e635", "#e879f9", "#60a5fa"]
                readonly property var lights: ["#93c5fd", "#f9a8d4", "#a7f3d0", "#fde68a", "#d8c6ff", "#8ff7ff", "#fdba74", "#d9f99d", "#f5d0fe", "#cfe3ff"]
                width: 500
                height: predColumn.implicitHeight + 34
                radius: 20
                Column {
                    id: predColumn
                    x: 18
                    y: 16
                    width: parent.width - 36
                    spacing: 12
                    Item {
                        width: parent.width
                        height: 28
                        Icon {
                            id: predIcon
                            anchors.verticalCenter: parent.verticalCenter
                            name: "prediction"
                            size: 26
                        }
                        Caps {
                            anchors.left: predIcon.right
                            anchors.leftMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 14
                            font.letterSpacing: 14 * 0.18
                            color: "#d6dbff"
                            text: qsTr("Prediction")
                        }
                        Rectangle {
                            anchors.right: parent.right
                            height: 28
                            width: predLeft.implicitWidth + 24
                            radius: 14
                            color: Qt.rgba(129 / 255, 140 / 255, 248 / 255, 0.2)
                            T {
                                id: predLeft
                                anchors.centerIn: parent
                                font.pixelSize: 15
                                font.features: ({ "tnum": 1 })
                                color: "#d6dbff"
                                text: predBox.d.done ? (predBox.d.winner >= 0 ? qsTr("Decided") : qsTr("Over"))
                                    : predBox.d.locked ? qsTr("Locked")
                                    : qsTr("closes in %1").arg(board.clock(board.until(predBox.d.locks || 0, prediction.made)))
                            }
                        }
                    }
                    T {
                        width: parent.width
                        font.pixelSize: 24
                        lineHeight: 1.2
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        font.letterSpacing: -0.24
                        text: predBox.d.title || ""
                    }
                    // Who bet how much, as one bar
                    Row {
                        width: parent.width
                        height: 16
                        spacing: 3
                        Repeater {
                            model: predBox.outcomes
                            Rectangle {
                                required property var modelData
                                required property int index
                                width: predBox.total > 0 ? Math.max(4, (predColumn.width - 3 * (predBox.outcomes.length - 1)) * (modelData.points || 0) / predBox.total)
                                                         : (predColumn.width - 3 * (predBox.outcomes.length - 1)) / Math.max(1, predBox.outcomes.length)
                                height: 16
                                radius: index === 0 || index === predBox.outcomes.length - 1 ? 8 : 2
                                color: predBox.colors[index % predBox.colors.length]
                            }
                        }
                    }
                    Flow {
                        width: parent.width
                        spacing: 12
                        Repeater {
                            model: predBox.outcomes
                            Rectangle {
                                required property var modelData
                                required property int index
                                readonly property color c: predBox.colors[index % predBox.colors.length]
                                readonly property bool won: predBox.d.winner === index
                                width: (predColumn.width - 12) / 2
                                height: 70
                                radius: 14
                                color: Qt.rgba(c.r, c.g, c.b, won ? 0.32 : 0.16)
                                border.width: won ? 2 : 0
                                border.color: c
                                opacity: predBox.d.done && predBox.d.winner >= 0 && !won ? 0.55 : 1
                                T {
                                    x: 14
                                    y: 9
                                    width: parent.width - 100
                                    font.pixelSize: 22
                                    color: predBox.lights[parent.index % predBox.lights.length]
                                    text: (parent.won ? "✓ " : "") + parent.modelData.title
                                }
                                T {
                                    anchors.right: parent.right
                                    anchors.rightMargin: 14
                                    y: 8
                                    font.pixelSize: 24
                                    text: predBox.total > 0 ? Math.round(100 * (parent.modelData.points || 0) / predBox.total) + " %" : "–"
                                }
                                T {
                                    x: 14
                                    y: 42
                                    width: parent.width - 28
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    color: board.soft
                                    text: {
                                        const p = parent.modelData.points || 0;
                                        const odds = p > 0 ? " · 1:" + (predBox.total / p).toFixed(2) : "";
                                        return qsTr("%1 points").arg(board.compact(p)) + odds;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Placed {
            id: adbreak
            key: "adbreak"
            title: qsTr("Ad break")
            readonly property var v: board.pick("adbreak")
            readonly property bool made: board.isFake("adbreak")
            readonly property double running: v && v.until ? board.until(v.until, made) : 0
            readonly property double coming: v && v.next ? board.until(v.next, made) : 0
            // Running, or coming within five minutes
            hasContent: !!board.real.adbreak && (running > 0 || (coming > 0 && coming <= 300000))
            Pill {
                height: 54
                radius: 27
                width: adRow.implicitWidth + 36
                Row {
                    id: adRow
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 12
                    // Running: a ring that empties; coming: the pause sign
                    Item {
                        visible: adbreak.running > 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: 30
                        height: 30
                        Shape {
                            anchors.fill: parent
                            ShapePath {
                                strokeColor: Qt.rgba(251 / 255, 191 / 255, 36 / 255, 0.25)
                                strokeWidth: 5
                                fillColor: "transparent"
                                PathAngleArc { centerX: 15; centerY: 15; radiusX: 12.5; radiusY: 12.5; startAngle: 0; sweepAngle: 360 }
                            }
                            ShapePath {
                                strokeColor: "#fbbf24"
                                strokeWidth: 5
                                capStyle: ShapePath.RoundCap
                                fillColor: "transparent"
                                PathAngleArc {
                                    centerX: 15; centerY: 15; radiusX: 12.5; radiusY: 12.5; startAngle: -90
                                    sweepAngle: 360 * Math.max(0.02, Math.min(1, adbreak.running / 90000))
                                }
                            }
                        }
                    }
                    Icon {
                        visible: adbreak.running <= 0
                        anchors.verticalCenter: parent.verticalCenter
                        name: "ad"
                        size: 28
                    }
                    T {
                        visible: adbreak.running > 0
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 20
                        text: qsTr("Ad break")
                    }
                    T {
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 18
                        font.weight: Font.DemiBold
                        color: board.soft
                        text: adbreak.running > 0 ? qsTr("back in") : qsTr("Ad break in")
                    }
                    T {
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 22
                        font.features: ({ "tnum": 1 })
                        color: "#fde68a"
                        text: board.clock(adbreak.running > 0 ? adbreak.running : adbreak.coming)
                    }
                }
            }
        }

        // ---- all at once

        Placed {
            id: recent
            key: "recent"
            title: qsTr("Recent events")
            readonly property var v: board.pick("recent")
            hasContent: !!board.real.recent && board.real.recent.length > 0
            Box {
                width: 1856
                height: 56
                radius: 28
                clip: true
                Rectangle {
                    id: recentTab
                    width: recentLabel.implicitWidth + 38
                    height: 56
                    // Round on the left like the bar, square on the right
                    topLeftRadius: 28
                    bottomLeftRadius: 28
                    color: Qt.rgba(140 / 255, 82 / 255, 255 / 255, 0.3)
                    Caps {
                        id: recentLabel
                        x: 20
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 14
                        font.letterSpacing: 14 * 0.18
                        color: "#e2d6ff"
                        text: qsTr("Recent")
                    }
                }
                Row {
                    anchors.left: recentTab.right
                    anchors.leftMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 18
                    Repeater {
                        model: recent.v || []
                        Row {
                            required property var modelData
                            required property int index
                            spacing: 18
                            Rectangle {
                                visible: parent.index > 0
                                anchors.verticalCenter: parent.verticalCenter
                                width: 5
                                height: 5
                                radius: 3
                                color: Qt.rgba(1, 1, 1, 0.35)
                            }
                            Row {
                                spacing: 9
                                Icon {
                                    anchors.verticalCenter: parent.verticalCenter
                                    size: 22
                                    name: ({ follow: "heart", sub: "star", gift: "gift", bits: "gem", raid: "raid", points: "points",
                                             charity: "charity", shoutout: "shoutout" })[parent.parent.modelData.kind] || "star"
                                }
                                T {
                                    anchors.verticalCenter: parent.verticalCenter
                                    font.pixelSize: 19
                                    color: "#e7e4fa"
                                    text: parent.parent.modelData.name
                                }
                                T {
                                    anchors.verticalCenter: parent.verticalCenter
                                    font.pixelSize: 19
                                    font.weight: Font.DemiBold
                                    color: "#e7e4fa"
                                    text: parent.parent.modelData.text
                                }
                            }
                        }
                    }
                }
                // Fades out at the right end
                Rectangle {
                    anchors.right: parent.right
                    width: 120
                    height: parent.height
                    visible: !board.plain
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Qt.rgba(14 / 255, 12 / 255, 30 / 255, 0) }
                        GradientStop { position: 1; color: board.look === "solid" ? "#10101a" : Qt.rgba(14 / 255, 12 / 255, 30 / 255, 0.95) }
                    }
                }
            }
        }

        Placed {
            id: chat
            key: "chat"
            title: qsTr("Twitch chat")
            readonly property var v: board.pick("chat")
            hasContent: !!board.real.chat && board.real.chat.length > 0
            Column {
                width: 480
                spacing: 6
                Rectangle {
                    height: 30
                    width: chatHead.implicitWidth + 22
                    radius: 15
                    color: board.plain ? "transparent" : Qt.rgba(14 / 255, 12 / 255, 30 / 255, 0.6)
                    Row {
                        id: chatHead
                        x: 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Icon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "chat"
                            size: 18
                        }
                        T {
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 13
                            font.letterSpacing: 13 * 0.16
                            color: "#dfe2ff"
                            text: qsTr("TWITCH CHAT")
                        }
                    }
                }
                Repeater {
                    model: (chat.v || []).slice(-5)
                    Box {
                        id: line
                        required property var modelData
                        required property int index
                        readonly property int fromEnd: Math.min(5, (chat.v || []).length) - 1 - index
                        radius: 14
                        width: Math.min(480, lineRow.implicitWidth + 28)
                        height: lineRow.implicitHeight + 16
                        // Older lines fade
                        opacity: fromEnd >= 4 ? 0.55 : fromEnd === 3 ? 0.7 : fromEnd === 2 ? 0.85 : 1
                        Row {
                            id: lineRow
                            x: 14
                            y: 8
                            spacing: 8
                            Row {
                                visible: (line.modelData.badges || []).length > 0
                                spacing: 4
                                y: 2
                                Repeater {
                                    model: line.modelData.badges || []
                                    Icon {
                                        required property string modelData
                                        size: 20
                                        name: ({ sub: "star", bits: "gem", mod: "mod", vip: "vip", broadcaster: "broadcaster" })[modelData] || ""
                                    }
                                }
                            }
                            // Name and message: plain text, never markup
                            Text {
                                width: Math.min(implicitWidth, 480 - 28 - ((line.modelData.badges || []).length * 24 + 8))
                                font.family: board.family
                                font.pixelSize: 18
                                font.weight: Font.DemiBold
                                lineHeight: 1.35
                                wrapMode: Text.Wrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                                textFormat: Text.StyledText
                                color: board.ink
                                // Both are escaped before they go in: StyledText only for the bold name in its colour
                                text: "<b><font color=\"" + line.modelData.color + "\">"
                                      + String(line.modelData.name).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                                      + "</font></b>&nbsp;&nbsp;"
                                      + String(line.modelData.text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
                            }
                        }
                    }
                }
            }
        }

        // ---- lining things up while dragging

        Item {
            id: guides
            anchors.fill: parent
            visible: board.editing && dragging !== null
            z: 10
            property var dragging: null
            property real lineX: -1
            property real lineY: -1

            /** Where a dragged element goes: its edges catch the edges of the others. */
            function snap(el, nx, ny) {
                let bestX = nx, bestY = ny, dx = 9, dy = 9;
                lineX = -1;
                lineY = -1;
                for (const other of board.places) {
                    if (other === el || !other.visible)
                        continue;
                    for (const [mine, theirs, line] of [[nx, other.x, other.x], [nx + el.width, other.x + other.width, other.x + other.width],
                                                       [nx, other.x + other.width, other.x + other.width], [nx + el.width, other.x, other.x]]) {
                        if (Math.abs(mine - theirs) < dx) {
                            dx = Math.abs(mine - theirs);
                            bestX = nx + (theirs - mine);
                            lineX = line;
                        }
                    }
                    for (const [mine, theirs, line] of [[ny, other.y, other.y], [ny + el.height, other.y + other.height, other.y + other.height]]) {
                        if (Math.abs(mine - theirs) < dy) {
                            dy = Math.abs(mine - theirs);
                            bestY = ny + (theirs - mine);
                            lineY = line;
                        }
                    }
                }
                return { x: bestX, y: bestY };
            }

            Shape {
                visible: guides.lineX >= 0
                ShapePath {
                    strokeColor: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.95)
                    strokeWidth: 3
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [3, 2]
                    startX: guides.lineX
                    startY: 0
                    PathLine { x: guides.lineX; y: 1080 }
                }
            }
            Shape {
                visible: guides.lineY >= 0
                ShapePath {
                    strokeColor: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.95)
                    strokeWidth: 3
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [3, 2]
                    startX: 0
                    startY: guides.lineY
                    PathLine { x: 1920; y: guides.lineY }
                }
            }
            // Where it is, in percent of the room left
            Rectangle {
                readonly property var el: guides.dragging
                visible: !!el
                x: el ? Math.min(1920 - width - 16, el.x + el.width / 2 - width / 2) : 0
                y: el ? (el.y + el.height + 64 < 1080 ? el.y + el.height + 18 : el.y - height - 18) : 0
                height: 44
                width: whereText.implicitWidth + 36
                radius: 22
                color: "#f0469b"
                T {
                    id: whereText
                    anchors.centerIn: parent
                    font.pixelSize: 21
                    color: "white"
                    text: {
                        const el = parent.el;
                        if (!el)
                            return "";
                        const fx = Math.round(100 * Math.max(0, Math.min(1, (el.x - 32) / Math.max(1, 1920 - 64 - el.width))));
                        const fy = Math.round(100 * Math.max(0, Math.min(1, (el.y - 32) / Math.max(1, 1080 - 64 - el.height))));
                        return (guides.lineX >= 0 || guides.lineY >= 0 ? qsTr("lines up with the elements around") + " · " : "") + fx + " % · " + fy + " %";
                    }
                }
            }
        }
    }
}
