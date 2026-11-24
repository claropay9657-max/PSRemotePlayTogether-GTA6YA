import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

// Friends: who is online and what they play, friend requests, a friend link to add
// somebody, and the chats with friends. A panel on the right of the window, over the
// page. Everything that is sealed, sent and kept is FriendsClient's (friends.h,
// Chiaki.friends); this only shows it and passes on what the player does.
//
// Three faces: friends switched off (what they are, and the switch), the lists (status,
// add a friend, requests, friends, chats), and one chat.
Popup {
    id: panel

    readonly property var fr: Chiaki.friends
    /** The friend whose chat is open; empty for the lists. */
    property string chat: ""
    /** The lists: 0 friends, 1 chats */
    property int tab: 0
    /** What the link in the field leads to: {fp, name, avatar}, or null. */
    property var looked: null
    property string lookError: ""
    property bool looking: false
    /** The messages of the open chat, read again whenever the friends change. */
    property var messages: []
    /** The message being answered: id, name, text; null if none. */
    property var replyTo: null
    /** Messages can be written in the open chat (a friend, or a group one is still in). */
    readonly property bool canWrite: !!chatFriend || (!!chatGroup && !chatGroup.left)
    /** Until when the open chat is muted (0: not). */
    readonly property double mutedUntil: fr && chat && messages ? fr.mutedUntil(chat) : 0
    /** Whom "@" offers in the open chat (a group's members; nobody with one friend). */
    readonly property var mentionable: fr && chatGroup && messages ? fr.mentionable(chat) : []

    function indexOfMessage(id) {
        for (let i = 0; i < messages.length; ++i) {
            if (messages[i].id === id)
                return i;
        }
        return -1;
    }

    /** Goes to a message and lights it up. */
    function goTo(id) {
        const at = indexOfMessage(id);
        if (at < 0)
            return;
        messageList.stickToEnd = false;
        messageList.positionViewAtIndex(at, ListView.Center);
        Qt.callLater(() => {
            const item = messageList.itemAtIndex(at);
            if (item)
                item.highlight();
        });
    }

    /** The message a reply answers, as its quote shows it. */
    function quoteOf(m) {
        const to = m.replyTo;
        if (!to)
            return null;
        return { name: to.name || "", text: to.text || "", deleted: !!to.deleted, color: "" };
    }

    function muteFor(minutes) {
        fr.mute(chat, minutes);
        readMessages();
    }
    readonly property var chatFriend: friendFor(chat)
    readonly property int incomingCount: {
        let n = 0;
        const list = fr ? fr.requests : [];
        for (let i = 0; i < list.length; ++i) {
            if (list[i].incoming)
                n++;
        }
        return n;
    }
    /** The clock for "open now" and "probably over", moved on every minute while the panel is open. */
    property double now: Date.now()
    /** The session this computer hosts, to invite a friend into. */
    readonly property var hosted: Chiaki.session && Chiaki.session.coplayHosting ? Chiaki.session : null

    /** Opens the panel at the lists, or at a friend's chat. */
    function show(fp) {
        chat = fp || "";
        if (chat)
            tab = 1;
        readMessages();
        open();
    }

    function friendFor(fp) {
        const list = fr ? fr.friends : [];
        for (let i = 0; i < list.length; ++i) {
            if (list[i].fp === fp)
                return list[i];
        }
        return null;
    }

    function readMessages() {
        messages = fr && fr.enabled && chat ? fr.messages(chat) : [];
        readMarks = fr && fr.enabled && chat ? placeReaders(messages, fr.readers(chat)) : ({});
        chatGroup = fr && fr.enabled && chat && fr.isGroup(chat) ? fr.group(chat) : null;
        if (opened && chat && fr)
            fr.markRead(chat);
    }

    /** A sheet over the panel: "" none, "newgroup", "members" (of the open group), "plan" (for the open chat). */
    property string sheet: ""
    /** The open chat's group: id, name, admin, add, left, members, banned; null for a friend's chat. */
    property var chatGroup: null

    function memberName(fp) {
        if (!chatGroup)
            return "";
        for (let i = 0; i < chatGroup.members.length; ++i) {
            if (chatGroup.members[i].fp === fp)
                return chatGroup.members[i].name;
        }
        return "";
    }

    function askReport() {
        const who = chatGroup ? chatGroup.name : (chatFriend ? chatFriend.name : qsTr("this chat"));
        root.showConfirmDialog(qsTr("Report %1?").arg(who),
            qsTr("The last ten messages others wrote here go, opened, to the people who run the friends server. Nobody else gets to read them, and the server could not read them before. You can block somebody as well."),
            () => {
                const ids = [];
                for (let i = messages.length - 1; i >= 0 && ids.length < 10; --i) {
                    if (!messages[i].mine && !messages[i].system)
                        ids.push(messages[i].id);
                }
                fr.report(chat, ids, "");
            });
    }

    function askLeave() {
        root.showConfirmDialog(qsTr("Leave %1?").arg(chatGroup.name),
            qsTr("You no longer get its messages. The chat so far stays on this computer."),
            () => fr.leaveGroup(chat));
    }

    /** One line for what a friend does right now. */
    function stateText(f) {
        if (!f || f.state === "offline")
            return qsTr("Offline");
        // What their app says ("Hosting Friday racing (2 / 4 players)") says the most
        if (f.activity)
            return f.activity;
        if (f.room && f.room.title)
            return qsTr("Hosting %1").arg(f.room.title);
        if (f.state === "away")
            return qsTr("Away");
        if (f.state === "busy")
            return qsTr("Busy");
        if (f.state === "web")
            return qsTr("Online in a web browser");
        return qsTr("Online");
    }

    function stateColor(state) {
        if (state === "online" || state === "web")
            return C.Theme.good;
        if (state === "away")
            return C.Theme.wait;
        if (state === "busy")
            return C.Theme.bad;
        return C.Theme.textDim;
    }

    function timeText(ms) {
        const d = new Date(ms);
        const today = new Date();
        if (d.toDateString() === today.toDateString())
            return Qt.formatTime(d, "hh:mm");
        return Qt.formatDateTime(d, "d MMM hh:mm");
    }

    /** The pictures of whoever read up to a message and no further, by the message's place. */
    property var readMarks: ({})

    /**
     * Instagram's way: each reader's picture under the newest message they have seen (or written).
     * Under their own message it says nothing new, so it is left out there.
     */
    function placeReaders(list, readers) {
        const marks = {};
        for (let r = 0; r < readers.length; ++r) {
            const reader = readers[r];
            for (let i = list.length - 1; i >= 0; --i) {
                const m = list[i];
                if (m.system)
                    continue;
                if (m.from === reader.fp || m.at <= reader.at) {
                    if (m.from !== reader.fp) {
                        if (!marks[i])
                            marks[i] = [];
                        marks[i].push({ name: reader.name, avatar: reader.avatar });
                    }
                    break;
                }
            }
        }
        return marks;
    }

    function reactionsOf(message) {
        const out = [];
        const all = message.reactions || {};
        const me = fr ? fr.fingerprint : "";
        for (const emoji in all) {
            const who = all[emoji] || [];
            if (who.length > 0)
                out.push({ emoji: emoji, count: who.length, mine: who.indexOf(me) >= 0 });
        }
        return out;
    }

    function avatarOf(fp) {
        const f = friendFor(fp);
        return f ? (f.avatar || "") : "";
    }

    /** All emoji, to react to a message with. */
    property string reactingTo: ""
    function pickReaction(id) {
        reactingTo = id;
        reactionPicker.open();
    }

    function copyText(text) {
        clipboard.text = text;
        clipboard.selectAll();
        clipboard.copy();
        clipboard.text = "";
        copied.restart();
    }

    function openLink(link) {
        if (/^https?:\/\//i.test(link))
            Qt.openUrlExternally(link);
    }

    function joinRoom(room) {
        if (!room || !room.invite)
            return;
        close();
        root.joinFriendRoom(room);
    }

    function showSafetyCode(f) {
        root.showConfirmDialog(qsTr("Safety code with %1").arg(f.name),
            qsTr("%1\n\nCompare these numbers with the ones %2 sees, in person or in a session. If they are the same, your chats go straight to %2 and nobody is in between.\n\nMark %2 as checked?").arg(f.safety).arg(f.name),
            () => fr.setChecked(f.fp, true));
    }

    function askUnfriend(f) {
        root.showConfirmDialog(qsTr("Remove %1 as a friend?").arg(f.name),
            qsTr("%1 no longer sees your status, and you no longer see theirs. Your chat stays on this computer. You can become friends again with a new request.").arg(f.name),
            () => {
                if (chat === f.fp)
                    chat = "";
                fr.unfriend(f.fp);
            });
    }

    function askBlock(fp, name) {
        root.showConfirmDialog(qsTr("Block %1?").arg(name || qsTr("this player")),
            qsTr("%1 can no longer send you messages or friend requests, and no longer sees your status. They are not told.").arg(name || qsTr("This player")),
            () => {
                if (chat === fp)
                    chat = "";
                fr.block(fp);
            });
    }

    function sendMessage(text) {
        if (!text || !chat)
            return;
        fr.send(chat, text, replyTo ? replyTo.id : "");
        composer.clear();
        replyTo = null;
        messageList.stickToEnd = true;
        Qt.callLater(messageList.positionViewAtEnd);
    }

    function inviteToRoom() {
        if (!hosted || !chat)
            return;
        // The session's own card: title, game and its picture, console, places, how one gets in
        fr.sendJoin(chat, hosted.coplayInviteCard);
        Qt.callLater(messageList.positionViewAtEnd);
    }

    /** Whether an invite in the chat leads to a session that is open now: "open", "old" or "" (not known). */
    function liveFor(message, nowMs) {
        const card = message.join;
        if (!card)
            return "";
        const open = message.mine ? (!!hosted && hosted.coplayInvite === card.invite)
                                  : (!!chatFriend && !!chatFriend.room && chatFriend.room.invite === card.invite);
        if (open)
            return "open";
        // Sessions rarely last longer; their status may just not show it (hidden, offline for us)
        return nowMs - message.at > 6 * 3600 * 1000 ? "old" : "";
    }

    parent: Overlay.overlay
    modal: true
    dim: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: parent ? parent.width - width : 0
    y: 0
    width: parent ? Math.min(460, parent.width) : 460
    height: parent ? parent.height : 640

    onOpened: {
        if (fr && fr.enabled)
            fr.refresh();
        readMessages();
        if (chat)
            Qt.callLater(() => { messageList.positionViewAtEnd(); composer.focusInput(); });
    }
    onChatChanged: {
        sheet = "";
        replyTo = null;
        messageList.stickToEnd = true;
        readMessages();
        if (chat)
            Qt.callLater(messageList.positionViewAtEnd);
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 110 }
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(4 / 255, 3 / 255, 12 / 255, 0.55)
    }

    background: Rectangle {
        color: C.Theme.ground

        Rectangle {
            anchors {
                left: parent.left
                top: parent.top
                bottom: parent.bottom
            }
            width: 1
            color: C.Theme.lineStrong
        }
    }

    Connections {
        target: panel.fr
        function onChanged() {
            panel.readMessages();
        }
        function onLinkLooked(ok, fp, name, avatar, error) {
            panel.looking = false;
            panel.looked = ok ? { fp: fp, name: name || qsTr("Somebody without a name"), avatar: avatar } : null;
            panel.lookError = ok ? "" : error;
        }
    }

    TextEdit {
        id: clipboard
        visible: false
    }

    Timer {
        id: copied
        interval: 1800
    }

    Timer {
        interval: 60000
        repeat: true
        running: panel.opened
        onTriggered: panel.now = Date.now()
    }

    // Looks a pasted link up a moment after typing stops
    Timer {
        id: lookTimer
        interval: 350
        onTriggered: {
            const text = linkField.text.trim();
            if (text.length === 0) {
                panel.looked = null;
                panel.lookError = "";
                return;
            }
            panel.looking = true;
            panel.fr.lookUp(text);
        }
    }

    // ------------------------------------------------------------ pieces

    /** A round picture with the status as a dot on its edge. */
    component FriendAvatar: Item {
        id: fa
        property string source
        property string name
        /** online, away, busy, web, offline; empty: no dot */
        property string presence: ""
        property int size: 40
        implicitWidth: size
        implicitHeight: size

        C.Avatar {
            anchors.fill: parent
            source: fa.source
            name: fa.name || "?"
        }

        Rectangle {
            visible: fa.presence.length > 0
            width: Math.round(fa.size * 0.32)
            height: width
            radius: width / 2
            x: fa.size - width + 1
            y: fa.size - height + 1
            color: panel.stateColor(fa.presence)
            border.width: 2
            border.color: C.Theme.ground
        }
    }

    /** A group's picture: the brand's colours with people on them. */
    component GroupTile: Rectangle {
        property int size: 40
        property bool dim: false
        implicitWidth: size
        implicitHeight: size
        radius: size * 0.3
        opacity: dim ? 0.5 : 1
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#00b8d4" }
            GradientStop { position: 1.0; color: C.Theme.brandFrom }
        }

        C.Icon {
            anchors.centerIn: parent
            iconName: "people"
            size: Math.round(parent.size * 0.5)
            color: "#ffffff"
        }
    }

    component Heading: Label {
        Layout.fillWidth: true
        Layout.topMargin: 6
        font.pixelSize: 12
        font.weight: Font.Bold
        font.letterSpacing: 0.6
        color: C.Theme.headingSoft
    }

    // ------------------------------------------------------------ the three faces

    contentItem: Item {
        Material.theme: Material.Dark
        Material.accent: C.Theme.brandFrom

        // ---------------------------------------------------- switched off
        Flickable {
            id: offView
            anchors.fill: parent
            visible: !panel.fr || !panel.fr.enabled
            contentHeight: offColumn.implicitHeight + 48
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: offColumn
                x: 24
                y: 20
                width: offView.width - 48
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: qsTr("Friends")
                        font.pixelSize: 22
                        font.bold: true
                        color: C.Theme.text
                    }

                    C.IconButton {
                        iconName: "close"
                        tip: qsTr("Close")
                        onClicked: panel.close()
                    }
                }

                C.Icon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 16
                    iconName: "people"
                    size: 56
                    color: C.Theme.brandFrom
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.pixelSize: 18
                    font.bold: true
                    color: C.Theme.text
                    text: qsTr("See who plays, chat, agree on a time")
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.pixelSize: 14
                    lineHeight: 1.2
                    color: C.Theme.textSoft
                    text: qsTr("Your friends see whether you are online and what you play, and join your sessions with one click. Chats work outside of sessions too.")
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    radius: C.Theme.radiusField
                    color: C.Theme.card
                    border.width: 1
                    border.color: C.Theme.line
                    implicitHeight: privacyColumn.implicitHeight + 24

                    ColumnLayout {
                        id: privacyColumn
                        anchors {
                            left: parent.left
                            right: parent.right
                            top: parent.top
                            margins: 12
                        }
                        spacing: 6

                        RowLayout {
                            spacing: 8

                            C.Icon {
                                iconName: "lock"
                                size: 16
                                color: C.Theme.good
                            }

                            Label {
                                textFormat: Text.PlainText
                                text: qsTr("Private by design")
                                font.pixelSize: 14
                                font.bold: true
                                color: C.Theme.text
                            }
                        }

                        Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            color: C.Theme.textSoft
                            text: qsTr("No account, no e-mail address. Chats and statuses are end-to-end encrypted: the friends server only passes on sealed notes it cannot read. Your keys and chats stay on your devices.")
                        }
                    }
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    visible: text.length > 0
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                    color: C.Theme.bad
                    text: panel.fr ? panel.fr.problem : ""
                }

                C.PillButton {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 6
                    kind: "brand"
                    iconName: "people"
                    text: qsTr("Switch on friends")
                    onClicked: panel.fr.enable()
                }

                C.TextLink {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("From a backup or my other device…")
                    onClicked: {
                        panel.close();
                        root.showSettingsDialog("friends");
                    }
                }

                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: C.Theme.textDim
                    text: qsTr("Friends see the name and picture of your profile.")
                }
            }
        }

        // ---------------------------------------------------- the lists
        ColumnLayout {
            id: listsView
            anchors.fill: parent
            visible: !!panel.fr && panel.fr.enabled && panel.chat === ""
            spacing: 0

            // Title, own status
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 12
                Layout.topMargin: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        text: qsTr("Friends")
                        font.pixelSize: 22
                        font.bold: true
                        color: C.Theme.text
                    }

                    C.IconButton {
                        iconName: "sliders"
                        tip: qsTr("Friends settings")
                        onClicked: {
                            panel.close();
                            root.showSettingsDialog("friends");
                        }
                    }

                    C.IconButton {
                        iconName: "close"
                        tip: qsTr("Close")
                        onClicked: panel.close()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.rightMargin: 8
                    spacing: 10

                    FriendAvatar {
                        size: 36
                        source: Chiaki.settings.coplayAvatarUrl
                        name: Chiaki.settings.coplayName
                        presence: panel.fr.myState === "invisible" ? "offline" : panel.fr.myState
                    }

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 15
                        font.bold: true
                        color: C.Theme.text
                        text: Chiaki.settings.coplayName || qsTr("No name yet")
                    }

                    C.Dropdown {
                        id: stateBox
                        Accessible.name: qsTr("Your status")
                        model: [qsTr("Online"), qsTr("Away"), qsTr("Busy"), qsTr("Invisible")]
                        readonly property var states: ["online", "away", "busy", "invisible"]
                        currentIndex: Math.max(0, states.indexOf(panel.fr.myState))
                        onActivated: (index) => panel.fr.myState = states[index]
                    }
                }

                C.Field {
                    id: freeField
                    Layout.fillWidth: true
                    Layout.rightMargin: 8
                    iconName: "clock"
                    hint: qsTr("When you're free, e.g. “tonight from 8”")
                    maximumLength: 80
                    text: panel.fr.myFree
                    onEditingFinished: panel.fr.myFree = text.trim()
                }

                // The server cannot be reached: what is shown is from the last time
                Label {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    Layout.rightMargin: 8
                    visible: !panel.fr.online
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: C.Theme.wait
                    text: qsTr("The friends server cannot be reached right now. Messages are sent once it answers again.")
                }

                C.Segmented {
                    Layout.fillWidth: true
                    Layout.rightMargin: 8
                    Layout.topMargin: 2
                    model: [panel.incomingCount > 0 ? qsTr("Friends (%1 new)").arg(panel.incomingCount) : qsTr("Friends"),
                        panel.fr.unread > 0 ? qsTr("Chats (%1)").arg(panel.fr.unread) : qsTr("Chats")]
                    currentIndex: panel.tab
                    onActivated: (index) => panel.tab = index
                }
            }

            Flickable {
                id: listFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 8
                contentHeight: listColumn.implicitHeight + 24
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    id: listColumn
                    x: 12
                    width: listFlick.width - 24
                    spacing: 4

                    // ---------------- friends: add, requests, the list
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panel.tab === 0
                        spacing: 4

                        // Add a friend with their link; one's own link to give out
                        Rectangle {
                            Layout.fillWidth: true
                            radius: C.Theme.radiusCard - 6
                            color: C.Theme.card
                            border.width: 1
                            border.color: C.Theme.line
                            implicitHeight: addColumn.implicitHeight + 24

                            ColumnLayout {
                                id: addColumn
                                anchors {
                                    left: parent.left
                                    right: parent.right
                                    top: parent.top
                                    margins: 12
                                }
                                spacing: 8

                                Label {
                                    textFormat: Text.PlainText
                                    text: qsTr("Add a friend")
                                    font.pixelSize: 14
                                    font.bold: true
                                    color: C.Theme.text
                                }

                                C.Field {
                                    id: linkField
                                    Layout.fillWidth: true
                                    iconName: "link"
                                    hint: qsTr("Paste their friend link")
                                    onTextChanged: lookTimer.restart()
                                }

                                Label {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    visible: panel.lookError.length > 0 && linkField.text.trim().length > 0
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 12
                                    color: C.Theme.bad
                                    text: panel.lookError
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: !!panel.looked && linkField.text.trim().length > 0
                                    spacing: 10

                                    C.Avatar {
                                        Layout.preferredWidth: 36
                                        Layout.preferredHeight: 36
                                        source: panel.looked ? panel.looked.avatar : ""
                                        name: panel.looked ? panel.looked.name : "?"
                                    }

                                    Label {
                                        textFormat: Text.PlainText
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.pixelSize: 15
                                        font.bold: true
                                        color: C.Theme.text
                                        text: panel.looked ? panel.looked.name : ""
                                    }
                                }

                                C.Field {
                                    id: noteField
                                    Layout.fillWidth: true
                                    visible: !!panel.looked && linkField.text.trim().length > 0 && !panel.friendFor(panel.looked.fp)
                                    maximumLength: 200
                                    hint: qsTr("A line for them (optional): who you are")
                                }

                                Label {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    visible: !!panel.looked && linkField.text.trim().length > 0 && !!panel.friendFor(panel.looked.fp)
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 13
                                    color: C.Theme.good
                                    text: qsTr("%1 is your friend already.").arg(panel.looked ? panel.looked.name : "")
                                }

                                C.PillButton {
                                    visible: !!panel.looked && linkField.text.trim().length > 0 && !panel.friendFor(panel.looked.fp)
                                    kind: "primary"
                                    small: true
                                    iconName: "plus"
                                    text: qsTr("Send friend request")
                                    onClicked: {
                                        panel.fr.sendRequest(linkField.text.trim(), noteField.text.trim());
                                        linkField.clear();
                                        noteField.clear();
                                        panel.looked = null;
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 2
                                    implicitHeight: 1
                                    color: C.Theme.line
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

                                        Label {
                                            textFormat: Text.PlainText
                                            text: copied.running ? qsTr("Copied") : qsTr("Your friend link")
                                            font.pixelSize: 13
                                            font.bold: true
                                            color: copied.running ? C.Theme.good : C.Theme.text
                                        }

                                        Label {
                                            textFormat: Text.PlainText
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: 12
                                            color: C.Theme.textDim
                                            text: qsTr("Give it to the people you want as friends. Whoever has it can send you a request.")
                                        }
                                    }

                                    C.PillButton {
                                        id: copyButton
                                        small: true
                                        iconName: "copy"
                                        text: qsTr("Copy")
                                        onClicked: copyMenu.popup(copyButton, 0, copyButton.height + 4)

                                        C.Menu {
                                            id: copyMenu

                                            MenuItem {
                                                text: qsTr("For the app")
                                                onTriggered: panel.copyText(panel.fr.link)
                                            }
                                            MenuItem {
                                                text: qsTr("For a web browser")
                                                onTriggered: panel.copyText(panel.fr.webLink)
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        // Requests: theirs to answer, ours waiting
                        Heading {
                            visible: panel.fr.requests.length > 0
                            text: qsTr("REQUESTS")
                        }

                        Repeater {
                            model: panel.fr.requests

                            delegate: Rectangle {
                                id: req
                                required property var modelData
                                Layout.fillWidth: true
                                radius: 14
                                color: modelData.incoming ? Qt.rgba(140 / 255, 82 / 255, 1, 0.10) : "transparent"
                                border.width: modelData.incoming ? 1 : 0
                                border.color: Qt.rgba(140 / 255, 82 / 255, 1, 0.35)
                                implicitHeight: reqRow.implicitHeight + 20

                                RowLayout {
                                    id: reqRow
                                    anchors {
                                        left: parent.left
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                        leftMargin: 10
                                        rightMargin: 8
                                    }
                                    spacing: 10

                                    C.Avatar {
                                        Layout.alignment: Qt.AlignTop
                                        Layout.preferredWidth: 38
                                        Layout.preferredHeight: 38
                                        source: req.modelData.avatar || ""
                                        name: req.modelData.name || "?"
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Label {
                                            textFormat: Text.PlainText
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            font.pixelSize: 14
                                            font.bold: true
                                            color: C.Theme.text
                                            text: req.modelData.name || qsTr("Somebody")
                                        }

                                        Label {
                                            textFormat: Text.PlainText
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            font.pixelSize: 12
                                            color: req.modelData.unsent ? C.Theme.bad : C.Theme.textSoft
                                            text: req.modelData.incoming
                                                  ? (req.modelData.text ? "“" + req.modelData.text + "”" : qsTr("wants to be your friend"))
                                                  : req.modelData.unsent ? qsTr("Not sent: the friends server did not take it")
                                                                         : qsTr("Waiting for an answer")
                                        }

                                        RowLayout {
                                            visible: req.modelData.incoming
                                            Layout.topMargin: 4
                                            spacing: 6

                                            C.PillButton {
                                                kind: "success"
                                                small: true
                                                text: qsTr("Accept")
                                                onClicked: panel.fr.accept(req.modelData.fp)
                                            }

                                            C.PillButton {
                                                small: true
                                                text: qsTr("Decline")
                                                onClicked: panel.fr.decline(req.modelData.fp)
                                            }

                                            C.IconButton {
                                                iconName: "block"
                                                small: true
                                                tip: qsTr("Block")
                                                onClicked: panel.askBlock(req.modelData.fp, req.modelData.name)
                                            }
                                        }
                                    }

                                    C.TextLink {
                                        visible: !req.modelData.incoming && req.modelData.unsent
                                        text: qsTr("Send again")
                                        onClicked: panel.fr.resendRequest(req.modelData.fp)
                                    }

                                    C.IconButton {
                                        visible: !req.modelData.incoming
                                        iconName: "close"
                                        small: true
                                        tip: qsTr("Take back the request")
                                        onClicked: panel.fr.decline(req.modelData.fp)
                                    }
                                }
                            }
                        }

                        Heading {
                            visible: panel.fr.friends.length > 0
                            text: qsTr("FRIENDS")
                        }

                        Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            Layout.topMargin: 18
                            visible: panel.fr.friends.length === 0 && panel.fr.requests.length === 0
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            color: C.Theme.textDim
                            text: qsTr("No friends yet. Send your friend link to somebody, or paste theirs above.")
                        }

                        Repeater {
                            model: panel.fr.friends

                            delegate: Rectangle {
                                id: row
                                required property var modelData
                                readonly property bool canJoin: !!modelData.room && !!modelData.room.invite
                                Layout.fillWidth: true
                                radius: 14
                                color: rowHover.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                                implicitHeight: friendRow.implicitHeight + 16

                                HoverHandler {
                                    id: rowHover
                                }

                                TapHandler {
                                    onTapped: panel.chat = row.modelData.fp
                                }

                                RowLayout {
                                    id: friendRow
                                    anchors {
                                        left: parent.left
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                        leftMargin: 10
                                        rightMargin: 4
                                    }
                                    spacing: 10

                                    FriendAvatar {
                                        size: 40
                                        source: row.modelData.avatar || ""
                                        name: row.modelData.name
                                        presence: row.modelData.state
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 5

                                            Label {
                                                textFormat: Text.PlainText
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                                font.pixelSize: 14
                                                font.bold: true
                                                color: C.Theme.text
                                                text: row.modelData.name || qsTr("A friend")
                                            }

                                            C.Icon {
                                                visible: row.modelData.checked
                                                iconName: "shield"
                                                size: 13
                                                color: C.Theme.good
                                                Accessible.name: qsTr("Safety code checked")
                                            }
                                        }

                                        Label {
                                            textFormat: Text.PlainText
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            font.pixelSize: 12
                                            color: row.modelData.state === "offline" ? C.Theme.textDim : C.Theme.textSoft
                                            text: panel.stateText(row.modelData)
                                        }

                                        Label {
                                            textFormat: Text.PlainText
                                            Layout.fillWidth: true
                                            visible: !!row.modelData.free
                                            elide: Text.ElideRight
                                            font.pixelSize: 12
                                            color: C.Theme.focus
                                            text: qsTr("Free: %1").arg(row.modelData.free || "")
                                        }
                                    }

                                    C.PillButton {
                                        visible: row.canJoin
                                        kind: "success"
                                        small: true
                                        text: qsTr("Join")
                                        onClicked: panel.joinRoom(row.modelData.room)
                                    }

                                    C.IconButton {
                                        iconName: "chat"
                                        small: true
                                        tip: qsTr("Chat with %1").arg(row.modelData.name)
                                        onClicked: panel.chat = row.modelData.fp
                                    }

                                    C.IconButton {
                                        id: friendMore
                                        iconName: "more"
                                        small: true
                                        tip: qsTr("More")
                                        onClicked: friendMenu.popup(friendMore, 0, friendMore.height + 4)

                                        C.Menu {
                                            id: friendMenu

                                            MenuItem {
                                                text: row.modelData.checked ? qsTr("Safety code (checked)…") : qsTr("Safety code…")
                                                onTriggered: panel.showSafetyCode(row.modelData)
                                            }
                                            MenuItem {
                                                text: qsTr("Remove as friend…")
                                                onTriggered: panel.askUnfriend(row.modelData)
                                            }
                                            MenuItem {
                                                text: qsTr("Block…")
                                                onTriggered: panel.askBlock(row.modelData.fp, row.modelData.name)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // ---------------- chats
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: panel.tab === 1
                        spacing: 2

                        C.PillButton {
                            visible: panel.fr.friends.length > 0
                            Layout.bottomMargin: 6
                            small: true
                            iconName: "plus"
                            text: qsTr("New group")
                            onClicked: panel.sheet = "newgroup"
                        }

                        Label {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            Layout.topMargin: 18
                            visible: panel.fr.chats.length === 0
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            color: C.Theme.textDim
                            text: qsTr("No chats yet. Open a friend to write to them.")
                        }

                        Repeater {
                            model: panel.fr.chats

                            delegate: Rectangle {
                                id: chatRow
                                required property var modelData
                                readonly property var who: panel.friendFor(modelData.id)
                                Layout.fillWidth: true
                                radius: 14
                                color: chatHover.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                                implicitHeight: chatLine.implicitHeight + 16

                                HoverHandler {
                                    id: chatHover
                                    cursorShape: Qt.PointingHandCursor
                                }

                                TapHandler {
                                    onTapped: panel.chat = chatRow.modelData.id
                                }

                                RowLayout {
                                    id: chatLine
                                    anchors {
                                        left: parent.left
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                        leftMargin: 10
                                        rightMargin: 10
                                    }
                                    spacing: 10

                                    FriendAvatar {
                                        visible: chatRow.modelData.kind !== "group"
                                        size: 40
                                        source: chatRow.modelData.avatar || ""
                                        name: chatRow.modelData.title
                                        presence: chatRow.who ? chatRow.who.state : ""
                                    }

                                    GroupTile {
                                        visible: chatRow.modelData.kind === "group"
                                        size: 40
                                        dim: !!chatRow.modelData.left
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1

                                        RowLayout {
                                            Layout.fillWidth: true

                                            Label {
                                                textFormat: Text.PlainText
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                                font.pixelSize: 14
                                                font.bold: true
                                                color: C.Theme.text
                                                text: chatRow.modelData.title || qsTr("A former friend")
                                            }

                                            C.Icon {
                                                visible: !!chatRow.modelData.muted
                                                iconName: "bell-off"
                                                size: 13
                                                color: C.Theme.textDim
                                            }

                                            Label {
                                                textFormat: Text.PlainText
                                                font.pixelSize: 11
                                                color: C.Theme.textDim
                                                text: panel.timeText(chatRow.modelData.at)
                                            }
                                        }

                                        RowLayout {
                                            Layout.fillWidth: true

                                            Label {
                                                textFormat: Text.PlainText
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                                font.pixelSize: 12
                                                color: chatRow.modelData.unread > 0 ? C.Theme.text : C.Theme.textDim
                                                text: chatRow.modelData.last
                                            }

                                            // How many are unread: dimmed in a muted chat, "@" for a mention of you
                                            Rectangle {
                                                readonly property bool quiet: !!chatRow.modelData.muted && !(chatRow.modelData.mentions > 0)
                                                visible: chatRow.modelData.unread > 0
                                                Layout.preferredWidth: Math.max(20, unreadLabel.implicitWidth + 10)
                                                Layout.preferredHeight: 20
                                                radius: 10
                                                color: quiet ? Qt.rgba(1, 1, 1, 0.14) : C.Theme.brandTo

                                                Label {
                                                    textFormat: Text.PlainText
                                                    id: unreadLabel
                                                    anchors.centerIn: parent
                                                    font.pixelSize: 11
                                                    font.bold: true
                                                    color: parent.quiet ? C.Theme.textSoft : "white"
                                                    text: chatRow.modelData.mentions > 0 ? "@" : chatRow.modelData.unread
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------- one chat
        ColumnLayout {
            id: chatView
            anchors.fill: parent
            visible: !!panel.fr && panel.fr.enabled && panel.chat !== ""
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                Layout.topMargin: 12
                Layout.bottomMargin: 8
                spacing: 8

                C.IconButton {
                    iconName: "back"
                    tip: qsTr("Back to the lists")
                    onClicked: panel.chat = ""
                }

                FriendAvatar {
                    visible: !panel.chatGroup
                    size: 38
                    source: panel.chatFriend ? panel.chatFriend.avatar || "" : ""
                    name: panel.chatFriend ? panel.chatFriend.name : "?"
                    presence: panel.chatFriend ? panel.chatFriend.state : ""
                }

                GroupTile {
                    visible: !!panel.chatGroup
                    size: 38
                    dim: !!panel.chatGroup && panel.chatGroup.left
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 16
                        font.bold: true
                        color: C.Theme.text
                        text: panel.chatGroup ? panel.chatGroup.name
                              : panel.chatFriend ? panel.chatFriend.name : qsTr("No longer a friend")
                    }

                    RowLayout {
                        visible: panel.mutedUntil > 0
                        spacing: 4

                        C.Icon {
                            iconName: "bell-off"
                            size: 12
                            color: C.Theme.textDim
                        }

                        Label {
                            textFormat: Text.PlainText
                            font.pixelSize: 11
                            color: C.Theme.textDim
                            text: panel.mutedUntil > 9.0e14 ? qsTr("Muted")
                                  : qsTr("Muted until %1").arg(Qt.formatTime(new Date(panel.mutedUntil), "hh:mm"))
                        }
                    }

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        color: C.Theme.textSoft
                        text: panel.chatGroup
                              ? (panel.chatGroup.left ? qsTr("You are no longer in this group")
                                                      : (panel.chatGroup.members.length === 1 ? qsTr("1 member") : qsTr("%1 members").arg(panel.chatGroup.members.length)))
                              : panel.chatFriend ? panel.stateText(panel.chatFriend) : qsTr("Messages can no longer be sent")
                    }
                }

                C.PillButton {
                    visible: !!panel.chatFriend && !!panel.chatFriend.room && !!panel.chatFriend.room.invite
                    kind: "success"
                    small: true
                    text: qsTr("Join")
                    onClicked: panel.joinRoom(panel.chatFriend.room)
                }

                C.IconButton {
                    id: chatMore
                    visible: !!panel.chatFriend || (!!panel.chatGroup && !panel.chatGroup.left)
                    iconName: "more"
                    tip: qsTr("More")
                    onClicked: chatMenu.popup(chatMore, 0, chatMore.height + 4)

                    C.Menu {
                        id: chatMenu

                        MenuItem {
                            visible: !!panel.chatGroup
                            height: visible ? implicitHeight : 0
                            text: qsTr("Members…")
                            onTriggered: panel.sheet = "members"
                        }
                        // Muting: no notifications, unless a message mentions you
                        MenuItem {
                            visible: panel.mutedUntil === 0
                            height: visible ? implicitHeight : 0
                            text: qsTr("Mute for 1 hour")
                            onTriggered: panel.muteFor(60)
                        }
                        MenuItem {
                            visible: panel.mutedUntil === 0
                            height: visible ? implicitHeight : 0
                            text: qsTr("Mute for 8 hours")
                            onTriggered: panel.muteFor(480)
                        }
                        MenuItem {
                            visible: panel.mutedUntil === 0
                            height: visible ? implicitHeight : 0
                            text: qsTr("Mute until I turn it back on")
                            onTriggered: panel.muteFor(-1)
                        }
                        MenuItem {
                            visible: panel.mutedUntil > 0
                            height: visible ? implicitHeight : 0
                            text: qsTr("Unmute")
                            onTriggered: panel.muteFor(0)
                        }
                        MenuItem {
                            text: qsTr("Plan a time…")
                            onTriggered: panel.sheet = "plan"
                        }
                        MenuItem {
                            visible: !panel.chatGroup
                            height: visible ? implicitHeight : 0
                            text: qsTr("Safety code…")
                            onTriggered: panel.showSafetyCode(panel.chatFriend)
                        }
                        MenuItem {
                            text: qsTr("Report…")
                            onTriggered: panel.askReport()
                        }
                        MenuItem {
                            visible: !!panel.chatGroup
                            height: visible ? implicitHeight : 0
                            text: qsTr("Leave group…")
                            onTriggered: panel.askLeave()
                        }
                        MenuItem {
                            visible: !panel.chatGroup
                            height: visible ? implicitHeight : 0
                            text: qsTr("Remove as friend…")
                            onTriggered: panel.askUnfriend(panel.chatFriend)
                        }
                        MenuItem {
                            visible: !panel.chatGroup
                            height: visible ? implicitHeight : 0
                            text: qsTr("Block…")
                            onTriggered: panel.askBlock(panel.chatFriend.fp, panel.chatFriend.name)
                        }
                    }
                }

                C.IconButton {
                    iconName: "close"
                    tip: qsTr("Close")
                    onClicked: panel.close()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: C.Theme.line
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Column {
                    anchors.centerIn: parent
                    width: parent.width - 60
                    visible: messageList.count === 0
                    spacing: 6

                    C.Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        iconName: "lock"
                        size: 30
                        color: C.Theme.good
                    }

                    Label {
                        textFormat: Text.PlainText
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.pixelSize: 13
                        color: C.Theme.textSoft
                        text: panel.chatGroup
                              ? qsTr("Messages in %1 are end-to-end encrypted. They are kept on your devices, not on the server.").arg(panel.chatGroup.name)
                              : qsTr("Messages with %1 are end-to-end encrypted. They are kept on your devices, not on the server.").arg(panel.chatFriend ? panel.chatFriend.name : qsTr("this friend"))
                    }
                }

                ListView {
                    id: messageList
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    spacing: 4
                    model: panel.messages
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    /** At the newest message: it stays there as messages, reactions and pictures come. */
                    property bool stickToEnd: true
                    onMovementEnded: stickToEnd = atYEnd
                    onCountChanged: {
                        if (stickToEnd || count < 6)
                            Qt.callLater(positionViewAtEnd);
                    }
                    onContentHeightChanged: {
                        if (stickToEnd)
                            Qt.callLater(positionViewAtEnd);
                    }
                    // Smaller while answering or choosing whom to mention: the end stays in view
                    onHeightChanged: {
                        if (stickToEnd)
                            Qt.callLater(positionViewAtEnd);
                    }

                    delegate: Item {
                        id: bubble
                        required property var modelData
                        required property int index
                        readonly property bool mine: !!modelData.mine
                        readonly property bool system: !!modelData.system
                        // A run of messages by one writer shows their picture and name once
                        readonly property var previous: index > 0 ? panel.messages[index - 1] : null
                        readonly property bool first: system || !(previous && previous.from === modelData.from && !previous.system)
                        readonly property bool deleted: !!modelData.deleted
                        width: messageList.width

                        function highlight() {
                            message.highlight();
                        }
                        height: (system ? systemText.implicitHeight + 10 : message.implicitHeight) + 2

                        Label {
                            id: systemText
                            textFormat: Text.PlainText
                            visible: bubble.system
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 5
                            width: Math.min(implicitWidth, parent.width - 40)
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: C.Theme.textDim
                            text: bubble.modelData.text || ""
                        }

                        ChatBubble {
                            id: message
                            visible: !bubble.system
                            width: parent.width
                            mine: bubble.mine
                            first: bubble.first
                            showAuthor: !!panel.chatGroup
                            name: bubble.modelData.name || qsTr("A member")
                            avatar: panel.avatarOf(bubble.modelData.from)
                            html: bubble.modelData.html || ""
                            embed: bubble.modelData.embed || null
                            status: bubble.modelData.state === "failed" ? qsTr("Not sent")
                                    : bubble.modelData.state === "sending" ? qsTr("Sending…") : panel.timeText(bubble.modelData.at)
                            statusBad: bubble.modelData.state === "failed"
                            reactions: bubble.deleted ? [] : panel.reactionsOf(bubble.modelData)
                            readers: panel.readMarks[bubble.index] || []
                            card: bubble.deleted ? null : bubble.modelData.join ? inviteCardComponent : bubble.modelData.plan ? planCardComponent : null
                            deleted: bubble.deleted
                            mentionsMe: !!bubble.modelData.mentionsMe
                            quote: bubble.deleted ? null : panel.quoteOf(bubble.modelData)
                            canReact: !bubble.deleted
                            canReply: !bubble.deleted && panel.canWrite
                            canDelete: bubble.mine && !bubble.deleted
                            onReplyRequested: {
                                const m = bubble.modelData;
                                panel.replyTo = { id: m.id, name: bubble.mine ? qsTr("yourself") : (m.name || qsTr("a member")),
                                                  text: m.join ? (m.join.title || m.text || "") : m.plan ? (m.plan.title || m.text || "") : (m.text || "") };
                                composer.focusInput();
                            }
                            onDeleteRequested: {
                                const id = bubble.modelData.id;
                                const chat = panel.chat;
                                deleteDialog.ask(() => panel.fr.deleteMessage(chat, id));
                            }
                            onQuoteClicked: panel.goTo(bubble.modelData.replyTo ? bubble.modelData.replyTo.id : "")
                            onReact: (emoji) => panel.fr.react(panel.chat, bubble.modelData.id, emoji)
                            onMoreReactions: panel.pickReaction(bubble.modelData.id)
                            onLinkActivated: (link) => panel.openLink(link)

                            Component {
                                id: inviteCardComponent

                                InviteCard {
                                    card: bubble.modelData.join || ({})
                                    mine: bubble.mine
                                    person: panel.chatGroup ? (bubble.modelData.name || qsTr("A member"))
                                            : panel.chatFriend ? panel.chatFriend.name : qsTr("A friend")
                                    live: bubble.modelData.join ? panel.liveFor(bubble.modelData, panel.now) : ""
                                    time: bubble.modelData.state === "failed" ? qsTr("Not sent")
                                          : bubble.modelData.state === "sending" ? qsTr("Sending…") : panel.timeText(bubble.modelData.at)
                                    onJoin: panel.joinRoom(bubble.modelData.join)
                                }
                            }

                            Component {
                                id: planCardComponent

                                PlanCard {
                                    plan: bubble.modelData.plan || ({})
                                    votes: bubble.modelData.votes || ({})
                                    me: panel.fr ? panel.fr.fingerprint : ""
                                    mine: bubble.mine
                                    person: bubble.modelData.name || qsTr("A friend")
                                    people: panel.chatGroup ? panel.chatGroup.members.length : 2
                                    time: panel.timeText(bubble.modelData.at)
                                    onAnswer: (answers) => panel.fr.vote(panel.chat, bubble.modelData.id, answers)
                                }
                            }
                        }
                    }
                }
            }

            EmojiPicker {
                id: writePicker
                x: 10
                y: chatView.height - height - 70
                width: chatView.width - 20
                height: Math.min(300, chatView.height - 160)
                onPicked: (emoji) => composer.insert(emoji)
                onClosed: composer.focusInput()
            }

            EmojiPicker {
                id: reactionPicker
                once: true
                x: 10
                y: Math.max(60, chatView.height / 2 - height / 2)
                width: chatView.width - 20
                height: Math.min(300, chatView.height - 160)
                onPicked: (emoji) => panel.fr.react(panel.chat, panel.reactingTo, emoji)
            }

            ChatDeleteDialog {
                id: deleteDialog
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: C.Theme.line
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 10
                spacing: 8
                visible: !!panel.chatFriend || (!!panel.chatGroup && !panel.chatGroup.left)

                C.PillButton {
                    visible: !!panel.hosted
                    small: true
                    iconName: "enter"
                    text: panel.hosted && panel.hosted.coplayInviteCard.title
                          ? qsTr("Invite to %1").arg(panel.hosted.coplayInviteCard.title) : qsTr("Invite to my session")
                    onClicked: panel.inviteToRoom()
                }

                // As in a session's chat
                ChatComposer {
                    id: composer
                    objectName: "composer"
                    Layout.fillWidth: true
                    hint: qsTr("Message")
                    maximumLength: 4000
                    accessibleName: qsTr("Message to %1").arg(panel.chatGroup ? panel.chatGroup.name : panel.chatFriend ? panel.chatFriend.name : "")
                    members: panel.mentionable
                    replyTo: panel.replyTo
                    onSend: (text) => panel.sendMessage(text)
                    onEmojiClicked: writePicker.opened ? writePicker.close() : writePicker.open()
                    onReplyCancelled: panel.replyTo = null
                }
            }
        }

        // ---------------------------------------------------- sheets over the panel
        Rectangle {
            id: sheetView
            anchors.fill: parent
            visible: panel.sheet !== "" && !!panel.fr && panel.fr.enabled
            color: C.Theme.ground

            /** Friends picked on the new-group sheet or to add to a group. */
            property var picked: []
            /** The times of a plan being made (ms). */
            property var planTimes: []

            function togglePick(fp) {
                const list = picked.slice();
                const at = list.indexOf(fp);
                if (at >= 0)
                    list.splice(at, 1);
                else
                    list.push(fp);
                picked = list;
            }

            function inGroup(fp) {
                if (!panel.chatGroup)
                    return false;
                for (let i = 0; i < panel.chatGroup.members.length; ++i) {
                    if (panel.chatGroup.members[i].fp === fp)
                        return true;
                }
                return false;
            }

            /** The days to choose from for a plan: today and the next six. */
            readonly property var days: {
                const out = [];
                const now = new Date();
                for (let i = 0; i < 7; ++i) {
                    const d = new Date(now.getFullYear(), now.getMonth(), now.getDate() + i);
                    out.push({ date: d, label: i === 0 ? qsTr("Today") : i === 1 ? qsTr("Tomorrow") : Qt.formatDate(d, "dddd d MMM") });
                }
                return out;
            }
            readonly property var hours: ["14:00", "15:00", "16:00", "17:00", "18:00", "19:00", "19:30", "20:00", "20:30", "21:00", "22:00", "23:00"]

            onVisibleChanged: {
                picked = [];
                planTimes = [];
                groupName.text = "";
                planTitle.text = "";
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Layout.topMargin: 12
                    Layout.bottomMargin: 8
                    spacing: 8

                    C.IconButton {
                        iconName: "back"
                        tip: qsTr("Back")
                        onClicked: panel.sheet = ""
                    }

                    Label {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: 18
                        font.bold: true
                        color: C.Theme.text
                        text: panel.sheet === "newgroup" ? qsTr("New group")
                              : panel.sheet === "members" ? (panel.chatGroup ? panel.chatGroup.name : "")
                              : qsTr("Plan a time")
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: C.Theme.line
                }

                Flickable {
                    id: sheetFlick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentHeight: sheetColumn.implicitHeight + 32
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    ColumnLayout {
                        id: sheetColumn
                        x: 16
                        y: 14
                        width: sheetFlick.width - 32
                        spacing: 10

                        // ---------------- a new group
                        C.Field {
                            id: groupName
                            visible: panel.sheet === "newgroup"
                            Layout.fillWidth: true
                            maximumLength: 48
                            hint: qsTr("The group's name, e.g. “Friday racing”")
                        }

                        // ---------------- members
                        Heading {
                            visible: panel.sheet === "members"
                            text: qsTr("MEMBERS")
                        }

                        Repeater {
                            model: panel.sheet === "members" && panel.chatGroup ? panel.chatGroup.members : []

                            delegate: RowLayout {
                                id: memberRow
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 10

                                FriendAvatar {
                                    size: 34
                                    name: memberRow.modelData.name
                                    presence: ""
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0

                                    Label {
                                        textFormat: Text.PlainText
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.pixelSize: 14
                                        font.bold: true
                                        color: C.Theme.text
                                        text: memberRow.modelData.me ? qsTr("%1 (you)").arg(memberRow.modelData.name) : memberRow.modelData.name
                                    }

                                    Label {
                                        textFormat: Text.PlainText
                                        visible: !memberRow.modelData.friend && !memberRow.modelData.me
                                        font.pixelSize: 11
                                        color: C.Theme.textDim
                                        text: qsTr("Not your friend: known from this group")
                                    }
                                }

                                C.Badge {
                                    visible: memberRow.modelData.admin
                                    small: true
                                    tone: "brand"
                                    text: qsTr("Admin")
                                }

                                C.IconButton {
                                    id: memberMore
                                    visible: !!panel.chatGroup && panel.chatGroup.admin && !memberRow.modelData.me
                                    iconName: "more"
                                    small: true
                                    tip: qsTr("More")
                                    onClicked: memberMenu.popup(memberMore, 0, memberMore.height + 4)

                                    C.Menu {
                                        id: memberMenu

                                        MenuItem {
                                            text: memberRow.modelData.admin ? qsTr("No longer admin") : qsTr("Make admin")
                                            onTriggered: panel.fr.setGroupAdmin(panel.chat, memberRow.modelData.fp, !memberRow.modelData.admin)
                                        }
                                        MenuItem {
                                            text: qsTr("Remove from the group…")
                                            onTriggered: root.showConfirmDialog(qsTr("Remove %1?").arg(memberRow.modelData.name),
                                                qsTr("%1 no longer gets the group's messages. They can be added again.").arg(memberRow.modelData.name),
                                                () => panel.fr.removeFromGroup(panel.chat, memberRow.modelData.fp, false))
                                        }
                                        MenuItem {
                                            text: qsTr("Remove and ban…")
                                            onTriggered: root.showConfirmDialog(qsTr("Ban %1?").arg(memberRow.modelData.name),
                                                qsTr("%1 no longer gets the group's messages, and nobody but an admin can add them again.").arg(memberRow.modelData.name),
                                                () => panel.fr.removeFromGroup(panel.chat, memberRow.modelData.fp, true))
                                        }
                                    }
                                }
                            }
                        }

                        Heading {
                            visible: panel.sheet === "members" && !!panel.chatGroup && panel.chatGroup.banned.length > 0
                            text: qsTr("BANNED")
                        }

                        Repeater {
                            model: panel.sheet === "members" && panel.chatGroup ? panel.chatGroup.banned : []

                            delegate: Label {
                                required property var modelData
                                Layout.fillWidth: true
                                font.pixelSize: 13
                                color: C.Theme.textSoft
                                text: modelData.name
                            }
                        }

                        Heading {
                            visible: panel.sheet === "newgroup" || (panel.sheet === "members" && !!panel.chatGroup
                                     && (panel.chatGroup.admin || panel.chatGroup.add === "all"))
                            text: panel.sheet === "newgroup" ? qsTr("WHO IS IN IT") : qsTr("ADD FRIENDS")
                        }

                        Repeater {
                            model: panel.sheet === "newgroup" || (panel.sheet === "members" && !!panel.chatGroup
                                   && (panel.chatGroup.admin || panel.chatGroup.add === "all")) ? panel.fr.friends : []

                            delegate: Rectangle {
                                id: pickRow
                                required property var modelData
                                readonly property bool already: panel.sheet === "members" && sheetView.inGroup(modelData.fp)
                                readonly property bool on: sheetView.picked.indexOf(modelData.fp) >= 0
                                visible: !already
                                Layout.fillWidth: true
                                Layout.preferredHeight: visible ? 52 : 0
                                radius: 12
                                color: on ? Qt.rgba(140 / 255, 82 / 255, 1, 0.16) : pickHover.hovered ? Qt.rgba(1, 1, 1, 0.05) : "transparent"

                                HoverHandler {
                                    id: pickHover
                                    cursorShape: Qt.PointingHandCursor
                                }

                                TapHandler {
                                    onTapped: sheetView.togglePick(pickRow.modelData.fp)
                                }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    spacing: 10

                                    FriendAvatar {
                                        size: 34
                                        source: pickRow.modelData.avatar || ""
                                        name: pickRow.modelData.name
                                        presence: pickRow.modelData.state
                                    }

                                    Label {
                                        textFormat: Text.PlainText
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.pixelSize: 14
                                        font.bold: true
                                        color: C.Theme.text
                                        text: pickRow.modelData.name || qsTr("A friend")
                                    }

                                    C.CheckBox {
                                        checked: pickRow.on
                                        Accessible.name: pickRow.modelData.name
                                        onClicked: sheetView.togglePick(pickRow.modelData.fp)
                                    }
                                }
                            }
                        }

                        C.PillButton {
                            visible: panel.sheet === "newgroup"
                            Layout.topMargin: 6
                            kind: "brand"
                            iconName: "people"
                            enabled: sheetView.picked.length > 0
                            text: sheetView.picked.length > 0 ? qsTr("Make the group (%1 friends)").arg(sheetView.picked.length) : qsTr("Pick friends first")
                            onClicked: {
                                const id = panel.fr.createGroup(groupName.text.trim(), sheetView.picked);
                                panel.sheet = "";
                                if (id) {
                                    panel.tab = 1;
                                    panel.chat = id;
                                }
                            }
                        }

                        C.PillButton {
                            visible: panel.sheet === "members" && sheetView.picked.length > 0
                            kind: "primary"
                            small: true
                            iconName: "plus"
                            text: qsTr("Add %1").arg(sheetView.picked.length)
                            onClicked: {
                                panel.fr.addToGroup(panel.chat, sheetView.picked);
                                sheetView.picked = [];
                            }
                        }

                        Heading {
                            visible: panel.sheet === "members" && !!panel.chatGroup && panel.chatGroup.admin
                            text: qsTr("SETTINGS")
                        }

                        C.Field {
                            id: renameField
                            visible: panel.sheet === "members" && !!panel.chatGroup && panel.chatGroup.admin
                            Layout.fillWidth: true
                            maximumLength: 48
                            hint: qsTr("The group's name")
                            text: panel.chatGroup ? panel.chatGroup.name : ""
                            onEditingFinished: {
                                if (panel.chatGroup && text.trim() && text.trim() !== panel.chatGroup.name)
                                    panel.fr.renameGroup(panel.chat, text.trim());
                            }
                        }

                        Label {
                            textFormat: Text.PlainText
                            visible: panel.sheet === "members" && !!panel.chatGroup && panel.chatGroup.admin
                            font.pixelSize: 13
                            font.bold: true
                            color: C.Theme.text
                            text: qsTr("Who may add people")
                        }

                        C.Segmented {
                            visible: panel.sheet === "members" && !!panel.chatGroup && panel.chatGroup.admin
                            Layout.fillWidth: true
                            model: [qsTr("Admins"), qsTr("Everybody")]
                            currentIndex: panel.chatGroup && panel.chatGroup.add === "all" ? 1 : 0
                            onActivated: (index) => panel.fr.setGroupAdd(panel.chat, index === 1 ? "all" : "admins")
                        }

                        // ---------------- a plan
                        C.Field {
                            id: planTitle
                            visible: panel.sheet === "plan"
                            Layout.fillWidth: true
                            maximumLength: 80
                            hint: qsTr("What for (optional), e.g. “Racing”")
                        }

                        Heading {
                            visible: panel.sheet === "plan"
                            text: qsTr("TIMES TO CHOOSE FROM")
                        }

                        Repeater {
                            model: panel.sheet === "plan" ? sheetView.planTimes : []

                            delegate: RowLayout {
                                id: timeRow
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true

                                C.Icon {
                                    iconName: "clock"
                                    size: 16
                                    color: C.Theme.focus
                                }

                                Label {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    font.pixelSize: 14
                                    font.bold: true
                                    color: C.Theme.text
                                    text: Qt.formatDateTime(new Date(timeRow.modelData), "dddd d MMM, hh:mm")
                                }

                                C.IconButton {
                                    iconName: "close"
                                    small: true
                                    tip: qsTr("Remove this time")
                                    onClicked: {
                                        const list = sheetView.planTimes.slice();
                                        list.splice(timeRow.index, 1);
                                        sheetView.planTimes = list;
                                    }
                                }
                            }
                        }

                        RowLayout {
                            visible: panel.sheet === "plan" && sheetView.planTimes.length < 8
                            Layout.fillWidth: true
                            spacing: 6

                            C.Dropdown {
                                id: dayBox
                                Layout.fillWidth: true
                                Accessible.name: qsTr("Day")
                                model: sheetView.days.map((d) => d.label)
                                currentIndex: 0
                                onActivated: (index) => currentIndex = index
                            }

                            C.Dropdown {
                                id: hourBox
                                Accessible.name: qsTr("Time")
                                model: sheetView.hours
                                currentIndex: 7
                                onActivated: (index) => currentIndex = index
                            }

                            C.PillButton {
                                small: true
                                iconName: "plus"
                                text: qsTr("Add")
                                onClicked: {
                                    const day = sheetView.days[dayBox.currentIndex].date;
                                    const parts = sheetView.hours[hourBox.currentIndex].split(":");
                                    const at = new Date(day.getFullYear(), day.getMonth(), day.getDate(), Number(parts[0]), Number(parts[1])).getTime();
                                    if (sheetView.planTimes.indexOf(at) < 0 && at > Date.now())
                                        sheetView.planTimes = sheetView.planTimes.concat([at]).sort((a, b) => a - b);
                                }
                            }
                        }

                        C.PillButton {
                            visible: panel.sheet === "plan"
                            Layout.topMargin: 6
                            kind: "brand"
                            iconName: "calendar"
                            enabled: sheetView.planTimes.length > 0
                            text: sheetView.planTimes.length > 0 ? qsTr("Ask: when?") : qsTr("Add a time first")
                            onClicked: {
                                panel.fr.sendPlan(panel.chat, planTitle.text.trim(), sheetView.planTimes);
                                panel.sheet = "";
                                Qt.callLater(messageList.positionViewAtEnd);
                            }
                        }
                    }
                }
            }
        }
    }
}
