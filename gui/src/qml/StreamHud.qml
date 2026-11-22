import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window
import QtQuick.Shapes

import org.streetpea.chiaking

import "controls" as C
import "scenes"

// The display on top of the game picture: who is playing, their webcams, what
// everybody presses and what the console gets out of it. Four elements, each
// with a switch, a place and a size (Chiaki.hud.values), kept as presets.
//
// An item that fills the game picture. It is either part of the stream window's
// own scene, which the app lays over the game, or sits in a see-through window
// of its own (StreamHudWindow); StreamView.hudInline says which and why. While
// playing it lets every click and key through to the game; in arrange mode it
// takes the mouse so that the elements can be dragged.
Item {
    id: hud

    /** The stream view: gives the place of the game picture on screen. */
    required property Item view
    /** False while something else is in front (the stream menu, an error). */
    property bool allowed: true
    property bool editing: false
    /** The chat window is open: new messages are seen there, no notes needed. */
    property bool chatOpen: false
    /**
     * Drawn for our live stream (StreamView's live overlay), never on screen: no chat, no
     * questions for the host, and whoever asked to be left out of streams appears without
     * name, picture or webcam.
     */
    property bool broadcast: false

    readonly property var cfg: Chiaki.hud.values
    readonly property var session: Chiaki.session
    readonly property bool shared: !!session && session.coplayActive
    // "In control" only says something when not everybody is
    readonly property bool exclusive: shared && (session.coplayMixMode === 1 || session.coplayMixMode === 2)
    readonly property var talking: shared ? session.coplayTalking : ({})
    readonly property int camSerial: shared ? session.coplayCamSerial : 0

    // Everybody in the session; alone it is just us, if that is wanted
    readonly property var players: {
        if (!session)
            return [];
        let list = session.coplayPlayers;
        if (list.length === 0)
            return (cfg.solo || editing) ? [session.coplayOwnPlayer()] : [];
        if (!broadcast)
            return list;
        return list.map((p) => (p.noStreams && !p.you)
            ? Object.assign({}, p, { name: qsTr("Player %1").arg(p.slot + 1), avatarUrl: "", camera: false })
            : p);
    }
    readonly property var camPlayers: players.filter((p) => !!p.camera)
    // Whose controller is drawn: everybody with one (any kind, or the keyboard), not who has none
    readonly property var padPlayers: players.filter((p) => p.controller === undefined || p.controller !== 0)
    readonly property bool wantPads: editing || ((cfg.pads_show || cfg.combined_show) && players.length > 0)
    property var padStates: ({})

    readonly property string padSkin: cfg.skin && cfg.skin !== "auto" && String(cfg.skin).startsWith("PS5") ? cfg.skin : "PS5"

    readonly property color accent: "#8c52ff"
    readonly property color cyan: "#00f2ff"
    readonly property color green: "#34d399"
    readonly property color red: "#f0466e"
    readonly property color glass: Qt.rgba(12 / 255, 10 / 255, 26 / 255, 0.72)
    // "transparent": nothing behind names and pictures; "glass": see-through boxes; "solid": dark boxes
    readonly property bool plain: cfg.style !== "glass" && cfg.style !== "solid"
    readonly property color boxColor: plain ? "transparent" : (cfg.style === "solid" ? "#10101a" : glass)

    readonly property bool separatePads: !!cfg.pads_separate
    /**
     * Whether there is anything to show right now. Whoever hosts this (the stream view or
     * the window) goes by this, never by `visible`: that is also false while the host
     * itself is hidden, and a host waiting for it would never show again.
     */
    readonly property bool shown: allowed && !!session && (editing || playersElement.wanted || camsElement.wanted || padsElement.wanted
                                      || combinedElement.wanted || (separatePads && cfg.pads_show && players.length > 0)
                                      || toasts.count > 0 || chatNotes.count > 0 || twitchLayer.shown)
    visible: shown

    Keys.onEscapePressed: editing = false

    // What everybody presses, while something shows it
    Timer {
        interval: 33
        repeat: true
        running: hud.shown && hud.wantPads
        onTriggered: hud.padStates = hud.session ? hud.session.coplayPadStates() : ({})
    }

    // A guest only gets the others' inputs when it asks for them
    readonly property bool needInputs: wantPads || Chiaki.settings.overlayEnabled
    // The display on screen asks for them; the stream's copy goes by what that one gets
    onNeedInputsChanged: if (session && !broadcast) session.coplaySetWantInputs(needInputs)
    onSessionChanged: if (session && !broadcast) session.coplaySetWantInputs(needInputs)

    Shortcut {
        enabled: hud.editing
        sequence: StandardKey.Cancel
        onActivated: hud.editing = false
    }

    // ------------------------------------------------------------ building blocks

    component Battery: Row {
        id: battery
        /** 0..100, 254 = on a cable, 255 = unknown */
        property int level: 255
        readonly property bool cable: level === 254
        visible: hud.cfg.battery && level !== 255
        spacing: 3

        // On its battery: how full it is
        Rectangle {
            visible: !battery.cable
            anchors.verticalCenter: parent.verticalCenter
            width: 20
            height: 10
            radius: 2.5
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.75)

            Rectangle {
                x: 2
                y: 2
                height: parent.height - 4
                width: Math.max(2, (parent.width - 4) * Math.min(1, battery.level / 100))
                radius: 1
                color: battery.level <= 20 ? hud.red : (battery.level <= 50 ? "#f5b942" : hud.green)
            }

            Rectangle {
                anchors.left: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 2
                height: 4
                color: Qt.rgba(1, 1, 1, 0.75)
            }
        }

        // On its cable: a plug (two prongs, the body, the cord), as on the page for browsers
        Shape {
            visible: battery.cable
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 12
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: hud.cyan
                strokeWidth: 1.4
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: "M1 4.5h3M1 7.5h3M5.6 2.5h1.8a1.6 1.6 0 0 1 1.6 1.6v3.8a1.6 1.6 0 0 1-1.6 1.6h-1.8a1.6 1.6 0 0 1-1.6-1.6v-3.8a1.6 1.6 0 0 1 1.6-1.6zM9 6h2.5a2.5 2.5 0 0 0 2.5-2.5V2" }
            }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: "white"
            opacity: 0.85
            font.pixelSize: 10
            font.bold: true
            text: battery.cable ? qsTr("cable") : battery.level + " %"
        }
    }

    /** One of the four things that can be shown, placed and sized. */
    component Element: Item {
        id: el
        /** "players", "cams", "pads" or "combined": the prefix of its settings. */
        property string key
        property string title
        /** Whether there is anything to show right now. */
        property bool hasContent: true
        /** False while this element does not apply at all (the other way of showing it is chosen). */
        property bool available: true
        /** The setting that switches it; several elements can share one. */
        property string showKey: key + "_show"
        default property alias content: holder.data

        readonly property bool on: !!hud.cfg[showKey]
        readonly property bool wanted: available && on && hasContent
        readonly property real size: Math.max(0.4, Math.min(2.5, (hud.cfg[key + "_scale"] || 100) / 100))
        property real dragX: -1
        property real dragY: -1

        visible: available && (hud.editing || wanted)
        width: Math.max(hud.editing ? 150 : 1, holder.childrenRect.width * size)
        height: Math.max(hud.editing ? 60 : 1, holder.childrenRect.height * size)
        x: dragX >= 0 ? dragX : Math.round((hud.cfg[key + "_x"] || 0) * Math.max(0, hud.width - width - 24) + 12)
        y: dragY >= 0 ? dragY : Math.round((hud.cfg[key + "_y"] || 0) * Math.max(0, hud.height - height - 24) + 12)
        opacity: hud.editing ? (on ? 1 : 0.4) : (hud.cfg.opacity || 100) / 100

        function resize(step) {
            Chiaki.hud.set(key + "_scale", Math.max(40, Math.min(250, Math.round(size * 100) + step)));
        }

        Item {
            id: holder
            scale: el.size
            transformOrigin: Item.TopLeft
        }

        // Arrange mode: a dashed frame to grab, and the element's own switches
        Shape {
            visible: hud.editing
            x: -6
            y: -6
            width: el.width + 12
            height: el.height + 12
            ShapePath {
                strokeColor: mover.pressed ? "white" : Qt.rgba(0, 242 / 255, 1, 0.85)
                strokeWidth: 2
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 2]
                fillColor: mover.pressed ? Qt.rgba(0, 242 / 255, 1, 0.10) : "transparent"
                PathRectangle { x: 1; y: 1; width: el.width + 10; height: el.height + 10; radius: 14 }
            }
        }

        Item {
            visible: hud.editing
            anchors.fill: parent
            anchors.margins: -6

            MouseArea {
                id: mover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                property real grabX: 0
                property real grabY: 0
                onPressed: (mouse) => {
                    let p = mapToItem(hud, mouse.x, mouse.y);
                    grabX = p.x - el.x;
                    grabY = p.y - el.y;
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return;
                    let p = mapToItem(hud, mouse.x, mouse.y);
                    el.dragX = Math.max(12, Math.min(hud.width - el.width - 12, p.x - grabX));
                    el.dragY = Math.max(12, Math.min(hud.height - el.height - 12, p.y - grabY));
                }
                onReleased: {
                    if (el.dragX < 0)
                        return;
                    let freeX = Math.max(1, hud.width - el.width - 24);
                    let freeY = Math.max(1, hud.height - el.height - 24);
                    Chiaki.hud.set(el.key + "_x", (el.dragX - 12) / freeX);
                    Chiaki.hud.set(el.key + "_y", (el.dragY - 12) / freeY);
                    el.dragX = -1;
                    el.dragY = -1;
                }
                onWheel: (wheel) => el.resize(wheel.angleDelta.y > 0 ? 10 : -10)
            }

            // Its switches: above the frame, or below it where there is no room above;
            // starting at the left edge, or ending at the right one near the right border
            Row {
                readonly property bool below: el.y < 44
                readonly property bool fromRight: el.x + width > hud.width - 12
                x: fromRight ? parent.width - width : 0
                y: below ? parent.height + 4 : -height - 4
                spacing: 4

                Rectangle {
                    height: 24
                    width: titleRow.implicitWidth + 20
                    radius: 12
                    color: "#7ff5ff"
                    Row {
                        id: titleRow
                        anchors.centerIn: parent
                        spacing: 6
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 12
                            height: 12
                            sourceSize: Qt.size(24, 24)
                            source: "qrc:/twitch/move.svg"
                        }
                        Text {
                            id: titleText
                            anchors.verticalCenter: parent.verticalCenter
                            color: "#04262a"
                            font.pixelSize: 12
                            font.bold: true
                            text: el.title
                        }
                    }
                }

                Repeater {
                    model: [
                        { label: el.on ? qsTr("Hide") : qsTr("Show"), action: "toggle" },
                        { label: "−", action: "smaller" },
                        { label: "+", action: "larger" },
                    ]

                    Rectangle {
                        required property var modelData
                        height: 24
                        width: Math.max(24, tool.implicitWidth + 16)
                        radius: 12
                        color: toolArea.containsMouse ? hud.accent : Qt.rgba(8 / 255, 6 / 255, 20 / 255, 0.88)
                        border.width: 1
                        border.color: Qt.rgba(0, 242 / 255, 1, 0.5)
                        Text {
                            id: tool
                            anchors.centerIn: parent
                            color: "white"
                            font.pixelSize: 12
                            font.bold: true
                            text: parent.modelData.label
                        }
                        MouseArea {
                            id: toolArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (parent.modelData.action === "toggle")
                                    Chiaki.hud.set(el.showKey, !el.on);
                                else
                                    el.resize(parent.modelData.action === "larger" ? 10 : -10);
                            }
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ the elements

    // The Twitch elements (latest follower, goals, poll, chat, ...): placed on a 16:9 stage of
    // their own, the same as in the app's own stream and on the OBS overlay page
    TwitchLayer {
        id: twitchLayer
        anchors.fill: parent
        editing: hud.editing
        visible: shown
        enabled: hud.editing
    }

    Element {
        id: playersElement
        key: "players"
        title: qsTr("Players")
        hasContent: hud.players.length > 0 && (hud.shared || hud.cfg.solo)

        Grid {
            columns: hud.cfg.vertical ? 1 : Math.max(1, hud.players.length)
            spacing: 6

            Repeater {
                model: hud.players

                Rectangle {
                    id: chip
                    required property var modelData
                    readonly property bool isTalking: hud.cfg.talking && !!hud.talking[String(modelData.slot)]
                    readonly property bool isMuted: !!modelData.muted || !!modelData.silenced
                    width: chipRow.implicitWidth + 20
                    height: 46
                    radius: 23
                    color: hud.boxColor
                    border.width: hud.plain ? 0 : (modelData.inControl && hud.exclusive ? 2 : 1)
                    border.color: modelData.inControl && hud.exclusive ? hud.accent : Qt.rgba(1, 1, 1, 0.14)
                    opacity: hud.exclusive && !modelData.inControl ? 0.7 : 1.0

                    Row {
                        id: chipRow
                        anchors.verticalCenter: parent.verticalCenter
                        x: 6
                        spacing: 8

                        // Picture or initial; the ring lights up while talking
                        C.Avatar {
                            visible: hud.cfg.avatars
                            anchors.verticalCenter: parent.verticalCenter
                            width: 34
                            height: 34
                            source: chip.modelData.avatarUrl || ""
                            name: chip.modelData.name || ""
                            ringWidth: 2
                            ringColor: chip.isTalking ? hud.green : "transparent"
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1

                            Text {
                                visible: hud.cfg.names
                                color: chip.isTalking ? hud.green : (hud.plain && chip.modelData.inControl && hud.exclusive ? hud.cyan : "white")
                                font.pixelSize: 14
                                font.bold: true
                                // Without a box behind it, an outline keeps it readable on any game picture
                                style: hud.plain ? Text.Outline : Text.Normal
                                styleColor: Qt.rgba(0, 0, 0, 0.85)
                                text: chip.modelData.name || qsTr("Player %1").arg(chip.modelData.slot + 1)
                            }

                            Row {
                                spacing: 6

                                // Each their own controller: the player the game sees
                                Text {
                                    visible: (chip.modelData.player || 0) > 0
                                    color: "#7dd3fc"
                                    font.pixelSize: 9
                                    font.bold: true
                                    text: qsTr("P%1").arg(chip.modelData.player)
                                }

                                Text {
                                    visible: !!chip.modelData.host
                                    color: hud.cyan
                                    font.pixelSize: 9
                                    font.bold: true
                                    text: qsTr("HOST")
                                }

                                // Streams this session live right now (Twitch, say)
                                Rectangle {
                                    visible: !!chip.modelData.live
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: liveText.implicitWidth + 8
                                    height: 12
                                    radius: 3
                                    color: "#e91916"

                                    Text {
                                        id: liveText
                                        anchors.centerIn: parent
                                        color: "white"
                                        font.pixelSize: 8
                                        font.bold: true
                                        text: qsTr("LIVE")
                                    }
                                }

                                Text {
                                    // Not allowed to play, or only the host plays ("Only me")
                                    visible: hud.shared && !chip.modelData.host && (!chip.modelData.canControl || hud.session.coplayMixMode === 2)
                                    color: "white"
                                    opacity: 0.6
                                    font.pixelSize: 9
                                    font.bold: true
                                    text: qsTr("WATCHING")
                                }

                                Text {
                                    visible: chip.isMuted
                                    color: hud.red
                                    font.pixelSize: 9
                                    font.bold: true
                                    text: qsTr("MUTED")
                                }

                                Battery {
                                    level: chip.modelData.battery === undefined ? 255 : chip.modelData.battery
                                }
                            }
                        }

                        Item { width: 4; height: 1 }
                    }
                }
            }
        }
    }

    Element {
        id: camsElement
        key: "cams"
        title: qsTr("Webcams")
        hasContent: hud.camPlayers.length > 0

        Row {
            spacing: 8

            Repeater {
                // In arrange mode without a webcam running: a stand-in to place
                model: hud.camPlayers.length > 0 ? hud.camPlayers : (hud.editing ? [{ slot: -1, name: qsTr("Webcam") }] : [])

                Rectangle {
                    id: camTile
                    required property var modelData
                    readonly property bool isTalking: hud.cfg.talking && !!hud.talking[String(modelData.slot)]
                    width: 176
                    height: 99
                    radius: 10
                    color: "#12121c"
                    border.width: 2
                    border.color: isTalking ? hud.green : Qt.rgba(1, 1, 1, 0.25)
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 2
                        cache: false
                        fillMode: Image.PreserveAspectCrop
                        source: camTile.modelData.slot >= 0 ? "image://coplaycam/" + camTile.modelData.slot + "/" + hud.camSerial : ""
                    }

                    Rectangle {
                        anchors {
                            left: parent.left
                            bottom: parent.bottom
                            margins: 6
                        }
                        visible: hud.cfg.names
                        radius: 7
                        color: Qt.rgba(0, 0, 0, 0.6)
                        width: Math.min(camName.implicitWidth + 12, parent.width - 12)
                        height: 18

                        Text {
                            id: camName
                            anchors {
                                fill: parent
                                leftMargin: 6
                                rightMargin: 6
                            }
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            font.pixelSize: 11
                            font.bold: true
                            color: "white"
                            text: camTile.modelData.name || ""
                        }
                    }
                }
            }
        }
    }

    Element {
        id: padsElement
        key: "pads"
        title: qsTr("Everybody's controller")
        hasContent: hud.padPlayers.length > 0
        available: !hud.separatePads

        Row {
            spacing: 10

            Repeater {
                model: hud.padPlayers

                Column {
                    id: padColumn
                    required property var modelData
                    spacing: 2

                    C.PadView {
                        skin: hud.padSkin
                        factor: 0.2
                        pad: hud.padStates[String(padColumn.modelData.slot)] || null
                        opacity: padColumn.modelData.inControl || !hud.exclusive ? 1.0 : 0.55
                    }

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        visible: hud.cfg.names
                        height: 18
                        width: padName.implicitWidth + 14
                        radius: 9
                        color: padColumn.modelData.inControl && hud.exclusive ? hud.accent : Qt.rgba(0, 0, 0, 0.6)

                        Text {
                            id: padName
                            anchors.centerIn: parent
                            color: "white"
                            font.pixelSize: 11
                            font.bold: true
                            text: padColumn.modelData.name || qsTr("Player %1").arg(padColumn.modelData.slot + 1)
                        }
                    }
                }
            }
        }
    }

    // Each player's controller by itself, with a place and a size of its own
    Repeater {
        model: hud.separatePads ? hud.padPlayers : []

        Element {
            id: ownPad
            required property var modelData
            key: "pad" + modelData.slot
            showKey: "pads_show"
            title: modelData.name || qsTr("Player %1").arg(modelData.slot + 1)

            Column {
                spacing: 2

                C.PadView {
                    skin: hud.padSkin
                    factor: 0.2
                    pad: hud.padStates[String(ownPad.modelData.slot)] || null
                    opacity: ownPad.modelData.inControl || !hud.exclusive ? 1.0 : 0.55
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: hud.cfg.names
                    height: 18
                    width: ownPadName.implicitWidth + 14
                    radius: 9
                    color: ownPad.modelData.inControl && hud.exclusive ? hud.accent : Qt.rgba(0, 0, 0, 0.6)

                    Text {
                        id: ownPadName
                        anchors.centerIn: parent
                        color: "white"
                        font.pixelSize: 11
                        font.bold: true
                        text: ownPad.title
                    }
                }
            }
        }
    }

    Element {
        id: combinedElement
        key: "combined"
        title: qsTr("What the console gets")
        hasContent: hud.players.length > 0

        Column {
            spacing: 2

            C.PadView {
                skin: hud.padSkin
                factor: 0.2
                pad: hud.padStates["combined"] || null
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: hud.cfg.names
                height: 18
                width: consoleName.implicitWidth + 14
                radius: 9
                color: Qt.rgba(0, 0.4, 0.45, 0.85)

                Text {
                    id: consoleName
                    anchors.centerIn: parent
                    color: "white"
                    font.pixelSize: 11
                    font.bold: true
                    text: qsTr("Console")
                }
            }
        }
    }

    // ------------------------------------------------------------ joined / left

    property var knownPlayers: ({})
    property bool knownReady: false

    onPlayersChanged: {
        if (!shared) {
            knownPlayers = {};
            knownReady = false;
            return;
        }
        let now = {};
        for (let i = 0; i < players.length; ++i)
            now[players[i].slot] = players[i].name || qsTr("Player %1").arg(players[i].slot + 1);
        // Not for everybody who was already there when we came in
        if (knownReady && cfg.notices) {
            for (let slot in now) {
                if (!(slot in knownPlayers))
                    toasts.append({ text: qsTr("%1 joined").arg(now[slot]), joined: true });
            }
            for (let slot in knownPlayers) {
                if (!(slot in now))
                    toasts.append({ text: qsTr("%1 left").arg(knownPlayers[slot]), joined: false });
            }
        }
        knownPlayers = now;
        if (players.length > 0)
            knownReady = true;
    }

    ListModel {
        id: toasts
    }

    Column {
        anchors {
            top: parent.top
            topMargin: 18
            horizontalCenter: parent.horizontalCenter
        }
        spacing: 6

        Repeater {
            model: toasts

            Rectangle {
                id: toast
                required property string text
                required property bool joined
                required property int index
                anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
                width: toastText.implicitWidth + 44
                height: 34
                radius: 17
                color: hud.glass
                border.width: 1
                border.color: joined ? hud.green : Qt.rgba(1, 1, 1, 0.3)
                opacity: 0

                Rectangle {
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: toast.joined ? hud.green : hud.red
                }

                Text {
                    id: toastText
                    x: 30
                    anchors.verticalCenter: parent.verticalCenter
                    color: "white"
                    font.pixelSize: 14
                    font.bold: true
                    text: toast.text
                }

                SequentialAnimation {
                    running: true
                    NumberAnimation { target: toast; property: "opacity"; to: 1; duration: 200 }
                    PauseAnimation { duration: 3800 }
                    NumberAnimation { target: toast; property: "opacity"; to: 0; duration: 400 }
                    ScriptAction { script: toasts.remove(0) }
                }
            }
        }
    }

    // ------------------------------------------------------------ chat

    ListModel {
        id: chatNotes
    }

    // Somebody knocks on the public room: say so on the game picture, the answer is in the menu
    Connections {
        target: hud.session
        function onCoplayKnock(name) {
            if (hud.broadcast)
                return;
            toasts.append({ text: qsTr("%1 wants to join · open the menu to answer").arg(name), joined: true });
        }
    }

    Connections {
        target: hud.session
        function onCoplayChatMessage(message) {
            // What the players write to each other stays between them
            if (!hud.cfg.chat || hud.chatOpen || hud.editing || hud.broadcast)
                return;
            let player = null;
            for (let i = 0; i < hud.players.length; ++i) {
                if (hud.players[i].slot === message.slot)
                    player = hud.players[i];
            }
            let text = message.text || "";
            if (!text && message.embed)
                text = message.embed.title || message.embed.url || "";
            chatNotes.append({
                name: message.name || qsTr("Player %1").arg(message.slot + 1),
                text: text,
                avatar: player ? (player.avatarUrl || "") : "",
                link: !!message.embed
            });
            while (chatNotes.count > 4)
                chatNotes.remove(0);
        }
    }

    Column {
        anchors {
            left: parent.left
            bottom: parent.bottom
            leftMargin: 18
            bottomMargin: 18
        }
        spacing: 6
        width: Math.min(460, hud.width * 0.45)

        Repeater {
            model: chatNotes

            Rectangle {
                id: note
                required property string name
                required property string text
                required property string avatar
                required property bool link
                width: Math.min(parent.width, noteRow.implicitWidth + 24)
                height: noteRow.implicitHeight + 14
                radius: 14
                color: hud.plain ? Qt.rgba(0, 0, 0, 0.45) : hud.boxColor
                border.width: hud.plain ? 0 : 1
                border.color: Qt.rgba(1, 1, 1, 0.14)
                opacity: 0

                Row {
                    id: noteRow
                    x: 10
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    C.Avatar {
                        width: 24
                        height: 24
                        anchors.verticalCenter: parent.verticalCenter
                        source: note.avatar
                        name: note.name
                    }

                    Text {
                        width: Math.min(implicitWidth, note.parent.width - 60)
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        color: "white"
                        font.pixelSize: 14
                        textFormat: Text.StyledText
                        style: hud.plain ? Text.Outline : Text.Normal
                        styleColor: Qt.rgba(0, 0, 0, 0.6)
                        text: "<b>" + note.name.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</b>  "
                              + note.text.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                              + (note.link ? "  🔗" : "")
                    }
                }

                SequentialAnimation {
                    running: true
                    NumberAnimation { target: note; property: "opacity"; to: 1; duration: 200 }
                    PauseAnimation { duration: 7000 }
                    NumberAnimation { target: note; property: "opacity"; to: 0; duration: 500 }
                    ScriptAction { script: if (chatNotes.count > 0) chatNotes.remove(0) }
                }
            }
        }
    }

    // ------------------------------------------------------------ arrange mode

    Rectangle {
        visible: hud.editing
        anchors.fill: parent
        z: -1
        color: Qt.rgba(0, 0, 0, 0.35)
    }

    // "Arranging – drag anything where you want it", with Reset and Done
    Rectangle {
        visible: hud.editing
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.round(parent.height * 0.8 - height / 2)
        width: Math.min(parent.width - 40, editRow.implicitWidth + 28)
        height: 42
        radius: 21
        color: Qt.rgba(8 / 255, 6 / 255, 20 / 255, 0.88)
        border.width: 1
        border.color: Qt.rgba(0, 242 / 255, 1, 0.5)

        // Takes the clicks that would otherwise reach an element below
        MouseArea {
            anchors.fill: parent
        }

        RowLayout {
            id: editRow
            anchors.verticalCenter: parent.verticalCenter
            x: 18
            spacing: 10

            Label {
                font.pixelSize: 15
                font.bold: true
                color: "white"
                text: qsTr("Arranging – drag anything where you want it")
            }

            Rectangle {
                Layout.preferredHeight: 30
                Layout.preferredWidth: resetText.implicitWidth + 28
                radius: 15
                color: resetArea.containsMouse ? Qt.rgba(1, 1, 1, 0.16) : Qt.rgba(1, 1, 1, 0.07)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.16)
                Text {
                    id: resetText
                    anchors.centerIn: parent
                    color: "white"
                    font.pixelSize: 14
                    font.bold: true
                    text: qsTr("Reset")
                }
                MouseArea {
                    id: resetArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    ToolTip.visible: containsMouse
                    ToolTip.text: qsTr("Everything back to its first place and size")
                    onClicked: {
                        Chiaki.hud.resetPlacement();
                        if (Chiaki.twitchElements)
                            Chiaki.twitchElements.resetPlaces();
                    }
                }
            }

            Rectangle {
                Layout.preferredHeight: 30
                Layout.preferredWidth: doneText.implicitWidth + 28
                radius: 15
                color: doneArea.containsMouse ? "#7ff5ff" : hud.cyan
                Text {
                    id: doneText
                    anchors.centerIn: parent
                    color: "#04262a"
                    font.pixelSize: 15
                    font.bold: true
                    text: qsTr("Done")
                }
                MouseArea {
                    id: doneArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: hud.editing = false
                }
            }
        }
    }
}
