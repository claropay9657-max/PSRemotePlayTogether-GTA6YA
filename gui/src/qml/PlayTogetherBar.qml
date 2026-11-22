import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window

import org.streetpea.chiaking

import "controls" as C

// Play Together in the stream menu: a panel beside the game picture. From the top: the
// room and its invites, who knocks, who controls, everybody who is here (with their
// webcam), one's own microphone, voices and webcam, and the chat. The host lets players
// in and out here; a click on a player opens what can be done with them, to the left of
// the panel where there is room. Scrolls where the window is too small for all of it.
Rectangle {
    id: panel

    /** Open the big chat (StreamChatPanel). */
    signal chatRequested()

    readonly property var session: Chiaki.session
    readonly property bool active: !!session && session.coplayActive
    readonly property bool guest: !!session && session.coplayGuest
    readonly property bool voiceOn: active && session.coplayVoiceOn
    readonly property bool hosting: active && session.coplayHosting
    readonly property bool publicRoom: hosting && session.coplayPublic
    readonly property var players: active ? session.coplayPlayers : []
    readonly property var pending: hosting ? (session.coplayPending || []) : []
    readonly property var talking: active ? session.coplayTalking : ({})
    readonly property int camSerial: active ? session.coplayCamSerial : 0
    readonly property var messages: active ? (session.coplayChat || []) : []
    readonly property int unread: active ? session.coplayChatUnread : 0
    readonly property var hostPlayer: players.find((p) => p.host) || null
    /** Who controls: only the host. Guests watch then, whatever they may do otherwise. */
    readonly property bool hostOnly: active && session.coplayMixMode === 2

    /** Whether a player's buttons reach the console now: the host always, guests when allowed and not "Only me". */
    /** Each their own controller (a game on this computer): every player has a number. */
    readonly property bool eachOwn: active && session.coplayMixMode === 3

    function playsNow(p) {
        // Each their own controller: a guest without a controller of its own watches
        return !!p && (!!p.host || (!!p.canControl && !hostOnly && (!eachOwn || p.player > 0)));
    }

    /** Player tiles side by side: two in the side panel, more when it has the whole width. */
    property int tileColumns: 2
    /** Space between the panel's edge and what is in it. */
    readonly property int inset: 16
    /** How tall the panel wants to be. With Play Together on it takes all the height it is given. */
    readonly property int wantedHeight: Math.ceil(column.implicitHeight) + 2 * inset

    // The invites of the running session: one per way a friend can reach this computer
    readonly property string inviteLan: hosting ? (session.coplayInviteLan || session.coplayInvite || "") : ""
    readonly property string invitePublic: hosting ? (session.coplayInvitePublic || "") : ""
    readonly property string inviteIPv6: hosting ? (session.coplayInviteIPv6 || "") : ""
    /** The link for a friend without the app, who joins from a web browser. */
    readonly property string inviteWeb: hosting ? (session.coplayInviteWeb || "") : ""

    /** The line under the room's name: the game, who hosts, how many are here. */
    readonly property string roomLine: {
        if (!active)
            return "";
        let parts = [];
        const game = hosting ? (session.coplayRoomGame || "") : "";
        if (game.length > 0)
            parts.push(game);
        if (guest && hostPlayer && hostPlayer.name)
            parts.push(qsTr("Hosted by %1").arg(hostPlayer.name));
        parts.push(players.length === 1 ? qsTr("1 player") : qsTr("%1 players").arg(players.length));
        return parts.join(" · ");
    }

    // One colour per place in the session, the same as in the big chat
    readonly property var slotColors: ["#b18cff", "#00f2ff", "#34d399", "#f472b6", "#fbbf24", "#60a5fa", "#fb923c", "#a3e635", "#e879f9"]

    color: Qt.rgba(16 / 255, 13 / 255, 34 / 255, 0.96)
    radius: 22
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.10)

    // Sharing has started: now the public address is worth asking for
    onHostingChanged: {
        if (hosting && Chiaki.settings.coplayLookupPublicAddress && (Chiaki.settings.coplayPublicLookupState || 0) === 0)
            Chiaki.settings.coplayLookupPublicAddress();
    }

    // Copies any text: a text item is the one thing in QML that reaches the clipboard
    TextEdit {
        id: clipboard
        visible: false
    }

    function copyText(text) {
        clipboard.text = text;
        clipboard.selectAll();
        clipboard.copy();
        clipboard.text = "";
        inviteCopied.restart();
    }

    function copyInvite() {
        // One invite: copy it. Several: ask which friend it is for.
        if (invitePublic.length === 0 && inviteIPv6.length === 0 && inviteWeb.length === 0) {
            session.coplayCopyInvite();
            inviteCopied.restart();
        } else {
            openMenu(inviteMenu, inviteButton, "below");
        }
    }

    function send() {
        if (!session || chatInput.text.trim().length === 0)
            return;
        session.coplaySendChat(chatInput.text);
        chatInput.text = "";
    }

    function openLink(link) {
        if (/^https?:\/\//i.test(link))
            Qt.openUrlExternally(link);
    }

    function escaped(text) {
        return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/"/g, "&quot;");
    }

    function nameOf(player) {
        return player ? (player.name || qsTr("Player %1").arg(player.slot + 1)) : "";
    }

    /** How tall a menu is, from its entries: right while it opens, before it has been laid out. */
    function menuHeight(m) {
        let h = m.topPadding + m.bottomPadding;
        for (let i = 0; i < m.count; ++i) {
            const item = m.itemAt(i);
            if (item && item.visible)
                h += item.height > 0 ? item.height : item.implicitHeight;
        }
        return h;
    }

    /**
     * Opens a menu next to `item` where there is room in the window: "below" it (lined up
     * with its right edge), "above" it, or "beside" it (to the right if that fits, else to
     * the left, else below). The menu itself keeps the last bit inside the window. It hangs
     * on `host` (the panel if not given), which stays while the menu is open. The menu has
     * to call its `placement` in onAboutToShow: only then do its entries know their size.
     */
    function openMenu(m, item, where, host) {
        const owner = host || panel;
        m.placement = () => placeMenu(m, item, where, owner);
        m.popup(owner, 0, 0);
    }

    function placeMenu(m, item, where, owner) {
        m.fit();
        const w = m.width;
        const h = menuHeight(m);
        const win = panel.Window.window;
        const winW = win ? win.width : 1e6;
        const winH = win ? win.height : 1e6;
        const p = item.mapToItem(null, 0, 0);
        let x = 0;
        let y = item.height + 6;
        if (where === "beside") {
            y = 0;
            if (p.x + item.width + 8 + w <= winW - 8) {
                x = item.width + 8;
            } else if (p.x - 8 - w >= 8) {
                x = -w - 8;
            } else {
                x = Math.round((item.width - w) / 2);
                y = item.height + 6;
            }
        } else {
            x = Math.round(item.width - w);
            if (where === "above" || (p.y + item.height + 6 + h > winH - 8 && p.y - 6 - h >= 8))
                y = -h - 6;
        }
        const q = item.mapToItem(owner, x, y);
        m.x = q.x;
        m.y = q.y;
    }

    function openPlayerMenu(tile) {
        playerMenu.slot = tile.player.slot;
        openMenu(playerMenu, tile, "beside");
    }

    function closeMenus() {
        inviteMenu.close();
        playerMenu.close();
    }

    /** Scrolls the panel so that an item that just got the focus can be seen. */
    function ensureVisible(item) {
        let inside = false;
        for (let p = item; p; p = p.parent) {
            if (p === column) {
                inside = true;
                break;
            }
        }
        if (!inside || flick.contentHeight <= flick.height)
            return;
        const r = item.mapToItem(flick.contentItem, 0, 0);
        const top = r.y - 12;
        const bottom = r.y + item.height + 12;
        if (top < flick.contentY)
            flick.contentY = Math.max(0, top);
        else if (bottom > flick.contentY + flick.height)
            flick.contentY = Math.min(flick.contentHeight - flick.height, bottom - flick.height);
    }

    // ------------------------------------------------------------ building blocks

    /** The small capital heading over a part of the panel. */
    component PanelLabel: C.SectionLabel {
        font.pixelSize: 11
        font.letterSpacing: 1.3
    }

    /** A line of a menu: an icon, the text, a tick for the one that is chosen. */
    component MenuEntry: MenuItem {
        id: entry
        property string iconName
        /** Removes somebody: red. */
        property bool danger: false
        /** Shows a tick: the one of a group that is on now. */
        property bool chosen: false

        height: visible ? implicitHeight : 0
        implicitHeight: 38
        leftPadding: 10
        rightPadding: 16
        topPadding: 0
        bottomPadding: 0
        font.pixelSize: 14
        font.weight: danger || chosen ? Font.Bold : Font.DemiBold
        indicator: Item {}
        arrow: Item {}

        contentItem: Item {
            // Measured at once, not by a Row: the menu takes its width before it opens
            readonly property int iconSpace: entry.iconName.length > 0 ? 27 : 0
            implicitWidth: iconSpace + entryText.implicitWidth + (entry.chosen ? 25 : 0)
            implicitHeight: 20

            C.Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: entry.iconName.length > 0
                iconName: entry.iconName
                size: 17
                color: entry.danger ? "#ff8fa8" : entry.chosen ? "#cdb6ff" : C.Theme.textSoft
            }

            Text {
                id: entryText
                anchors.verticalCenter: parent.verticalCenter
                x: parent.iconSpace
                text: entry.text
                font: entry.font
                color: entry.danger ? "#ffb3c4" : C.Theme.text
            }

            C.Icon {
                anchors.verticalCenter: parent.verticalCenter
                x: entryText.x + entryText.implicitWidth + 10
                visible: entry.chosen
                iconName: "check"
                size: 15
                color: "#cdb6ff"
            }
        }

        background: Rectangle {
            implicitHeight: 38
            radius: 10
            color: entry.highlighted ? Qt.rgba(140 / 255, 82 / 255, 1, 0.20)
                 : entry.chosen ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
        }
    }

    /** The ground of every menu of the stream menu. */
    component MenuGround: Rectangle {
        implicitWidth: 220
        radius: 16
        color: C.Theme.popup
        border.width: 1
        border.color: C.Theme.lineStrong
    }

    component MenuLine: MenuSeparator {
        height: visible ? implicitHeight : 0
        topPadding: 4
        bottomPadding: 4
        leftPadding: 6
        rightPadding: 6
        contentItem: Rectangle {
            implicitHeight: 1
            color: C.Theme.line
        }
    }

    /** One of one's own switches: an icon over a word. */
    component OwnSwitch: AbstractButton {
        id: sw
        property string iconName
        /** "bad" (switched off: muted), "good" (running: the webcam), "dim" (leaving), or neutral. */
        property string tone: ""

        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitHeight: 50
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        padding: 2
        font.pixelSize: 11
        font.weight: Font.Bold
        Accessible.name: text

        readonly property color foreground: tone === "bad" ? "#ffc2d1" : tone === "good" ? "#c9f7e3" : tone === "dim" ? "#d9d5f2" : C.Theme.text

        Keys.onPressed: (event) => C.FocusChain.handle(sw, event)

        background: Item {
            Rectangle {
                anchors.fill: parent
                radius: 12
                color: sw.tone === "bad" ? Qt.rgba(240 / 255, 70 / 255, 110 / 255, 0.16)
                     : sw.tone === "good" ? Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.16)
                     : Qt.rgba(1, 1, 1, sw.tone === "dim" ? 0.04 : 0.07)
                border.width: 1
                border.color: sw.tone === "bad" ? Qt.rgba(240 / 255, 70 / 255, 110 / 255, 0.5)
                            : sw.tone === "good" ? Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.5)
                            : Qt.rgba(1, 1, 1, sw.tone === "dim" ? 0.10 : 0.12)
            }

            Rectangle {
                anchors.fill: parent
                radius: 12
                color: "#ffffff"
                opacity: sw.down ? 0.12 : sw.hovered ? 0.06 : 0
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: 15
                color: "transparent"
                border.width: 2
                border.color: C.Theme.focus
                visible: sw.visualFocus
            }
        }

        contentItem: Item {
            Column {
                anchors.centerIn: parent
                width: parent.width
                spacing: 4

                C.Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    iconName: sw.iconName
                    size: 18
                    color: sw.foreground
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: sw.text
                    font: sw.font
                    color: sw.foreground
                }
            }
        }
    }

    /** A tag on a player's tile: LIVE, HOST, WEB, WATCHING, MUTED. */
    component Tag: Rectangle {
        property alias text: tagText.text
        implicitWidth: tagText.implicitWidth + 14
        implicitHeight: 17
        radius: 8.5

        Text {
            id: tagText
            anchors.centerIn: parent
            font.pixelSize: 9
            font.weight: Font.ExtraBold
            font.letterSpacing: 0.6
            color: "#ffffff"
        }
    }

    /** Three bars that move while somebody talks. */
    component TalkBars: Item {
        id: bars
        property bool running: false
        property real phase: 0
        implicitWidth: 13
        implicitHeight: 11

        NumberAnimation on phase {
            from: 0
            to: 1
            duration: 700
            loops: Animation.Infinite
            running: bars.running && bars.visible
        }

        Repeater {
            model: 3

            Rectangle {
                required property int index
                x: index * 5
                width: 3
                radius: 1.5
                height: Math.round(bars.height * (0.45 + 0.55 * (0.5 + 0.5 * Math.sin(2 * Math.PI * (bars.phase - index * 0.29)))))
                y: bars.height - height
                color: C.Theme.good
            }
        }
    }

    /** A player: their webcam, picture or initial, their name and what they do. */
    component PlayerTile: AbstractButton {
        id: tile
        required property int index
        readonly property var player: panel.players[index] || ({})
        readonly property bool you: !!player.you
        readonly property bool isTalking: !!panel.talking[String(player.slot)]
        readonly property bool isMuted: !!player.muted || !!player.silenced
        readonly property bool menuOpen: playerMenu.visible && playerMenu.slot === player.slot

        implicitHeight: 84
        padding: 0
        hoverEnabled: !you
        // One's own tile has nothing to choose
        focusPolicy: you ? Qt.NoFocus : Qt.StrongFocus
        Accessible.name: panel.nameOf(player)
        Accessible.description: you ? "" : qsTr("Click for more")

        Keys.onPressed: (event) => C.FocusChain.handle(tile, event)
        onClicked: if (!you) panel.openPlayerMenu(tile)

        HoverHandler {
            cursorShape: tile.you ? Qt.ArrowCursor : Qt.PointingHandCursor
        }

        background: Item {
            // Talking: a soft green glow around the ring
            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: 14
                color: "transparent"
                border.width: 4
                border.color: Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.22)
                visible: tile.isTalking
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -5
                radius: 15
                color: "transparent"
                border.width: 2
                border.color: C.Theme.focus
                visible: tile.visualFocus
            }
        }

        // Rounded without a shader: the webcam sits a little inside the rounded frame, as on the
        // game picture (StreamHud), so that its square corners never show
        contentItem: Item {
            Item {
                anchors.fill: parent

                Rectangle {
                    anchors.fill: parent
                    radius: 10
                    color: tile.hovered ? "#1a1630" : "#141126"
                }

                // The player's picture or initial; the webcam covers it while it runs
                C.Avatar {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 11
                    width: 40
                    height: 40
                    source: tile.player.avatarUrl || ""
                    name: tile.player.name || "?"
                }

                Image {
                    anchors.fill: parent
                    anchors.margins: 3
                    visible: !!tile.player.camera
                    cache: false
                    fillMode: Image.PreserveAspectCrop
                    source: tile.player.camera ? "image://coplaycam/" + tile.player.slot + "/" + panel.camSerial : ""
                }

                // Live right now, host, from a browser, only watching
                Row {
                    x: 6
                    y: 6
                    spacing: 4

                    Tag {
                        // Each their own controller: the player the game sees
                        visible: (tile.player.player || 0) > 0
                        color: Qt.rgba(14 / 255, 165 / 255, 233 / 255, 0.92)
                        text: qsTr("P%1").arg(tile.player.player)
                    }

                    Tag {
                        // Plays with mouse and keyboard in the game on the host's computer
                        visible: !!tile.player.keyMouse
                        color: Qt.rgba(99 / 255, 102 / 255, 241 / 255, 0.92)
                        text: qsTr("M+K")
                    }

                    Tag {
                        // Streams the session live (Twitch, say): everybody sees it
                        visible: !!tile.player.live
                        color: "#e91916"
                        text: qsTr("LIVE")
                    }

                    Tag {
                        // We are live and this player asked to be left out of streams: not in ours
                        visible: Chiaki.live.live && !!tile.player.noStreams && !tile.player.you
                        color: Qt.rgba(0, 0, 0, 0.65)
                        text: qsTr("NOT IN STREAM")
                    }

                    Tag {
                        visible: !!tile.player.host
                        color: Qt.rgba(122 / 255, 60 / 255, 245 / 255, 0.92)
                        text: qsTr("HOST")
                    }

                    Tag {
                        // Joined from a web browser, without the app
                        visible: !!tile.player.web
                        color: Qt.rgba(16 / 255, 140 / 255, 98 / 255, 0.92)
                        text: qsTr("WEB")
                    }

                    Tag {
                        visible: !panel.playsNow(tile.player)
                        color: Qt.rgba(0, 0, 0, 0.65)
                        text: qsTr("WATCHING")
                    }
                }

                // Name and state along the bottom, rounded with the frame
                Item {
                    anchors {
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                        margins: 1
                    }
                    height: 24
                    clip: true

                    Rectangle {
                        anchors {
                            left: parent.left
                            right: parent.right
                            bottom: parent.bottom
                        }
                        height: parent.height + 12
                        radius: 9
                        color: Qt.rgba(0, 0, 0, 0.62)
                    }

                    RowLayout {
                        anchors {
                            fill: parent
                            leftMargin: 8
                            rightMargin: 7
                        }
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            font.pixelSize: 12
                            font.weight: Font.ExtraBold
                            color: C.Theme.text
                            text: panel.nameOf(tile.player) + (tile.you ? qsTr(" (you)") : "")
                        }

                        // Plays along, or only watches (the host always plays); faint while
                        // no controller is connected (the keyboard counts as one)
                        C.Icon {
                            readonly property bool noPad: panel.playsNow(tile.player) && tile.player.controller === 0
                            visible: !tile.player.host
                            iconName: panel.playsNow(tile.player) ? "gamepad" : "eye"
                            size: 14
                            color: panel.playsNow(tile.player) ? "#cdb6ff" : C.Theme.textSoft
                            opacity: noPad ? 0.35 : 1
                            Accessible.name: noPad ? qsTr("No controller connected") : ""
                        }

                        Rectangle {
                            visible: tile.isMuted || !!tile.player.mutedHere
                            implicitWidth: mutedText.implicitWidth + 12
                            implicitHeight: 15
                            radius: 7.5
                            color: "#d93a61"

                            Text {
                                id: mutedText
                                anchors.centerIn: parent
                                font.pixelSize: 9
                                font.weight: Font.ExtraBold
                                font.letterSpacing: 0.5
                                color: "#ffffff"
                                text: tile.player.mutedHere && !tile.isMuted ? qsTr("MUTED HERE") : qsTr("MUTED")
                            }
                        }

                        TalkBars {
                            visible: !!tile.player.voice && !tile.isMuted && !tile.player.mutedHere && tile.isTalking
                            running: visible
                        }

                        // In voice chat, quiet
                        Rectangle {
                            visible: !!tile.player.voice && !tile.isMuted && !tile.player.mutedHere && !tile.isTalking
                            implicitWidth: 8
                            implicitHeight: 8
                            radius: 4
                            color: Qt.rgba(1, 1, 1, 0.35)
                        }
                    }
                }
            }

            // Talking, the open menu, who has the controller
            Rectangle {
                anchors.fill: parent
                radius: 10
                color: "transparent"
                border.width: tile.isTalking || tile.menuOpen || tile.player.inControl ? 2 : 1
                border.color: tile.isTalking ? C.Theme.good
                            : tile.menuOpen ? C.Theme.headingSoft
                            : tile.player.inControl ? C.Theme.brandFrom
                            : Qt.rgba(1, 1, 1, 0.12)
            }
        }
    }

    // ------------------------------------------------------------ the panel

    Flickable {
        id: flick
        anchors {
            fill: parent
            topMargin: 8
            bottomMargin: 8
        }
        clip: true
        contentWidth: width
        contentHeight: column.height + 2 * (panel.inset - 8)
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        // A thin bar in the panel's margin, there whenever not everything fits
        ScrollBar.vertical: ScrollBar {
            id: panelScroll
            policy: flick.contentHeight > flick.height + 1 ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            padding: 3
            background: Item {}
            contentItem: Rectangle {
                implicitWidth: 4
                radius: 2
                color: Qt.rgba(1, 1, 1, panelScroll.pressed ? 0.4 : panelScroll.hovered ? 0.3 : 0.18)
            }
        }

        ColumnLayout {
            id: column
            x: panel.inset
            y: panel.inset - 8
            width: flick.width - 2 * panel.inset
            // The chat takes what is left
            height: Math.max(implicitHeight, flick.height - 2 * (panel.inset - 8))
            spacing: 10

            // ---------------------------------------- the room
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 2

                        Row {
                            visible: panel.publicRoom
                            spacing: 6

                            C.Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                iconName: "globe"
                                size: 12
                                color: C.Theme.headingSoft
                            }

                            PanelLabel {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Public room")
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            font.pixelSize: 17
                            font.weight: Font.ExtraBold
                            color: C.Theme.text
                            text: panel.publicRoom && (panel.session.coplayRoomTitle || "").length > 0
                                  ? panel.session.coplayRoomTitle : qsTr("Play Together")
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            wrapMode: Text.WordWrap
                            textFormat: Text.PlainText
                            font.pixelSize: 12
                            color: C.Theme.textSoft
                            text: panel.roomLine
                        }
                    }

                    C.PillButton {
                        id: inviteButton
                        Layout.alignment: Qt.AlignTop
                        visible: panel.hosting
                        small: true
                        iconName: inviteCopied.running ? "check" : "copy"
                        text: inviteCopied.running ? qsTr("Copied") : qsTr("Invite")
                        Accessible.name: qsTr("Copy invite")
                        onClicked: panel.copyInvite()

                        Timer {
                            id: inviteCopied
                            interval: 1500
                        }
                    }
                }

                // Nobody else can come into this session
                Text {
                    Layout.fillWidth: true
                    visible: !!panel.session && !panel.active
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                    color: C.Theme.textSoft
                    text: qsTr("Nobody can join this session: you chose Play. To play, talk and use the webcam with friends, choose Host on the start page instead.")
                }

                // The console sleeps and the room stays: who may, wakes it from here
                RowLayout {
                    Layout.fillWidth: true
                    visible: panel.active && !!panel.session && panel.session.coplayConsoleAsleep
                    spacing: 10

                    C.Icon {
                        iconName: "moon"
                        size: 22
                        color: "#c4b5fd"
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 13
                        color: C.Theme.textSoft
                        text: !!panel.session && panel.session.coplayWaking ? qsTr("Waking the console…")
                              : (!!panel.session && panel.session.coplayMayWake ? qsTr("The console is asleep. Wake it to play on.")
                                                                                 : qsTr("The console is asleep. The host can wake it."))
                    }

                    C.PillButton {
                        kind: "primary"
                        visible: !!panel.session && panel.session.coplayMayWake && !panel.session.coplayWaking
                        text: qsTr("Wake the console")
                        onClicked: panel.session.coplayWake()
                    }
                }

                // A guest the host lets play with mouse and keyboard: this window takes them for the game
                C.ToggleRow {
                    visible: panel.guest && panel.active && !!panel.session && panel.session.coplayKeyMouseAllowed
                    implicitHeight: 48
                    title: qsTr("Mouse and keyboard")
                    subtitle: checked ? qsTr("They go to the game. %1 opens this menu and lets go.").arg(Chiaki.settings.stringForStreamMenuShortcut())
                                      : qsTr("The host lets you play with them too")
                    checked: !!panel.session && panel.session.coplayKeyMouse
                    onToggled: panel.session.coplayKeyMouse = checked
                }

                // A guest decides here, any time, whether picture and sound come to this computer
                C.ToggleRow {
                    visible: panel.guest && panel.active
                    implicitHeight: 48
                    title: qsTr("Picture and sound")
                    subtitle: checked ? qsTr("The game on this computer too, as the host sends it")
                                      : qsTr("Controller only: your controller plays along, rumble too")
                    checked: !!panel.session && panel.session.coplayVideoEnabled
                    onToggled: panel.session.coplayVideoEnabled = checked
                }

                Text {
                    readonly property string problem: panel.hosting ? (panel.session.coplayRoomProblem || "") : ""
                    Layout.fillWidth: true
                    visible: problem.length > 0
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: C.Theme.wait
                    text: problem
                }

                // While sharing: whether friends elsewhere get in without a forwarded port
                Text {
                    readonly property string reach: panel.hosting ? (panel.session.coplayReachText || "") : ""
                    Layout.fillWidth: true
                    visible: reach.length > 0
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: panel.hosting && panel.session.coplayReachGood ? C.Theme.textDim : C.Theme.wait
                    text: reach
                }

                C.ToggleRow {
                    id: lockRow
                    visible: panel.hosting
                    implicitHeight: 48
                    title: qsTr("Open to newcomers")
                    subtitle: checked ? qsTr("Switch off to keep the room as it is") : qsTr("Closed: nobody new gets in")
                    checked: panel.hosting && !panel.session.coplayLocked
                    onToggled: panel.session.coplayLocked = !checked
                }

                // A public room open to newcomers: how they get in (the words of the room list)
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.publicRoom && !panel.session.coplayLocked
                    spacing: 6

                    PanelLabel {
                        text: qsTr("Who gets in")
                    }

                    C.Segmented {
                        Layout.fillWidth: true
                        implicitHeight: 36
                        model: [qsTr("Ask to join"), qsTr("Join as viewers"), qsTr("Join and play")]
                        currentIndex: panel.publicRoom ? panel.session.coplayJoinPolicy : 0
                        onActivated: (index) => panel.session.coplayJoinPolicy = index
                        Accessible.name: qsTr("Who gets in")
                    }
                }
            }

            // ---------------------------------------- who knocks
            Repeater {
                model: panel.pending.length

                Rectangle {
                    id: knock
                    required property int index
                    readonly property var knocker: panel.pending[index] || ({})

                    Layout.fillWidth: true
                    implicitHeight: knockRow.implicitHeight + 18
                    radius: 14
                    color: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.11)
                    border.width: 1
                    border.color: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.45)

                    RowLayout {
                        id: knockRow
                        anchors {
                            left: parent.left
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                            leftMargin: 10
                            rightMargin: 9
                        }
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            radius: 16
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#fb923c" }
                                GradientStop { position: 1.0; color: "#ea580c" }
                            }

                            Text {
                                anchors.centerIn: parent
                                font.pixelSize: 13
                                font.weight: Font.ExtraBold
                                color: "#2a0f02"
                                text: (knock.knocker.name || "?").trim().charAt(0).toUpperCase()
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                                font.pixelSize: 13
                                font.weight: Font.ExtraBold
                                color: "#fde4a8"
                                text: qsTr("%1 wants to join").arg(knock.knocker.name || "?")
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: !!knock.knocker.web
                                elide: Text.ElideRight
                                font.pixelSize: 11
                                color: "#d8c9a3"
                                text: qsTr("From a web browser")
                            }
                        }

                        C.PillButton {
                            kind: "success"
                            small: true
                            text: qsTr("Let in")
                            onClicked: panel.session.coplayAdmit(knock.knocker.id, true)
                        }

                        C.PillButton {
                            small: true
                            text: qsTr("No")
                            onClicked: panel.session.coplayAdmit(knock.knocker.id, false)
                        }
                    }
                }
            }

            // ---------------------------------------- who controls
            ColumnLayout {
                Layout.fillWidth: true
                visible: panel.hosting
                spacing: 6

                PanelLabel {
                    text: qsTr("Who controls")
                }

                C.Segmented {
                    Layout.fillWidth: true
                    implicitHeight: 38
                    // Each their own controller only for a game on this computer: a console takes one
                    model: panel.hosting && panel.session.pcSource
                           ? [qsTr("Together"), qsTr("One at a time"), qsTr("Only me"), qsTr("Each their own")]
                           : [qsTr("Together"), qsTr("One at a time"), qsTr("Only me")]
                    Accessible.name: qsTr("Who controls")
                    currentIndex: panel.hosting ? panel.session.coplayMixMode : 0
                    onActivated: (index) => panel.session.coplayMixMode = index
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: C.Theme.textDim
                    text: [qsTr("Everybody's buttons count at the same time."), qsTr("Whoever moves takes over; the others rest."), qsTr("Your friends watch."),
                           qsTr("Every player is a player of their own: you 1, your friends 2, 3 and 4.")][panel.hosting ? panel.session.coplayMixMode : 0] || ""
                }
            }

            // ---------------------------------------- everybody
            ColumnLayout {
                Layout.fillWidth: true
                visible: panel.active && panel.players.length > 0
                spacing: 6

                PanelLabel {
                    text: panel.players.length > 1 ? qsTr("Players · click one for more") : qsTr("Players")
                }

                Grid {
                    id: tiles
                    Layout.fillWidth: true
                    columns: Math.max(1, panel.tileColumns)
                    spacing: 8
                    readonly property int tileWidth: Math.floor((width - (columns - 1) * spacing) / columns)

                    // By place, not by player: the tiles stay (and keep the focus) while the
                    // players' states change, and only come and go with the players
                    Repeater {
                        model: panel.players.length

                        PlayerTile {
                            width: tiles.tileWidth
                        }
                    }
                }
            }

            // ---------------------------------------- one's own microphone, voices and webcam
            RowLayout {
                Layout.fillWidth: true
                visible: panel.active
                spacing: 6

                OwnSwitch {
                    id: voiceButton
                    visible: !panel.voiceOn
                    iconName: "headphones"
                    text: qsTr("Join voice chat")
                    onClicked: panel.session.coplayVoiceOn = true
                }

                OwnSwitch {
                    id: micButton
                    readonly property bool muted: panel.active && panel.session.coplayMicMuted
                    visible: panel.voiceOn
                    iconName: muted ? "mic-off" : "mic"
                    tone: muted ? "bad" : ""
                    text: muted ? qsTr("Mic muted") : qsTr("Mic on")
                    onClicked: panel.session.coplayMicMuted = !panel.session.coplayMicMuted
                }

                OwnSwitch {
                    id: soundButton
                    readonly property bool deafened: panel.active && panel.session.coplayDeafened
                    visible: panel.voiceOn
                    iconName: deafened ? "speaker-off" : "headphones"
                    tone: deafened ? "bad" : ""
                    text: deafened ? qsTr("Voices off") : qsTr("Voices on")
                    onClicked: panel.session.coplayDeafened = !panel.session.coplayDeafened
                }

                OwnSwitch {
                    id: camButton
                    readonly property bool running: panel.active && panel.session.coplayCameraOn
                    iconName: "camera"
                    tone: running ? "good" : ""
                    text: running ? qsTr("Webcam on") : qsTr("Webcam off")
                    onClicked: panel.session.coplayCameraOn = !panel.session.coplayCameraOn
                }

                OwnSwitch {
                    id: leaveVoiceButton
                    visible: panel.voiceOn
                    iconName: "leave"
                    tone: "dim"
                    text: qsTr("Leave voice")
                    Accessible.name: qsTr("Leave voice chat")
                    onClicked: panel.session.coplayVoiceOn = false
                }
            }

            // ---------------------------------------- the chat
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: panel.active
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    Layout.bottomMargin: 2
                    color: C.Theme.line
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    PanelLabel {
                        text: qsTr("Chat")
                    }

                    Rectangle {
                        visible: panel.unread > 0
                        implicitWidth: Math.max(18, unreadText.implicitWidth + 12)
                        implicitHeight: 18
                        radius: 9
                        color: "#d92f7f"

                        Text {
                            id: unreadText
                            anchors.centerIn: parent
                            font.pixelSize: 10
                            font.weight: Font.ExtraBold
                            color: "#ffffff"
                            text: panel.unread > 99 ? "99+" : panel.unread
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Text {
                        font.pixelSize: 11
                        color: C.Theme.textDim
                        // Ctrl+T, ⌘T on a Mac: the stream window's shortcut for the big chat
                        text: Qt.platform.os === "osx" ? "⌘T" : "Ctrl+T"
                    }

                    C.PillButton {
                        id: chatOpenButton
                        small: true
                        implicitHeight: 30
                        iconName: "chat"
                        text: qsTr("Open big")
                        Accessible.name: qsTr("Open the chat")
                        onClicked: panel.chatRequested()
                    }
                }

                // The last messages, newest at the bottom; older ones go out at the top
                Item {
                    id: chatView
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 56
                    Layout.preferredHeight: 76
                    clip: true

                    Text {
                        anchors {
                            left: parent.left
                            right: parent.right
                            bottom: parent.bottom
                        }
                        visible: panel.messages.length === 0
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        color: C.Theme.textDim
                        text: qsTr("No messages yet. Say hi!")
                    }

                    Column {
                        anchors {
                            left: parent.left
                            right: parent.right
                            bottom: parent.bottom
                        }
                        spacing: 4

                        Repeater {
                            model: panel.messages.slice(-30)

                            Text {
                                required property var modelData
                                width: parent ? parent.width : 0
                                wrapMode: Text.Wrap
                                textFormat: Text.StyledText
                                font.pixelSize: 13
                                color: "#e7e4fa"
                                linkColor: C.Theme.focus
                                opacity: modelData.pending ? 0.55 : 1
                                text: "<b><font color=\"" + panel.slotColors[modelData.slot % panel.slotColors.length] + "\">"
                                      + panel.escaped(modelData.name || qsTr("Player %1").arg(modelData.slot + 1))
                                      + "</font></b>&nbsp; " + (modelData.html || "")
                                      + (modelData.failed ? " <font color=\"#f0466e\">· " + qsTr("not sent") + "</font>" : "")
                                onLinkActivated: (link) => panel.openLink(link)

                                HoverHandler {
                                    cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    radius: 12
                    color: C.Theme.field
                    border.width: chatInput.activeFocus ? 2 : 1
                    border.color: chatInput.activeFocus ? C.Theme.focus : Qt.rgba(1, 1, 1, 0.10)

                    RowLayout {
                        anchors {
                            fill: parent
                            leftMargin: 12
                            rightMargin: 4
                        }
                        spacing: 6

                        // Return starts writing (and sends), as everywhere with the controller
                        C.TextField {
                            id: chatInput
                            Layout.fillWidth: true
                            implicitHeight: 36
                            leftPadding: 0
                            rightPadding: 0
                            topPadding: 0
                            bottomPadding: 0
                            verticalAlignment: TextInput.AlignVCenter
                            font.pixelSize: 13
                            color: C.Theme.text
                            maximumLength: 400
                            selectByMouse: true
                            Accessible.name: qsTr("Message to everybody")
                            onAccepted: panel.send()

                            background: Item {
                                Text {
                                    anchors {
                                        left: parent.left
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                    }
                                    visible: chatInput.length === 0 && chatInput.preeditText.length === 0
                                    elide: Text.ElideRight
                                    font.pixelSize: 13
                                    color: C.Theme.textDim
                                    text: qsTr("Message everybody…")
                                }
                            }
                        }

                        AbstractButton {
                            id: sendButton
                            implicitWidth: 32
                            implicitHeight: 32
                            enabled: chatInput.text.trim().length > 0
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus
                            Accessible.name: qsTr("Send")
                            Keys.onPressed: (event) => C.FocusChain.handle(sendButton, event)
                            onClicked: panel.send()

                            background: Rectangle {
                                radius: 9
                                color: sendButton.enabled ? Qt.lighter(C.Theme.button, sendButton.down ? 0.9 : sendButton.hovered ? 1.12 : 1)
                                                          : Qt.rgba(1, 1, 1, 0.08)
                                border.width: sendButton.visualFocus ? 2 : 0
                                border.color: C.Theme.focus
                            }

                            contentItem: Item {
                                C.Icon {
                                    anchors.centerIn: parent
                                    iconName: "send"
                                    size: 15
                                    color: sendButton.enabled ? "#ffffff" : C.Theme.textDim
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ menus

    C.Menu {
        id: inviteMenu
        property var placement: null
        onAboutToShow: if (placement) placement()
        padding: 6
        font.pixelSize: 14
        background: MenuGround {}

        MenuEntry {
            visible: panel.inviteLan.length > 0
            enabled: visible
            iconName: "home"
            text: qsTr("For a friend in the same network")
            onTriggered: panel.copyText(panel.inviteLan)
        }

        MenuEntry {
            visible: panel.invitePublic.length > 0
            enabled: visible
            iconName: "globe"
            text: qsTr("For a friend over the internet")
            onTriggered: panel.copyText(panel.invitePublic)
        }

        MenuEntry {
            visible: panel.inviteIPv6.length > 0
            enabled: visible
            iconName: "globe"
            text: qsTr("For a friend over the internet, IPv6")
            onTriggered: panel.copyText(panel.inviteIPv6)
        }

        MenuEntry {
            visible: panel.inviteWeb.length > 0
            enabled: visible
            iconName: "browser"
            text: qsTr("For a friend without the app: a link for the web browser")
            onTriggered: panel.copyText(panel.inviteWeb)
        }
    }

    // What can be done with one player. One menu for all tiles, so that it stays open
    // while the players' states change.
    C.Menu {
        id: playerMenu
        property var placement: null
        onAboutToShow: if (placement) placement()
        property int slot: -1
        readonly property var player: panel.players.find((p) => p.slot === slot) || null
        readonly property string name: panel.nameOf(player)

        padding: 6
        font.pixelSize: 14
        background: MenuGround {}
        // The player has left
        onPlayerChanged: if (visible && !player) close()

        // Who it is, and how they are here (measured at once, like the entries)
        Item {
            implicitWidth: 10 + 26 + 8 + Math.max(headName.implicitWidth, headState.implicitWidth) + 12
            implicitHeight: 50

            C.Avatar {
                x: 10
                y: 10
                width: 26
                height: 26
                source: playerMenu.player ? (playerMenu.player.avatarUrl || "") : ""
                name: playerMenu.name
            }

            Text {
                id: headName
                x: 44
                y: 6
                textFormat: Text.PlainText
                font.pixelSize: 13
                font.weight: Font.ExtraBold
                color: C.Theme.text
                text: playerMenu.name
            }

            Text {
                id: headState
                x: 44
                y: 24
                font.pixelSize: 11
                color: C.Theme.textDim
                text: {
                    const p = playerMenu.player;
                    if (!p)
                        return "";
                    if (panel.eachOwn && p.host)
                        return qsTr("Host · player 1");
                    if (p.host)
                        return qsTr("Host");
                    if (panel.eachOwn)
                        return p.player > 0 ? (p.canControl ? qsTr("Player %1").arg(p.player) : qsTr("Player %1 · watching").arg(p.player))
                                            : qsTr("Watching · no controller of their own free");
                    if (panel.hostOnly)
                        return panel.hosting ? qsTr("Watching · only you play") : qsTr("Watching · only the host plays");
                    const noPad = p.controller === 0 ? " · " + qsTr("no controller connected") : "";
                    if (p.web)
                        return p.canControl ? qsTr("From a web browser · plays along") + noPad : qsTr("From a web browser · watching");
                    return p.canControl ? qsTr("Plays along") + noPad : qsTr("Watching");
                }
            }

            Rectangle {
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                    leftMargin: 4
                    rightMargin: 4
                }
                height: 1
                color: C.Theme.line
            }
        }

        // Each their own controller: which player they are; whoever was it gets their old number
        MenuEntry {
            readonly property int number: 2
            visible: panel.hosting && panel.eachOwn && !!playerMenu.player && !playerMenu.player.host
            enabled: visible
            chosen: visible && playerMenu.player.player === number
            iconName: "gamepad"
            text: qsTr("Player %1").arg(number)
            onTriggered: panel.session.coplaySetPlayerNumber(playerMenu.slot, number)
        }

        MenuEntry {
            readonly property int number: 3
            visible: panel.hosting && panel.eachOwn && !!playerMenu.player && !playerMenu.player.host
            enabled: visible
            chosen: visible && playerMenu.player.player === number
            iconName: "gamepad"
            text: qsTr("Player %1").arg(number)
            onTriggered: panel.session.coplaySetPlayerNumber(playerMenu.slot, number)
        }

        MenuEntry {
            readonly property int number: 4
            visible: panel.hosting && panel.eachOwn && !!playerMenu.player && !playerMenu.player.host
            enabled: visible
            chosen: visible && playerMenu.player.player === number
            iconName: "gamepad"
            text: qsTr("Player %1").arg(number)
            onTriggered: panel.session.coplaySetPlayerNumber(playerMenu.slot, number)
        }

        MenuLine {
            visible: panel.hosting && panel.eachOwn && !!playerMenu.player && !playerMenu.player.host
        }

        // A game on this computer: mouse and keyboard, for the players the host allows
        MenuEntry {
            readonly property bool allowed: !!playerMenu.player && !!playerMenu.player.keyMouse
            visible: panel.hosting && !!panel.session && panel.session.pcSource && Chiaki.settings.coplayPcKeyMouse
                     && !!playerMenu.player && !playerMenu.player.host
            enabled: visible
            iconName: "keyboard"
            text: allowed ? qsTr("No mouse and keyboard") : qsTr("Allow mouse and keyboard")
            onTriggered: panel.session.coplaySetKeyMouse(playerMenu.slot, !allowed)
        }

        MenuEntry {
            readonly property bool plays: !!playerMenu.player && playerMenu.player.canControl
            visible: panel.hosting
            // While only the host controls, letting somebody play would change nothing yet
            enabled: visible && !panel.hostOnly
            iconName: plays ? "eye" : "gamepad"
            text: plays ? qsTr("Only let them watch") : qsTr("Let them play")
            onTriggered: panel.session.coplaySetCanControl(playerMenu.slot, !plays)
        }

        MenuEntry {
            readonly property bool mutedHere: !!playerMenu.player && playerMenu.player.mutedHere
            iconName: mutedHere ? "speaker" : "speaker-off"
            text: mutedHere ? qsTr("Hear %1 again").arg(playerMenu.name) : qsTr("Mute %1 for me").arg(playerMenu.name)
            onTriggered: panel.session.coplaySetPlayerMuted(playerMenu.slot, !mutedHere)
        }

        MenuEntry {
            readonly property bool silenced: !!playerMenu.player && playerMenu.player.silenced
            visible: panel.hosting
            enabled: visible
            iconName: silenced ? "mic" : "mic-off"
            text: silenced ? qsTr("Let everybody hear them again") : qsTr("Mute for everybody")
            onTriggered: panel.session.coplaySetSilenced(playerMenu.slot, !silenced)
        }

        MenuLine {
            visible: panel.hosting
        }

        MenuEntry {
            visible: panel.hosting
            enabled: visible
            danger: true
            iconName: "remove-person"
            text: qsTr("Remove from the session")
            onTriggered: panel.session.coplayKick(playerMenu.slot)
        }

        MenuEntry {
            visible: panel.hosting
            enabled: visible
            danger: true
            iconName: "block"
            text: qsTr("Remove and block")
            onTriggered: panel.session.coplayBlock(playerMenu.slot)
        }
    }
}
