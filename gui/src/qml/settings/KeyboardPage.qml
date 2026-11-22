import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Keyboard & mouse: the keyboard as a controller, the mouse as its touchpad or, as in a
// game on a computer, for aiming; which key, mouse button or wheel direction is which button
// of the controller; and a place to try it all without a game (Chiaki.keyMouse, keymouse.h).
//
// An input is changed right where it is shown, without a pop-up: click it (or press Return
// on it) and it waits; the next key, mouse button or turn of the wheel is the new one.
// Escape keeps the old one. While it waits, the window hands every key and click to
// Chiaki.keyMouse, so none of them reaches this page.
C.SettingsPage {
    id: page
    title: qsTr("Keyboard & mouse")
    description: qsTr("Play with the keyboard as a controller, and with the mouse too: walk with the keys and aim with the mouse, as in a game on a computer.")

    readonly property var km: Chiaki.keyMouse
    /** Function → [{ label, kind }, …]. */
    readonly property var bound: km.bindings

    /** Everything that can be bound, by Chiaki.keyMouse's names for it. */
    readonly property var sections: [
        {
            title: qsTr("Left stick"),
            keywords: qsTr("analog stick move walk wasd up down left right"),
            keys: [
                { name: qsTr("Up"), full: qsTr("Left stick up"), id: "left_up" },
                { name: qsTr("Down"), full: qsTr("Left stick down"), id: "left_down" },
                { name: qsTr("Left"), full: qsTr("Left stick left"), id: "left_left" },
                { name: qsTr("Right"), full: qsTr("Left stick right"), id: "left_right" }
            ]
        },
        {
            title: qsTr("Right stick"),
            keywords: qsTr("analog stick camera look aim up down left right"),
            keys: [
                { name: qsTr("Up"), full: qsTr("Right stick up"), id: "right_up" },
                { name: qsTr("Down"), full: qsTr("Right stick down"), id: "right_down" },
                { name: qsTr("Left"), full: qsTr("Right stick left"), id: "right_left" },
                { name: qsTr("Right"), full: qsTr("Right stick right"), id: "right_right" }
            ]
        },
        {
            title: qsTr("Face buttons"),
            keywords: qsTr("cross circle square triangle moon box pyramid x o"),
            keys: [
                { name: qsTr("Cross"), id: "cross" },
                { name: qsTr("Circle"), id: "circle" },
                { name: qsTr("Square"), id: "square" },
                { name: qsTr("Triangle"), id: "triangle" }
            ]
        },
        {
            title: qsTr("D-pad"),
            keywords: qsTr("dpad direction arrows up down left right"),
            keys: [
                { name: qsTr("Up"), full: qsTr("D-pad up"), id: "dpad_up" },
                { name: qsTr("Down"), full: qsTr("D-pad down"), id: "dpad_down" },
                { name: qsTr("Left"), full: qsTr("D-pad left"), id: "dpad_left" },
                { name: qsTr("Right"), full: qsTr("D-pad right"), id: "dpad_right" }
            ]
        },
        {
            title: qsTr("Shoulders and triggers"),
            keywords: qsTr("l1 r1 l2 r2 l3 r3 bumpers triggers stick press fire aim"),
            keys: [
                { name: "L1", id: "l1" },
                { name: "R1", id: "r1" },
                { name: "L2", id: "l2" },
                { name: "R2", id: "r2" },
                { name: "L3", id: "l3" },
                { name: "R3", id: "r3" }
            ]
        },
        {
            title: qsTr("Middle"),
            keywords: qsTr("options create share touchpad ps button home"),
            keys: [
                { name: qsTr("Options"), id: "options" },
                { name: qsTr("Create"), id: "create" },
                { name: qsTr("Touchpad"), id: "touchpad" },
                { name: qsTr("PS button"), id: "ps" }
            ]
        },
        {
            title: qsTr("Walking and the mouse"),
            keywords: qsTr("walk slow sneak catch mouse release let go toggle aim"),
            keys: [
                { name: qsTr("Walk"), full: qsTr("Walk: the left stick only part of the way"), id: "walk" },
                { name: qsTr("Mouse aims, on and off"), full: qsTr("Mouse aims, on and off: catches the mouse and lets it go"), id: "mouse" }
            ]
        }
    
    ]

    Component.onDestruction: {
        km.stopListening();
        km.trying = false;
    }
    onVisibleChanged: {
        if (!visible) {
            km.stopListening();
            km.trying = false;
        }
    }

    // ------------------------------------------------ building blocks

    /** One input of a function, as a key: click it and the next key or mouse button takes its place. The last one of a row, with a plus, adds one. */
    component Cap: QQC.AbstractButton {
        id: cap
        /** The function, as Chiaki.keyMouse names it, and which of its inputs. */
        property string function_
        property int slot: 0
        /** { label, kind }, or null for the one that adds. */
        property var input: null
        /** What the function is called, for screen readers. */
        property string what
        readonly property bool waiting: page.km.listening === function_ && page.km.listeningSlot === slot
        property bool firstInFocusChain: false
        property bool lastInFocusChain: false

        implicitWidth: Math.max(input || waiting ? 44 : 30, capText.implicitWidth + 18)
        implicitHeight: 30
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        text: waiting ? qsTr("Press…") : input ? input.label : "+"
        Accessible.name: input ? qsTr("%1: %2").arg(what).arg(input.label) : qsTr("%1: add a key or mouse button").arg(what)
        Accessible.description: waiting ? qsTr("Press the new key or mouse button, or Escape to keep this one.")
                                        : qsTr("Press to change it. Delete takes it away.")
        QQC.ToolTip.visible: hovered && !waiting && !input
        QQC.ToolTip.text: qsTr("Add another key or mouse button")
        QQC.ToolTip.delay: 500

        onClicked: page.km.listen(function_, slot)
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                if (!event.isAutoRepeat)
                    page.km.listen(function_, slot);
                event.accepted = true;
                return;
            }
            if (input && (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)) {
                page.km.clear(function_, slot);
                event.accepted = true;
                return;
            }
            C.FocusChain.handle(cap, event);
        }

        background: Item {
            // The key: a darker edge, thicker at the bottom. The one that adds is only an outline
            Rectangle {
                anchors.fill: parent
                visible: !cap.waiting && !!cap.input
                radius: 8
                color: "#39325e"

                Rectangle {
                    anchors {
                        fill: parent
                        leftMargin: 1
                        rightMargin: 1
                        topMargin: 1
                        bottomMargin: 3
                    }
                    radius: 7
                    color: cap.down ? "#1a1632" : cap.hovered ? "#2a2449" : "#221d3e"
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: !cap.waiting && !cap.input
                radius: 8
                color: Qt.rgba(1, 1, 1, cap.down ? 0.10 : cap.hovered ? 0.07 : 0.03)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, cap.hovered ? 0.22 : 0.12)
            }

            // Waiting for the new one
            Rectangle {
                anchors.fill: parent
                visible: cap.waiting
                radius: 8
                color: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.14)
                border.width: 1
                border.color: C.Theme.wait

                SequentialAnimation on opacity {
                    running: cap.waiting
                    loops: Animation.Infinite
                    alwaysRunToEnd: true
                    NumberAnimation { to: 0.6; duration: 700; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1; duration: 700; easing.type: Easing.InOutSine }
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: 11
                color: "transparent"
                border.width: 2
                border.color: C.Theme.focus
                visible: cap.visualFocus
            }
        }

        contentItem: Text {
            id: capText
            text: cap.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            // On the key's face, above its thicker bottom edge
            bottomPadding: cap.waiting || !cap.input ? 0 : 2
            color: cap.waiting ? "#fde4a8" : cap.input ? C.Theme.text : C.Theme.textSoft
            font.pixelSize: cap.input || cap.waiting ? 12 : 15
            font.weight: Font.ExtraBold
        }

        // Takes the input away: a small cross on the key's corner, while the pointer is on the key
        Rectangle {
            id: removeMark
            visible: !!cap.input && !cap.waiting && (cap.hovered || removeArea.containsMouse)
            x: parent.width - 9
            y: -7
            width: 16
            height: 16
            radius: 8
            color: removeArea.containsMouse ? C.Theme.bad : "#4a4370"
            border.width: 1
            border.color: C.Theme.card

            C.Icon {
                anchors.centerIn: parent
                iconName: "close"
                size: 10
                color: "#ffffff"
            }

            MouseArea {
                id: removeArea
                anchors.fill: parent
                anchors.margins: -3
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: page.km.clear(cap.function_, cap.slot)
            }
        }
    }

    /**
     * Acceleration, drawn: how far the stick goes (up) for how fast the mouse moves (to the
     * right). The four points in between can be dragged, each no lower than the one before it.
     */
    component CurveGraph: Item {
        id: graph
        readonly property var points: page.km.curve
        // Where the drawing is: room for the words on the left and below
        readonly property real plotLeft: 22
        readonly property real plotTop: 8
        readonly property real plotWidth: width - plotLeft - 12
        readonly property real plotHeight: height - plotTop - 24

        function atX(i) {
            return plotLeft + plotWidth * i / 5;
        }

        function atY(value) {
            return plotTop + plotHeight * (1 - value);
        }

        function value(i) {
            return i <= 0 ? 0 : i >= 5 ? 1 : (points[i - 1] || 0);
        }

        /** Point `i` (0 to 3) to `to`, kept between its neighbours. */
        function setPoint(i, to) {
            const next = [points[0], points[1], points[2], points[3]];
            const low = i > 0 ? next[i - 1] : 0;
            const high = i < 3 ? next[i + 1] : 1;
            next[i] = Math.max(low, Math.min(high, Math.round(to * 100) / 100));
            page.km.curve = next;
        }

        implicitWidth: 320
        implicitHeight: 190

        Rectangle {
            x: graph.plotLeft
            y: graph.plotTop
            width: graph.plotWidth
            height: graph.plotHeight
            radius: 10
            color: C.Theme.field
            border.width: 1
            border.color: C.Theme.line
        }

        // A line for each fifth, both ways
        Repeater {
            model: 4

            Rectangle {
                required property int index
                x: graph.atX(index + 1)
                y: graph.plotTop + 1
                width: 1
                height: graph.plotHeight - 2
                color: Qt.rgba(1, 1, 1, 0.05)
            }
        }

        Repeater {
            model: 4

            Rectangle {
                required property int index
                x: graph.plotLeft + 1
                y: graph.atY((index + 1) / 5)
                width: graph.plotWidth - 2
                height: 1
                color: Qt.rgba(1, 1, 1, 0.05)
            }
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            // Even, for comparison: twice as fast, twice as far
            ShapePath {
                strokeColor: Qt.rgba(1, 1, 1, 0.16)
                strokeWidth: 1.5
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 4]
                fillColor: "transparent"
                startX: graph.atX(0)
                startY: graph.atY(0)
                PathLine { x: graph.atX(5); y: graph.atY(1) }
            }

            // What is under the curve, lightly
            ShapePath {
                strokeColor: "transparent"
                strokeWidth: 0
                fillGradient: LinearGradient {
                    x1: graph.plotLeft
                    y1: 0
                    x2: graph.plotLeft + graph.plotWidth
                    y2: 0
                    GradientStop { position: 0.0; color: Qt.rgba(140 / 255, 82 / 255, 1, 0.20) }
                    GradientStop { position: 1.0; color: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.20) }
                }
                startX: graph.atX(0)
                startY: graph.atY(0)
                PathLine { x: graph.atX(1); y: graph.atY(graph.value(1)) }
                PathLine { x: graph.atX(2); y: graph.atY(graph.value(2)) }
                PathLine { x: graph.atX(3); y: graph.atY(graph.value(3)) }
                PathLine { x: graph.atX(4); y: graph.atY(graph.value(4)) }
                PathLine { x: graph.atX(5); y: graph.atY(1) }
                PathLine { x: graph.atX(5); y: graph.atY(0) }
                PathLine { x: graph.atX(0); y: graph.atY(0) }
            }

            ShapePath {
                strokeColor: "#b08cff"
                strokeWidth: 3
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                fillColor: "transparent"
                startX: graph.atX(0)
                startY: graph.atY(0)
                PathLine { x: graph.atX(1); y: graph.atY(graph.value(1)) }
                PathLine { x: graph.atX(2); y: graph.atY(graph.value(2)) }
                PathLine { x: graph.atX(3); y: graph.atY(graph.value(3)) }
                PathLine { x: graph.atX(4); y: graph.atY(graph.value(4)) }
                PathLine { x: graph.atX(5); y: graph.atY(1) }
            }
        }

        // The points to drag
        Repeater {
            model: 4

            Item {
                id: point
                required property int index
                readonly property real at: graph.value(index + 1)
                property bool firstInFocusChain: false
                property bool lastInFocusChain: false

                x: graph.atX(index + 1) - width / 2
                y: graph.atY(at) - height / 2
                width: 30
                height: 30
                activeFocusOnTab: true
                Accessible.role: Accessible.Slider
                Accessible.name: qsTr("At %1 % of full speed the stick goes %2 % of the way").arg((index + 1) * 20).arg(Math.round(at * 100))
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Right || event.key === Qt.Key_Plus) {
                        graph.setPoint(index, at + 0.02);
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Minus) {
                        graph.setPoint(index, at - 0.02);
                        event.accepted = true;
                    } else
                        C.FocusChain.handle(point, event);
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: pointArea.pressed ? 18 : 14
                    height: width
                    radius: width / 2
                    color: "#ffffff"
                    border.width: pointArea.pressed ? 5 : 3
                    border.color: C.Theme.brandFrom
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: 26
                    height: 26
                    radius: 13
                    color: "transparent"
                    border.width: 2
                    border.color: C.Theme.focus
                    visible: point.activeFocus
                }

                MouseArea {
                    id: pointArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.SizeVerCursor
                    // The page scrolls up and down too: this drag is ours
                    preventStealing: true
                    onPressed: point.forceActiveFocus(Qt.MouseFocusReason)
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return;
                        const inGraph = mapToItem(graph, mouse.x, mouse.y);
                        graph.setPoint(point.index, (graph.plotTop + graph.plotHeight - inGraph.y) / graph.plotHeight);
                    }
                }
            }
        }

        Text {
            x: graph.plotLeft
            y: graph.plotTop + graph.plotHeight + 6
            text: qsTr("mouse slow")
            color: C.Theme.textDim
            font.pixelSize: 11
        }

        Text {
            x: graph.plotLeft + graph.plotWidth - width
            y: graph.plotTop + graph.plotHeight + 6
            text: qsTr("mouse fast")
            color: C.Theme.textDim
            font.pixelSize: 11
        }

        Text {
            x: graph.plotLeft - 8 - height
            y: graph.plotTop + width
            rotation: -90
            transformOrigin: Item.TopLeft
            text: qsTr("stick far")
            color: C.Theme.textDim
            font.pixelSize: 11
        }
    }

    // ------------------------------------------------ the two switches and the layouts

    Rectangle {
        id: bar
        Layout.fillWidth: true
        implicitHeight: barLayout.implicitHeight + 20
        radius: 18
        color: C.Theme.card
        border.width: 1
        border.color: C.Theme.line

        GridLayout {
            id: barLayout
            readonly property bool wide: bar.width >= 600

            x: 14
            y: 10
            width: parent.width - 28
            columns: wide ? 5 : 1
            columnSpacing: 12
            rowSpacing: 8

            // Each part is a row for the search: what it is called and the words it is found by
            Item {
                readonly property bool isSettingRow: true
                readonly property string title: qsTr("Keyboard as controller")
                readonly property string subtitle: qsTr("Play with the keys below while streaming.")
                readonly property string keywords: qsTr("keys keyboard controller play enable")
                implicitWidth: keyboardSwitch.implicitWidth
                implicitHeight: 36

                C.Toggle {
                    id: keyboardSwitch
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.title
                    font.pixelSize: 14
                    font.weight: Font.ExtraBold
                    Accessible.name: text
                    checked: Chiaki.settings.keyboardEnabled
                    onToggled: Chiaki.settings.keyboardEnabled = checked
                    QQC.ToolTip.visible: hovered
                    QQC.ToolTip.text: parent.subtitle
                    QQC.ToolTip.delay: 600
                }
            }

            Rectangle {
                visible: barLayout.wide
                implicitWidth: 1
                implicitHeight: 24
                color: Qt.rgba(1, 1, 1, 0.10)
            }

            Item {
                readonly property bool isSettingRow: true
                readonly property string title: qsTr("Mouse as touchpad")
                readonly property string subtitle: qsTr("Drag on the picture to swipe on the touchpad; another mouse button presses it. Not while the mouse aims.")
                readonly property string keywords: qsTr("mouse touchpad touch swipe click trackpad enable")
                implicitWidth: mouseSwitch.implicitWidth
                implicitHeight: 36

                C.Toggle {
                    id: mouseSwitch
                    anchors.verticalCenter: parent.verticalCenter
                    text: parent.title
                    font.pixelSize: 14
                    font.weight: Font.ExtraBold
                    Accessible.name: text
                    checked: Chiaki.settings.mouseTouchEnabled
                    onToggled: Chiaki.settings.mouseTouchEnabled = checked
                    QQC.ToolTip.visible: hovered
                    QQC.ToolTip.text: parent.subtitle
                    QQC.ToolTip.delay: 600
                }
            }

            Item {
                visible: barLayout.wide
                Layout.fillWidth: true
            }

            Item {
                readonly property bool isSettingRow: true
                readonly property string title: qsTr("Layouts")
                readonly property string subtitle: qsTr("Every key and mouse button at once: as in games on a computer, or the keys the app came with.")
                readonly property string keywords: qsTr("default defaults reset preset wasd shooter fps key mapping restore layout")
                implicitWidth: layouts.implicitWidth
                implicitHeight: 36

                C.PillButton {
                    id: layouts
                    anchors.verticalCenter: parent.verticalCenter
                    small: true
                    iconName: "chevron-down"
                    iconAfter: true
                    text: parent.title
                    onClicked: layoutsMenu.popup(layouts, 0, layouts.height + 4)

                    C.Menu {
                        id: layoutsMenu

                        QQC.MenuItem {
                            text: qsTr("Mouse and WASD, as in games on a computer")
                            onTriggered: root.showConfirmDialog(qsTr("Mouse and WASD"),
                                                                qsTr("Every key and mouse button gets what games on a computer give it: WASD walks, the mouse aims, the left button fires, the right one aims, Space jumps. What you set yourself is replaced, and the mouse aims from now on."),
                                                                () => page.km.applyLayout("shooter"))
                        }

                        QQC.MenuItem {
                            text: qsTr("Keys only, as the app came")
                            onTriggered: root.showConfirmDialog(qsTr("Keys only"),
                                                                qsTr("Every button gets the key it came with: the arrows for the D-pad, Return for Cross, the number keys for the shoulders. What you set yourself is replaced, and the mouse no longer aims."),
                                                                () => page.km.applyLayout("classic"))
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------ the mouse aims

    C.SettingsGroup {
        title: qsTr("Aiming with the mouse")
        // Off while the keyboard does not play
        enabled: Chiaki.settings.keyboardEnabled
        opacity: enabled ? 1 : 0.5

        C.SettingsRow {
            title: qsTr("The mouse aims")
            subtitle: page.km.catchKey.length > 0
                      ? qsTr("Moving the mouse moves a stick, and its buttons and wheel are buttons of the controller. While you play, the mouse is caught: %1 lets it go and catches it again, and the stream menu lets it go too.").arg(page.km.catchKey)
                      : qsTr("Moving the mouse moves a stick, and its buttons and wheel are buttons of the controller. While you play, the mouse is caught: the stream menu lets it go.")
            keywords: qsTr("mouse aim look camera shooter fps conversion controller catch capture wasd")
            switchItem: aimSwitch

            C.Toggle {
                id: aimSwitch
                Accessible.name: qsTr("The mouse aims")
                checked: page.km.aim
                onToggled: page.km.aim = checked
            }
        }

        C.SettingsRow {
            sub: true
            enabled: page.km.aim
            title: qsTr("The mouse moves")
            subtitle: qsTr("The right stick looks around in most games.")
            keywords: qsTr("mouse stick right left camera")
            textMinimum: 200

            C.Segmented {
                model: [qsTr("Right stick"), qsTr("Left stick")]
                currentIndex: page.km.rightStick ? 0 : 1
                onActivated: (index) => page.km.rightStick = index === 0
            }
        }

        C.SettingsRow {
            sub: true
            enabled: page.km.aim
            title: qsTr("Speed, left and right")
            subtitle: qsTr("How far the stick goes for a movement of the mouse. Set the game's own stick speed high, then find what fits here.")
            keywords: qsTr("mouse sensitivity speed horizontal turn")
            textMinimum: 200

            C.ValueSlider {
                Layout.preferredWidth: 230
                valueWidth: 52
                Accessible.name: qsTr("Speed, left and right")
                from: 10
                to: 400
                stepSize: 5
                value: Math.round(page.km.sensX * 100)
                valueText: qsTr("%1 %").arg(Math.round(value))
                onMoved: page.km.sensX = value / 100
            }
        }

        C.SettingsRow {
            sub: true
            enabled: page.km.aim
            title: qsTr("Speed, up and down")
            keywords: qsTr("mouse sensitivity speed vertical look")
            textMinimum: 200

            C.ValueSlider {
                Layout.preferredWidth: 230
                valueWidth: 52
                Accessible.name: qsTr("Speed, up and down")
                from: 10
                to: 400
                stepSize: 5
                value: Math.round(page.km.sensY * 100)
                valueText: qsTr("%1 %").arg(Math.round(value))
                onMoved: page.km.sensY = value / 100
            }
        }

        C.SettingsRow {
            sub: true
            enabled: page.km.aim
            title: qsTr("Acceleration")
            subtitle: qsTr("How much further the stick goes as the mouse gets faster. Even: twice as fast, twice as far. Fine: small movements aim more finely. Quick: the stick answers strongly at once. Or drag the points to a curve of your own.")
            keywords: qsTr("mouse acceleration curve response ballistics even linear fine precise quick custom")
            textMinimum: 220

            ColumnLayout {
                spacing: 10

                C.Segmented {
                    readonly property var names: ["even", "fine", "quick", "custom"]
                    Layout.alignment: Qt.AlignRight
                    model: [qsTr("Even"), qsTr("Fine"), qsTr("Quick"), qsTr("Own")]
                    currentIndex: Math.max(0, names.indexOf(page.km.curveName))
                    onActivated: (index) => {
                        if (index < 3)
                            page.km.curveName = names[index];
                    }
                }

                CurveGraph {
                    Layout.preferredWidth: 320
                    Layout.preferredHeight: 190
                }
            }
        }

        C.SettingsRow {
            sub: true
            enabled: page.km.aim
            title: qsTr("Past the game's dead zone")
            subtitle: qsTr("Games ignore a stick that is only a little off its middle. The smallest movement of the mouse pushes the stick this far at once. Too low and slow aiming does nothing; too high and it jumps.")
            keywords: qsTr("dead zone deadzone anti minimum stick small movement")
            textMinimum: 200

            C.ValueSlider {
                Layout.preferredWidth: 230
                valueWidth: 52
                Accessible.name: qsTr("Past the game's dead zone")
                from: 0
                to: 40
                stepSize: 1
                value: Math.round(page.km.deadZone * 100)
                valueText: qsTr("%1 %").arg(Math.round(value))
                onMoved: page.km.deadZone = value / 100
            }
        }

        C.SettingsRow {
            sub: true
            enabled: page.km.aim
            title: qsTr("Smoothing")
            subtitle: qsTr("The mouse's speed is taken over this long. Longer is calmer, and a little later.")
            keywords: qsTr("smooth jitter average delay milliseconds")
            textMinimum: 200

            C.ValueSlider {
                Layout.preferredWidth: 230
                valueWidth: 52
                Accessible.name: qsTr("Smoothing")
                from: 8
                to: 80
                stepSize: 4
                value: page.km.smoothing
                valueText: qsTr("%1 ms").arg(Math.round(value))
                onMoved: page.km.smoothing = Math.round(value)
            }
        }

        C.SettingsRow {
            sub: true
            enabled: page.km.aim
            title: qsTr("Slower while aiming")
            subtitle: qsTr("While the button for aiming is held, the mouse counts this much: a steadier hand down the sights.")
            keywords: qsTr("aim down sights ads zoom scope slower sensitivity l2 l1")
            textMinimum: 200

            RowLayout {
                spacing: 10

                C.Segmented {
                    readonly property var buttons: ["l2", "l1", ""]
                    model: ["L2", "L1", qsTr("Never")]
                    currentIndex: Math.max(0, buttons.indexOf(page.km.aimButton))
                    onActivated: (index) => page.km.aimButton = buttons[index]
                }

                C.ValueSlider {
                    Layout.preferredWidth: 170
                    valueWidth: 46
                    enabled: page.km.aimButton.length > 0
                    Accessible.name: qsTr("Speed while aiming")
                    from: 20
                    to: 100
                    stepSize: 5
                    value: Math.round(page.km.aimFactor * 100)
                    valueText: qsTr("%1 %").arg(Math.round(value))
                    onMoved: page.km.aimFactor = value / 100
                }
            }
        }

        C.SettingsRow {
            sub: true
            enabled: page.km.aim
            title: qsTr("Up and down the other way round")
            subtitle: qsTr("Pushing the mouse away looks down, as in a plane.")
            keywords: qsTr("invert y axis inverted look flight")
            switchItem: invertSwitch

            C.Toggle {
                id: invertSwitch
                Accessible.name: qsTr("Up and down the other way round")
                checked: page.km.invertY
                onToggled: page.km.invertY = checked
            }
        }

        C.SettingsRow {
            title: qsTr("Walking")
            subtitle: qsTr("How far the left stick goes while the key for walking is held. Without it, a key pushes the stick all the way.")
            keywords: qsTr("walk slow sneak half stick analog")
            textMinimum: 200

            C.ValueSlider {
                Layout.preferredWidth: 230
                valueWidth: 52
                Accessible.name: qsTr("Walking")
                from: 20
                to: 90
                stepSize: 5
                value: Math.round(page.km.walk * 100)
                valueText: qsTr("%1 %").arg(Math.round(value))
                onMoved: page.km.walk = value / 100
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Good to know: a console game reads the stick as how fast to turn, with a dead zone and a top speed of its own. So aiming with the mouse plays well once the speeds fit the game, but not like a game made for a mouse, and turning faster than the game's top speed is not possible. Some online games do not allow a mouse through a controller in their rules.")
        wrapMode: Text.WordWrap
        lineHeight: 1.15
        color: C.Theme.textDim
        font.pixelSize: 13
    }

    // ------------------------------------------------ trying it

    Rectangle {
        id: tryCard
        readonly property bool isSettingRow: true
        readonly property string title: qsTr("Try it here")
        readonly property string subtitle: qsTr("See what your keys and your mouse do to the controller, without a game.")
        readonly property string keywords: qsTr("test try preview controller picture tune")
        readonly property bool wide: width >= 560

        Layout.fillWidth: true
        implicitHeight: tryLayout.implicitHeight + 28
        radius: 18
        color: C.Theme.card
        border.width: page.km.trying ? 2 : 1
        border.color: page.km.trying ? C.Theme.brandFrom : C.Theme.line
        enabled: Chiaki.settings.keyboardEnabled
        opacity: enabled ? 1 : 0.5

        GridLayout {
            id: tryLayout
            x: 18
            y: 14
            width: parent.width - 36
            columns: tryCard.wide ? 2 : 1
            columnSpacing: 22
            rowSpacing: 12

            Item {
                Layout.alignment: Qt.AlignHCenter | Qt.AlignVCenter
                implicitWidth: tryPad.width
                implicitHeight: tryPad.height

                C.PadView {
                    id: tryPad
                    factor: 0.3
                    pad: page.km.trying ? page.km.pad : null
                    opacity: page.km.trying ? 1 : 0.55
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 8

                C.SectionLabel {
                    text: tryCard.title
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    lineHeight: 1.15
                    color: page.km.trying ? C.Theme.text : C.Theme.textSoft
                    font.pixelSize: 14
                    text: !page.km.trying ? tryCard.subtitle
                          : page.km.caught ? qsTr("Your keys and your mouse are the controller now, and the mouse is caught. Escape ends it.")
                                           : qsTr("Your keys are the controller now. Escape ends it.")
                }

                C.PillButton {
                    kind: page.km.trying ? "ghost" : "primary"
                    small: true
                    iconName: page.km.trying ? "close" : "play"
                    text: page.km.trying ? qsTr("End") : qsTr("Try it")
                    onClicked: page.km.trying = !page.km.trying
                }
            }
        }
    }

    // ------------------------------------------------ the inputs, one card per part of the controller

    GridLayout {
        id: keyGrid
        readonly property real cardWidth: (width - columnSpacing * (columns - 1)) / columns

        Layout.fillWidth: true
        // Off while the keyboard does not play
        enabled: Chiaki.settings.keyboardEnabled
        opacity: enabled ? 1 : 0.5
        columns: width >= 640 ? 2 : 1
        columnSpacing: 14
        rowSpacing: 14

        Repeater {
            model: page.sections

            Rectangle {
                id: card
                required property var modelData
                // A row for the search: the window finds it by these, and its first key gets the focus
                readonly property bool isSettingRow: true
                readonly property string title: modelData.title
                readonly property string subtitle: modelData.keys.map((k) => k.full || k.name).join(", ")
                readonly property string keywords: qsTr("key keys mouse button") + " " + modelData.keywords

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: keyGrid.cardWidth
                implicitHeight: cardColumn.implicitHeight + 24
                radius: 18
                color: C.Theme.card
                border.width: 1
                border.color: C.Theme.line

                ColumnLayout {
                    id: cardColumn
                    x: 14
                    y: 12
                    width: parent.width - 28
                    spacing: 6

                    C.SectionLabel {
                        text: card.title
                    }

                    Repeater {
                        model: card.modelData.keys

                        RowLayout {
                            id: line
                            required property var modelData
                            readonly property var inputs: page.bound[modelData.id] || []

                            Layout.fillWidth: true
                            Layout.minimumHeight: 38
                            spacing: 8

                            Text {
                                Layout.fillWidth: true
                                text: line.modelData.name
                                elide: Text.ElideRight
                                color: C.Theme.text
                                font.pixelSize: 14
                                font.weight: Font.Bold
                            }

                            Row {
                                spacing: 6

                                Repeater {
                                    // What is bound, and one more to add while there is room
                                    model: Math.min(page.km.maxInputs, line.inputs.length + 1)

                                    Cap {
                                        required property int index
                                        function_: line.modelData.id
                                        slot: index
                                        input: index < line.inputs.length ? line.inputs[index] : null
                                        what: line.modelData.full || line.modelData.name
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Changing happens right here: click a key and press the new one, or a mouse button, or turn the wheel. Up to three for each button of the controller; one key or mouse button does one thing. Escape keeps the old one, so Escape itself comes back only with a layout. Mouse buttons and the wheel count while the mouse aims.")
        wrapMode: Text.WordWrap
        lineHeight: 1.15
        color: C.Theme.textDim
        font.pixelSize: 13
    }
}
