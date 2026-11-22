import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window

import org.streetpea.chiaking

import "controls" as C

// The stream menu (⌘O / Ctrl+O, or the controller combo), laid over the game picture:
// the connection and the two ways out along the top, Play Together in a panel at the
// side (PlayTogetherBar), and sound, picture, quality and overlays in a dock along the
// bottom. Shown inside the stream window, or with some renderers in a window of its own
// that covers the game picture (StreamMenuWindow). Every control can be reached with the
// arrow keys / D-pad; Escape or Circle goes back to the game.
FocusScope {
    id: streamMenu

    /** Gets the focus when the menu opens. */
    readonly property Item initialFocusItem: resumeButton
    /** Bind to whether the menu is open: it slides in and out by itself. */
    property bool shown: false

    /** The menu has slid out after `shown` went false. */
    signal hidden()
    signal closeRequested()
    signal displaySettingsRequested()
    signal placeboSettingsRequested()
    signal displayPanelRequested()
    signal mainViewRequested()
    signal chatRequested()
    /** Drag the things on the game picture into place. */
    signal arrangeRequested()

    readonly property var session: Chiaki.session
    readonly property bool connected: !!session && session.connected
    readonly property bool guest: !!session && session.coplayGuest
    readonly property bool together: !!session && session.coplayActive
    readonly property bool hosting: together && session.coplayHosting
    readonly property bool padConnected: Chiaki.controllers.length > 0

    // ------------------------------------------------------------ where things go

    readonly property int margin: 16
    /** Too narrow for the panel beside the game: it takes the width between top bar and dock. */
    readonly property bool narrow: width < 800
    readonly property bool panelShown: !!session
    readonly property int panelWidth: narrow ? width - 2 * margin : Math.round(Math.max(368, Math.min(420, width * 0.2875)))
    /** What the panel takes from the width of the top bar and the dock. */
    readonly property int sideWidth: panelShown && !narrow ? panelWidth + margin : 0

    // Slides in and out
    property real reveal: shown ? 1 : 0
    Behavior on reveal {
        NumberAnimation {
            duration: 220
            easing.type: Easing.OutCubic
            onRunningChanged: {
                if (!running && !streamMenu.shown)
                    streamMenu.hidden();
            }
        }
    }

    onShownChanged: {
        if (!shown)
            closeMenus();
    }

    function setVolume(v) {
        Chiaki.settings.audioVolume = Math.max(0, Math.min(128, Math.round(v)));
    }

    function setZoom(v) {
        const z = Math.max(-1, Math.min(4, Math.round(v * 100) / 100));
        Chiaki.window.ZoomFactor = z;
        Chiaki.settings.sZoomFactor = z;
    }

    function leave() {
        if (Chiaki.session)
            Chiaki.window.close(); // asks first, depending on the settings
        else
            mainViewRequested();
    }

    function closeMenus() {
        panel.closeMenus();
        overlaysMenu.close();
        qualityCombo.popup.close();
    }

    // ------------------------------------------------------------ keys and controller
    //
    // Up and Down move along the focus chain (each control does that itself, see
    // controls/FocusChain.qml), and so do Left and Right where a control leaves them to us.
    // The chain runs: End session, Back to game, the panel from top to bottom, the dock from
    // left to right. Back to game gets the focus first; Down from it goes into the panel and
    // Right to End session, so that the way out is never one key away by accident.

    property point lastFocusPoint: Qt.point(0, 0)
    property bool lastFocusRing: false
    /** What had the focus last: when it goes away, the focus goes to what is closest. */
    property Item lastFocusItem: null

    function contains(item) {
        for (let p = item; p; p = p.parent) {
            if (p === streamMenu)
                return true;
        }
        return false;
    }

    /** The window's root, or something else the menu sits in: the focus fell out of the menu. */
    function isAbove(item) {
        for (let p = streamMenu.parent; p; p = p.parent) {
            if (p === item)
                return true;
        }
        return false;
    }

    function focusItem(item) {
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason);
    }

    function step(item, forward) {
        const next = item.nextItemInFocusChain(forward);
        if (next && next !== leaveButton && contains(next))
            focusItem(next);
    }

    /**
     * The focus is on something that went away (a button that hid, a knock that was
     * answered), or it fell out of the menu when one of its lists closed: it goes to
     * whatever is closest to where it was, with the ring if the ring was showing.
     */
    function recoverFocus() {
        const w = streamMenu.Window.window;
        if (!shown || !w)
            return;
        const now = w.activeFocusItem;
        if (now && now !== streamMenu && !isAbove(now) && now.visible)
            return;
        let best = null;
        let bestDistance = Infinity;
        let item = leaveButton;
        for (let i = 0; i < 300 && item; ++i) {
            if (contains(item) && item.visible && item.enabled) {
                const c = item.mapToItem(streamMenu, item.width / 2, item.height / 2);
                const d = Math.pow(c.x - lastFocusPoint.x, 2) + Math.pow(c.y - lastFocusPoint.y, 2);
                if (d < bestDistance) {
                    best = item;
                    bestDistance = d;
                }
            }
            item = item.nextItemInFocusChain(true);
            if (item === leaveButton)
                break;
        }
        const target = best || resumeButton;
        target.forceActiveFocus(lastFocusRing ? Qt.TabFocusReason : Qt.OtherFocusReason);
    }

    function focusMoved() {
        const w = streamMenu.Window.window;
        if (!shown || !w)
            return;
        const item = w.activeFocusItem;
        if (item && item !== streamMenu && contains(item)) {
            lastFocusItem = item;
            lastFocusPoint = item.mapToItem(streamMenu, item.width / 2, item.height / 2);
            lastFocusRing = item.visualFocus === true;
            panel.ensureVisible(item);
        } else if (!item || item === streamMenu || isAbove(item)) {
            recoverTimer.restart();
        }
    }

    // After the layouts have caught up with what came and went, so that "closest" is right
    Timer {
        id: recoverTimer
        interval: 50
        onTriggered: streamMenu.recoverFocus()
    }

    Connections {
        target: streamMenu.Window.window
        ignoreUnknownSignals: true
        function onActiveFocusItemChanged() { streamMenu.focusMoved(); }
    }

    // Qt leaves the focus on a button that hides: it would go on from there unseen
    Connections {
        target: streamMenu.lastFocusItem
        ignoreUnknownSignals: true
        function onVisibleChanged() {
            if (streamMenu.lastFocusItem && !streamMenu.lastFocusItem.visible)
                recoverTimer.restart();
        }
    }

    Keys.onPressed: (event) => {
        const key = event.key;
        if (key === Qt.Key_Escape || key === Qt.Key_Menu) {
            streamMenu.closeRequested();
            event.accepted = true;
            return;
        }
        if (key !== Qt.Key_Left && key !== Qt.Key_Right && key !== Qt.Key_Up && key !== Qt.Key_Down)
            return;
        event.accepted = true;
        const w = streamMenu.Window.window;
        const item = w ? w.activeFocusItem : null;
        if (!item || item === streamMenu || !streamMenu.contains(item) || !item.visible) {
            streamMenu.lastFocusRing = true;
            streamMenu.recoverFocus();
            return;
        }
        // Writing: the arrows move in the text
        if (item.readOnly === false && item.cursorPosition !== undefined)
            return;
        if (item === resumeButton || item === leaveButton) {
            if (key === Qt.Key_Right && item === resumeButton)
                streamMenu.focusItem(leaveButton);
            else if (key === Qt.Key_Left && item === leaveButton)
                streamMenu.focusItem(resumeButton);
            else if (key === Qt.Key_Down)
                streamMenu.step(resumeButton, true);
            return;
        }
        if (key === Qt.Key_Left || key === Qt.Key_Right)
            streamMenu.step(item, key === Qt.Key_Right);
    }

    // ------------------------------------------------------------ building blocks

    /** A button of the dock: an icon, or a word with an icon, 36 px high. */
    component DockButton: AbstractButton {
        id: db
        property string iconName
        /** Says what it does in a tooltip and to screen readers. */
        property string tip
        // `checked` shows a switch that is on (the console's microphone); a click does not change it
        /** Opens a list. */
        property bool chevron: false
        property bool firstInFocusChain: false
        property bool lastInFocusChain: false

        implicitHeight: 36
        implicitWidth: text.length > 0 ? dbRow.implicitWidth + 22 : 36
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        font.pixelSize: 13
        font.weight: Font.Bold
        opacity: enabled ? 1 : 0.45
        Accessible.name: tip.length > 0 ? tip : text
        ToolTip.visible: hovered && tip.length > 0
        ToolTip.text: tip
        ToolTip.delay: 600

        Keys.onPressed: (event) => C.FocusChain.handle(db, event)

        background: Item {
            Rectangle {
                anchors.fill: parent
                radius: 10
                color: db.checked ? Qt.rgba(122 / 255, 60 / 255, 245 / 255, db.hovered ? 0.45 : 0.35)
                     : Qt.rgba(1, 1, 1, db.down ? 0.14 : db.hovered ? 0.11 : (db.text.length > 0 ? 0.05 : 0.07))
                border.width: db.checked || db.text.length > 0 ? 1 : 0
                border.color: db.checked ? Qt.rgba(140 / 255, 82 / 255, 1, 0.7) : Qt.rgba(1, 1, 1, 0.12)
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: 14
                color: "transparent"
                border.width: 2
                border.color: C.Theme.focus
                visible: db.visualFocus
            }
        }

        contentItem: Item {
            Row {
                id: dbRow
                anchors.centerIn: parent
                spacing: 6

                C.Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: db.iconName.length > 0
                    iconName: db.iconName
                    size: db.text.length > 0 ? 15 : 17
                    color: db.checked ? "#ffffff" : db.text.length > 0 ? "#cdb6ff" : "#d9d5f2"
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: text.length > 0
                    text: db.text
                    font: db.font
                    color: C.Theme.text
                }

                C.Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: db.chevron
                    iconName: "chevron-down"
                    size: 14
                    color: C.Theme.textSoft
                }
            }
        }
    }

    /** A thin upright line between the groups of the dock. */
    component DockLine: Item {
        implicitWidth: 3
        implicitHeight: 36

        Rectangle {
            anchors.centerIn: parent
            width: 1
            height: 26
            color: Qt.rgba(1, 1, 1, 0.12)
        }
    }

    /** A bar to drag, for the mouse: the buttons beside it are for the keys and the controller. */
    component DockSlider: C.Slider {
        id: ds
        focusPolicy: Qt.NoFocus
        implicitWidth: 88
        implicitHeight: 36
        padding: 0

        background: Rectangle {
            x: ds.leftPadding
            y: ds.topPadding + ds.availableHeight / 2 - height / 2
            width: ds.availableWidth
            height: 6
            radius: 3
            color: Qt.rgba(1, 1, 1, 0.14)

            Rectangle {
                width: ds.visualPosition * parent.width
                height: parent.height
                radius: 3
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: C.Theme.brandFrom }
                    GradientStop { position: 1.0; color: C.Theme.brandTo }
                }
            }
        }

        handle: Rectangle {
            x: ds.leftPadding + ds.visualPosition * (ds.availableWidth - width)
            y: ds.topPadding + ds.availableHeight / 2 - height / 2
            width: 14
            height: 14
            radius: 7
            color: "#ffffff"
            border.color: C.Theme.brandFrom
            border.width: ds.pressed ? 4 : 2
        }
    }

    // ------------------------------------------------------------ the game, dimmed

    Rectangle {
        anchors.fill: parent
        opacity: streamMenu.reveal
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: Qt.rgba(6 / 255, 4 / 255, 16 / 255, 0.30) }
            GradientStop { position: 0.55; color: Qt.rgba(6 / 255, 4 / 255, 16 / 255, 0.45) }
            GradientStop { position: 1.0; color: Qt.rgba(6 / 255, 4 / 255, 16 / 255, streamMenu.panelShown && !streamMenu.narrow ? 0.78 : 0.45) }
        }
    }

    // ------------------------------------------------------------ top: the connection, and the ways out

    Item {
        id: topBar
        x: streamMenu.margin
        y: streamMenu.margin
        width: streamMenu.width - 2 * streamMenu.margin - streamMenu.sideWidth
        readonly property bool oneRow: connection.implicitWidth + 10 + exits.implicitWidth <= width
        height: oneRow ? 44 : 96
        opacity: streamMenu.reveal
        transform: Translate { y: (1 - streamMenu.reveal) * -16 }

        Rectangle {
            id: connection
            width: Math.min(implicitWidth, topBar.width)
            height: 44
            implicitWidth: Math.ceil(connectionRow.implicitWidth) + 24
            radius: 22
            color: Qt.rgba(14 / 255, 11 / 255, 30 / 255, 0.88)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.10)

            RowLayout {
                id: connectionRow
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: 14
                    rightMargin: 8
                }
                spacing: 10

                Item {
                    id: stateDot
                    readonly property bool waiting: Chiaki.reconnecting || !streamMenu.connected
                    Layout.preferredWidth: 17
                    Layout.preferredHeight: 17

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: stateDot.waiting ? Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.18) : Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.18)
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 9
                        height: 9
                        radius: 4.5
                        color: stateDot.waiting ? C.Theme.wait : C.Theme.good

                        SequentialAnimation on opacity {
                            running: Chiaki.reconnecting || (!!streamMenu.session && !streamMenu.connected)
                            loops: Animation.Infinite
                            alwaysRunToEnd: true
                            NumberAnimation { to: 0.3; duration: 600 }
                            NumberAnimation { to: 1.0; duration: 600 }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.maximumWidth: implicitWidth
                    elide: Text.ElideRight
                    font.pixelSize: 14
                    font.weight: Font.ExtraBold
                    color: C.Theme.text
                    textFormat: Text.PlainText
                    text: {
                        if (!streamMenu.session)
                            return qsTr("Not connected");
                        if (Chiaki.reconnecting)
                            return Chiaki.reconnectStatus;
                        const where = Chiaki.settings.streamerMode ? qsTr("hidden") : streamMenu.session.host;
                        if (streamMenu.guest)
                            return streamMenu.connected ? qsTr("Playing at %1").arg(where) : qsTr("Joining %1…").arg(where);
                        if (streamMenu.hosting && streamMenu.connected)
                            return qsTr("Hosting on %1").arg(where);
                        return streamMenu.connected ? qsTr("Connected to %1").arg(where) : qsTr("Connecting to %1…").arg(where);
                    }
                }

                C.Badge {
                    visible: streamMenu.connected
                    tone: "play"
                    text: qsTr("%1 Mbps").arg(streamMenu.connected ? streamMenu.session.measuredBitrate.toFixed(1) : "0")
                }

                C.Badge {
                    readonly property real loss: streamMenu.connected && isFinite(streamMenu.session.averagePacketLoss) ? streamMenu.session.averagePacketLoss * 100 : 0
                    visible: streamMenu.connected
                    tone: loss >= 1 ? "bad" : "neutral"
                    text: qsTr("%1 % loss").arg(loss.toFixed(1))
                }

                C.Badge {
                    visible: streamMenu.connected && Chiaki.window.droppedFrames > 0
                    tone: "bad"
                    text: qsTr("%1 dropped").arg(Chiaki.window.droppedFrames)
                }

                // Going live: on air (red), on its way (amber), or why it stopped
                C.Badge {
                    readonly property var stats: Chiaki.live.stats
                    visible: Chiaki.live.live || Chiaki.live.state === "failed"
                    dot: true
                    tone: Chiaki.live.state === "live" ? "bad" : "wait"
                    text: {
                        switch (Chiaki.live.state) {
                        case "live": {
                            const secs = stats.seconds || 0;
                            const time = Math.floor(secs / 60) + ":" + String(secs % 60).padStart(2, "0");
                            let parts = [qsTr("LIVE %1").arg(time), qsTr("%1 Mbps").arg(((stats.kbps || 0) / 1000).toFixed(1))];
                            if (stats.dropped > 0)
                                parts.push(qsTr("%1 left out").arg(stats.dropped));
                            return parts.join(" · ");
                        }
                        case "connecting":
                            return qsTr("Going live…");
                        case "reconnecting":
                            return qsTr("Live: connecting again…");
                        default:
                            return qsTr("Not live: %1").arg(stats.error || qsTr("the stream stopped"));
                        }
                    }
                }
            }
        }

        // End session first in the focus chain, but on the right: see the keys above
        Row {
            id: exits
            x: topBar.width - width
            y: topBar.oneRow ? 0 : 52
            spacing: 10
            layoutDirection: Qt.RightToLeft

            C.PillButton {
                id: leaveButton
                kind: "danger"
                iconName: "leave"
                font.pixelSize: 14
                firstInFocusChain: true
                lastInFocusChain: true
                text: streamMenu.session ? (streamMenu.guest ? qsTr("Leave") : qsTr("End session")) : qsTr("Main screen")
                onClicked: streamMenu.leave()
            }

            AbstractButton {
                id: resumeButton
                readonly property bool firstInFocusChain: true
                readonly property bool lastInFocusChain: true
                implicitHeight: C.Theme.touch
                implicitWidth: resumeRow.implicitWidth + 26
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                text: qsTr("Back to game")
                font.pixelSize: 14
                font.weight: Font.ExtraBold
                Accessible.name: text

                Keys.onPressed: (event) => C.FocusChain.handle(resumeButton, event)
                onClicked: streamMenu.closeRequested()

                background: Item {
                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: C.Theme.text
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: "#000000"
                        opacity: resumeButton.down ? 0.14 : resumeButton.hovered ? 0.07 : 0
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        radius: height / 2
                        color: "transparent"
                        border.width: 2
                        border.color: C.Theme.focus
                        visible: resumeButton.visualFocus
                    }
                }

                contentItem: Item {
                    Row {
                        id: resumeRow
                        anchors.verticalCenter: parent.verticalCenter
                        x: 18
                        spacing: 8

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: resumeButton.text
                            font: resumeButton.font
                            color: C.Theme.textOnLight
                        }

                        // What also goes back: Escape, or Circle on the controller
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            implicitWidth: streamMenu.padConnected ? 24 : escText.implicitWidth + 14
                            implicitHeight: 22
                            radius: 7
                            color: Qt.rgba(42 / 255, 19 / 255, 102 / 255, 0.12)

                            Text {
                                id: escText
                                anchors.centerIn: parent
                                visible: !streamMenu.padConnected
                                font.pixelSize: 11
                                font.weight: Font.ExtraBold
                                color: C.Theme.textOnLight
                                text: qsTr("Esc")
                            }

                            Image {
                                anchors.centerIn: parent
                                visible: streamMenu.padConnected
                                width: 16
                                height: 16
                                sourceSize: Qt.size(32, 32)
                                source: streamMenu.padConnected && typeof root !== "undefined" && root.controllerButton ? root.controllerButton("moon") : ""
                            }
                        }
                    }
                }
            }

            // Going live from here, to Twitch or another service (set up under Settings → Live streaming)
            C.PillButton {
                id: liveButton
                visible: !!streamMenu.session
                kind: Chiaki.live.live ? "danger" : "ghost"
                iconName: "broadcast"
                font.pixelSize: 14
                firstInFocusChain: true
                lastInFocusChain: true
                text: Chiaki.live.live ? qsTr("Stop live") : qsTr("Go live")
                Accessible.description: qsTr("Streams what you see and hear to %1").arg(Chiaki.live.server)
                onClicked: {
                    if (Chiaki.live.live) {
                        Chiaki.live.stop();
                        return;
                    }
                    const problem = Chiaki.live.start();
                    liveProblem.text = problem;
                    liveProblemTimer.restart();
                }
            }
        }
    }

    // What is missing for going live, under the buttons for a moment
    Rectangle {
        visible: liveProblem.text.length > 0
        x: topBar.x + topBar.width - width
        y: topBar.y + topBar.height + 8
        width: Math.min(liveProblem.implicitWidth + 28, topBar.width)
        height: liveProblem.implicitHeight + 16
        radius: 12
        color: Qt.rgba(14 / 255, 11 / 255, 30 / 255, 0.92)
        border.width: 1
        border.color: Qt.rgba(0.94, 0.27, 0.43, 0.55)
        opacity: streamMenu.reveal

        Text {
            id: liveProblem
            x: 14
            y: 8
            width: parent.width - 28
            wrapMode: Text.WordWrap
            font.pixelSize: 13
            font.weight: Font.Bold
            color: "#ffc2d1"
        }

        Timer {
            id: liveProblemTimer
            interval: 8000
            onTriggered: liveProblem.text = ""
        }
    }

    // ------------------------------------------------------------ the side: Play Together

    PlayTogetherBar {
        id: panel
        visible: streamMenu.panelShown
        x: streamMenu.narrow ? streamMenu.margin : streamMenu.width - streamMenu.margin - width
        y: streamMenu.narrow ? topBar.y + topBar.height + 12 : streamMenu.margin
        width: streamMenu.panelWidth
        readonly property int room: Math.max(120, (streamMenu.narrow ? dock.y - 12 : streamMenu.height - streamMenu.margin) - y)
        height: active ? room : Math.min(room, wantedHeight)
        tileColumns: Math.max(2, Math.floor((width - 2 * inset + 8) / 168))
        opacity: streamMenu.reveal
        transform: Translate { x: streamMenu.narrow ? 0 : (1 - streamMenu.reveal) * 40; y: streamMenu.narrow ? (1 - streamMenu.reveal) * 16 : 0 }
        onChatRequested: streamMenu.chatRequested()
    }

    // ------------------------------------------------------------ bottom: sound, picture, quality, overlays

    Rectangle {
        id: dock
        x: streamMenu.margin
        y: streamMenu.height - streamMenu.margin - height
        readonly property int room: streamMenu.width - 2 * streamMenu.margin - streamMenu.sideWidth
        width: Math.min(room, Math.ceil(dockFlow.lineWidth) + 21)
        height: dockFlow.height + 16
        radius: 18
        color: Qt.rgba(14 / 255, 11 / 255, 30 / 255, 0.92)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)
        opacity: streamMenu.reveal
        transform: Translate { y: (1 - streamMenu.reveal) * 24 }

        Flow {
            id: dockFlow
            x: 10
            y: 8
            width: dock.width - 20
            spacing: 6
            /** The width of the dock on one line, with the lines between the groups. */
            readonly property real lineWidth: soundGroup.implicitWidth + pictureGroup.implicitWidth
                + (zoomGroup.visible ? zoomGroup.implicitWidth + spacing : 0)
                + qualityGroup.implicitWidth + moreGroup.implicitWidth + 3 * 3 + 6 * spacing
            /** It wraps: no lines between the groups then. */
            readonly property bool wrapped: lineWidth > width + 0.5

            // Sound: the game's volume
            Row {
                id: soundGroup
                spacing: 4

                DockButton {
                    id: volDown
                    iconName: "minus"
                    tip: qsTr("Quieter")
                    onClicked: streamMenu.setVolume(Chiaki.settings.audioVolume - 8)
                }

                DockSlider {
                    anchors.verticalCenter: parent.verticalCenter
                    from: 0
                    to: 128
                    stepSize: 1
                    value: Chiaki.settings.audioVolume
                    onMoved: Chiaki.settings.audioVolume = value
                }

                DockButton {
                    id: volUp
                    iconName: "plus"
                    tip: qsTr("Louder")
                    onClicked: streamMenu.setVolume(Chiaki.settings.audioVolume + 8)
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 42
                    horizontalAlignment: Text.AlignRight
                    rightPadding: 2
                    font.pixelSize: 13
                    font.weight: Font.ExtraBold
                    color: C.Theme.text
                    text: Math.round(Chiaki.settings.audioVolume / 128 * 100) + " %"
                }
            }

            DockLine {
                visible: !dockFlow.wrapped
            }

            // Picture: fit, zoom or stretch
            Row {
                id: pictureGroup

                C.Segmented {
                    id: pictureMode
                    implicitHeight: 36
                    readonly property var modes: [ChiakiWindow.VideoMode.Normal, ChiakiWindow.VideoMode.Zoom, ChiakiWindow.VideoMode.Stretch]
                    model: [qsTr("Fit"), qsTr("Zoom"), qsTr("Stretch")]
                    currentIndex: Math.max(0, modes.indexOf(Chiaki.window.videoMode))
                    onActivated: (index) => Chiaki.window.videoMode = modes[index]
                }
            }

            // How far to zoom in, while zooming
            Row {
                id: zoomGroup
                visible: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom
                spacing: 6

                DockButton {
                    id: zoomOut
                    iconName: "minus"
                    tip: qsTr("Zoom out")
                    onClicked: streamMenu.setZoom(Chiaki.window.ZoomFactor - 0.1)
                }

                DockSlider {
                    anchors.verticalCenter: parent.verticalCenter
                    from: -1
                    to: 4
                    stepSize: 0.01
                    value: Chiaki.window.ZoomFactor
                    onMoved: streamMenu.setZoom(value)
                }

                DockButton {
                    id: zoomIn
                    iconName: "plus"
                    tip: qsTr("Zoom in")
                    onClicked: streamMenu.setZoom(Chiaki.window.ZoomFactor + 0.1)
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 13
                    font.weight: Font.ExtraBold
                    color: C.Theme.text
                    text: {
                        const z = Chiaki.window.ZoomFactor;
                        if (z === -1)
                            return qsTr("No black bars");
                        return (z >= 0 ? z + 1 : z).toFixed(2) + " ×";
                    }
                }
            }

            DockLine {
                visible: !dockFlow.wrapped
            }

            // Quality, and what is shown on the game picture
            Row {
                id: qualityGroup
                spacing: 6

                C.ComboBox {
                    id: qualityCombo
                    // As wide as what is chosen; the list stays as wide as its longest entry
                    implicitWidth: Math.ceil(qualityMetrics.advanceWidth(displayText)) + 56
                    implicitHeight: 36
                    font.pixelSize: 13
                    font.weight: Font.Bold
                    // The text field inside brings its own 12 px on the left
                    leftPadding: 0
                    rightPadding: 30
                    topInset: 0
                    bottomInset: 0
                    Accessible.name: qsTr("Quality")
                    // VideoPreset: Fast (not offered), Default, HighQuality, HighQualitySpatial,
                    // HighQualityAdvancedSpatial, Custom
                    model: [qsTr("Default"), qsTr("High quality"), qsTr("HQ + spatial"),
                            qsTr("HQ + advanced spatial"), qsTr("Custom")]
                    currentIndex: Math.max(0, Chiaki.window.videoPreset - 1)
                    onActivated: (index) => {
                        Chiaki.window.videoPreset = index + 1;
                        Chiaki.settings.videoPreset = index + 1;
                    }
                    // The dock is at the bottom: the list opens upwards, inside the window
                    popup.y: -popup.height - 6

                    background: Rectangle {
                        implicitWidth: 100
                        implicitHeight: 36
                        radius: 10
                        color: Qt.rgba(1, 1, 1, qualityCombo.hovered ? 0.09 : 0.05)
                        border.width: qualityCombo.visualFocus ? 2 : 1
                        border.color: qualityCombo.visualFocus ? C.Theme.focus : Qt.rgba(1, 1, 1, 0.12)
                    }
                    indicator: C.Icon {
                        x: qualityCombo.width - width - 10
                        y: (qualityCombo.height - height) / 2
                        iconName: "chevron-down"
                        size: 14
                        color: C.Theme.textSoft
                    }
                    FontMetrics {
                        id: qualityMetrics
                        font: qualityCombo.font
                    }
                    popup.background: Rectangle {
                        radius: 12
                        color: C.Theme.popup
                        border.width: 1
                        border.color: C.Theme.lineStrong
                    }
                }

                DockButton {
                    id: placeboButton
                    visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                    text: qsTr("Placebo…")
                    onClicked: streamMenu.placeboSettingsRequested()
                }

                DockButton {
                    id: overlaysButton
                    iconName: "layers"
                    text: qsTr("Overlays")
                    chevron: true
                    onClicked: panel.openMenu(overlaysMenu, overlaysButton, "above", streamMenu)

                    C.Menu {
                        id: overlaysMenu
                        property var placement: null
                        onAboutToShow: if (placement) placement()
                        padding: 6
                        font.pixelSize: 14
                        background: PlayTogetherBar.MenuGround {}

                        // What is shown on the game picture: switch between the saved arrangements
                        Instantiator {
                            model: Chiaki.hud ? Chiaki.hud.presets : []
                            delegate: PlayTogetherBar.MenuEntry {
                                required property int index
                                required property string modelData
                                text: modelData
                                chosen: !!Chiaki.hud && Chiaki.hud.active === index
                                onTriggered: Chiaki.hud.active = index
                            }
                            onObjectAdded: (index, object) => overlaysMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => overlaysMenu.removeItem(object)
                        }

                        PlayTogetherBar.MenuLine {}

                        PlayTogetherBar.MenuEntry {
                            iconName: "layers"
                            text: qsTr("Arrange")
                            onTriggered: streamMenu.arrangeRequested()
                        }

                        PlayTogetherBar.MenuEntry {
                            // What is shown at all, on the game picture and in OBS
                            iconName: "sliders"
                            text: qsTr("Options…")
                            onTriggered: streamMenu.displayPanelRequested()
                        }
                    }
                }

                // Stream scenes: what viewers see instead of the game (Starting soon, Be right
                // back, ...), in the app's own live stream and in OBS (/scene/current)
                DockButton {
                    id: sceneButton
                    readonly property string shownId: Chiaki.scenes ? Chiaki.scenes.current : "game"
                    visible: !!Chiaki.scenes && (Chiaki.live.live || !!Chiaki.settings.overlayEnabled)
                    iconName: "broadcast"
                    // Lit while a scene is up; which one, the menu and the note at the top say
                    text: qsTr("Scenes")
                    checked: shownId !== "game"
                    chevron: true
                    onClicked: panel.openMenu(scenesMenu, sceneButton, "above", streamMenu)

                    C.Menu {
                        id: scenesMenu
                        property var placement: null
                        onAboutToShow: if (placement) placement()
                        padding: 6
                        font.pixelSize: 14
                        background: PlayTogetherBar.MenuGround {}

                        Instantiator {
                            model: Chiaki.scenes ? Chiaki.scenes.scenes : []
                            delegate: PlayTogetherBar.MenuEntry {
                                required property int index
                                required property var modelData
                                text: modelData.name
                                chosen: !!Chiaki.scenes && Chiaki.scenes.current === modelData.id
                                onTriggered: Chiaki.scenes.show(modelData.id)
                            }
                            onObjectAdded: (index, object) => scenesMenu.insertItem(index, object)
                            onObjectRemoved: (index, object) => scenesMenu.removeItem(object)
                        }

                        PlayTogetherBar.MenuLine {}

                        PlayTogetherBar.MenuEntry {
                            iconName: "refresh"
                            text: qsTr("Start the countdown again")
                            onTriggered: Chiaki.scenes.restartCountdown()
                        }
                    }
                }

                // Twitch alerts: what is up now and waiting, and a pause
                DockButton {
                    id: alertsButton
                    readonly property int waiting: Chiaki.alerts ? Chiaki.alerts.waiting + (Chiaki.alerts.current.kind ? 1 : 0) : 0
                    visible: !!Chiaki.twitch && Chiaki.twitch.state === "logged_in"
                    iconName: "heart"
                    text: waiting > 0 ? qsTr("Alerts · %1").arg(waiting) : qsTr("Alerts")
                    checked: !!Chiaki.alerts && Chiaki.alerts.paused
                    chevron: true
                    onClicked: panel.openMenu(alertsMenu, alertsButton, "above", streamMenu)

                    C.Menu {
                        id: alertsMenu
                        property var placement: null
                        onAboutToShow: if (placement) placement()
                        padding: 6
                        font.pixelSize: 14
                        background: PlayTogetherBar.MenuGround {}

                        PlayTogetherBar.MenuEntry {
                            enabled: !!Chiaki.alerts && !!Chiaki.alerts.current.kind
                            iconName: "forward"
                            text: qsTr("Skip this alert")
                            onTriggered: Chiaki.alerts.skip()
                        }

                        PlayTogetherBar.MenuEntry {
                            enabled: alertsButton.waiting > 0
                            iconName: "trash"
                            text: qsTr("Clear all (%1)").arg(alertsButton.waiting)
                            onTriggered: Chiaki.alerts.clear()
                        }

                        PlayTogetherBar.MenuEntry {
                            iconName: Chiaki.alerts && Chiaki.alerts.paused ? "play" : "moon"
                            text: Chiaki.alerts && Chiaki.alerts.paused ? qsTr("Show alerts again") : qsTr("Hold alerts for now")
                            onTriggered: Chiaki.alerts.paused = !Chiaki.alerts.paused
                        }

                        PlayTogetherBar.MenuEntry {
                            visible: !!Chiaki.viewerActions && Chiaki.viewerActions.enabled
                            height: visible ? implicitHeight : 0
                            iconName: "close"
                            text: qsTr("Stop viewer actions")
                            onTriggered: Chiaki.viewerActions.stopAll()
                        }

                        PlayTogetherBar.MenuLine {}

                        PlayTogetherBar.MenuEntry {
                            iconName: "heart"
                            text: qsTr("Test an alert")
                            onTriggered: Chiaki.alerts.test("follow")
                        }
                    }
                }
            }

            DockLine {
                visible: !dockFlow.wrapped
            }

            // The console's microphone, and how the stream is shown on this screen
            Row {
                id: moreGroup
                spacing: 6

                DockButton {
                    id: muteButton
                    // The console's own microphone; voice chat has its own switch in the panel
                    visible: !streamMenu.guest
                    enabled: streamMenu.connected
                    checked: streamMenu.connected && !streamMenu.session.muted
                    iconName: checked ? "mic" : "mic-off"
                    tip: checked ? qsTr("Console mic on") : qsTr("Console mic off")
                    onClicked: streamMenu.session.muted = !streamMenu.session.muted
                }

                DockButton {
                    id: mouseAimButton
                    // Mouse and keyboard as a controller: whether the mouse aims while playing.
                    // Its keys and how it aims are under Settings → Keyboard & mouse
                    visible: Chiaki.settings.keyboardEnabled
                    checked: Chiaki.keyMouse.aim
                    iconName: "keyboard"
                    text: qsTr("Mouse aims")
                    tip: !checked ? qsTr("Aim with the mouse and walk with the keys, as in a game on a computer")
                         : Chiaki.keyMouse.catchKey.length > 0
                           ? qsTr("While you play, the mouse is caught and aims. %1 lets it go and catches it again.").arg(Chiaki.keyMouse.catchKey)
                           : qsTr("While you play, the mouse is caught and aims. This menu lets it go.")
                    onClicked: Chiaki.keyMouse.aim = !Chiaki.keyMouse.aim
                }

                DockButton {
                    id: displaySettingsButton
                    iconName: "sliders"
                    tip: qsTr("Display settings")
                    lastInFocusChain: true
                    onClicked: streamMenu.displaySettingsRequested()
                }
            }
        }
    }
}
