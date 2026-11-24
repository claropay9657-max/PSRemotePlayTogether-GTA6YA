import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import "controls" as C

// A session's text chat: the messages (ChatBubble, as the friends' chats show them), the
// reactions, whoever has read how far, replies, mentions, deleting one's own, the emoji, and
// writing (ChatComposer). StreamChatPanel puts it in a window of its own beside the game picture.
Item {
    id: panel

    /** The session (Chiaki.session in the app). */
    property var session: null
    /** Shown: what arrives counts as read. */
    property bool open: false
    signal closeRequested()

    function focusInput() {
        composer.focusInput();
    }

    function toEnd() {
        list.stickToEnd = true;
        list.positionViewAtEnd();
    }

    /** Closes an open picker, the @ list or the reply; false if there was none. */
    function closePopups() {
        if (emojiPicker.opened || reactionPicker.opened || deleteDialog.opened) {
            emojiPicker.close();
            reactionPicker.close();
            deleteDialog.close();
            return true;
        }
        return composer.closeExtras();
    }

    readonly property var messages: session ? session.coplayChat : []
    readonly property var players: session ? session.coplayPlayers : []

    readonly property color accent: "#8c52ff"
    readonly property color cyan: "#00f2ff"
    readonly property color red: "#f0466e"
    // One colour per place in the session, so that names are told apart at a glance
    readonly property var slotColors: ["#b18cff", "#00f2ff", "#34d399", "#f472b6", "#fbbf24", "#60a5fa", "#fb923c", "#a3e635", "#e879f9"]

    function playerFor(slot) {
        for (let i = 0; i < players.length; ++i) {
            if (players[i].slot === slot)
                return players[i];
        }
        return null;
    }

    /** Whom "@" offers: the other players. */
    readonly property var mentionable: {
        const out = [];
        for (let i = 0; i < players.length; ++i) {
            if (!players[i].you && players[i].name)
                out.push({ name: players[i].name, avatar: players[i].avatarUrl || "" });
        }
        return out;
    }

    function indexOfSeq(seq) {
        for (let i = 0; i < messages.length; ++i) {
            if (messages[i].seq === seq)
                return i;
        }
        return -1;
    }

    /** The message a reply answers, as its quote shows it. */
    function quoteOf(message) {
        if (!message.reply)
            return null;
        const at = indexOfSeq(message.reply);
        if (at < 0)
            return { name: "", text: qsTr("A message from earlier"), color: "", deleted: false };
        const original = messages[at];
        return {
            name: original.name || qsTr("Player %1").arg(original.slot + 1),
            text: original.deleted ? qsTr("Deleted message") : (original.text || ""),
            color: slotColors[original.slot % slotColors.length],
            deleted: !!original.deleted
        };
    }

    /** Goes to a message and lights it up. */
    function goTo(seq) {
        const at = indexOfSeq(seq);
        if (at < 0)
            return;
        list.stickToEnd = false;
        list.positionViewAtIndex(at, ListView.Center);
        Qt.callLater(() => {
            const item = list.itemAtIndex(at);
            if (item)
                item.highlight();
        });
    }

    /** The message being answered: seq, name, text; null if none. */
    property var replyTo: null

    readonly property var marks: session ? session.coplayChatMarks : ({})
    /** Whose pictures go under which message: read marks, as Instagram shows them. */
    readonly property var readMarks: {
        const out = {};
        const read = marks.read || {};
        for (const slotText in read) {
            const slot = Number(slotText);
            const player = playerFor(slot);
            if (!player || player.you)
                continue;
            for (let i = messages.length - 1; i >= 0; --i) {
                const m = messages[i];
                if (m.pending || !m.seq)
                    continue;
                if (m.slot === slot || m.seq <= read[slotText]) {
                    if (m.slot !== slot) {
                        if (!out[i])
                            out[i] = [];
                        out[i].push({ name: player.name, avatar: player.avatarUrl || "" });
                    }
                    break;
                }
            }
        }
        return out;
    }

    function reactionsOf(message) {
        const kinds = message.seq ? ((marks.reactions || {})[String(message.seq)] || []) : [];
        const out = [];
        for (let i = 0; i < kinds.length; ++i)
            out.push({ emoji: kinds[i].emoji, count: kinds[i].slots.length, mine: kinds[i].mine });
        return out;
    }

    /** The message a reaction from all emoji is for. */
    property var reactingTo: 0

    function timeText(ms) {
        const d = new Date(ms);
        return Qt.formatTime(d, "hh:mm");
    }

    function openLink(link) {
        if (/^https?:\/\//i.test(link))
            Qt.openUrlExternally(link);
    }

    function send(text) {
        if (!session || text.length === 0)
            return;
        session.coplaySendChat(text, replyTo ? replyTo.seq : 0);
        composer.clear();
        replyTo = null;
        list.stickToEnd = true;
        list.positionViewAtEnd();
    }

    onMessagesChanged: {
        if (open && session)
            session.coplayMarkChatRead();
        if (list.stickToEnd || list.count < 4)
            Qt.callLater(list.positionViewAtEnd);
    }

    Rectangle {
        anchors.fill: parent
        radius: 18
        color: Qt.rgba(13 / 255, 11 / 255, 31 / 255, 0.96)
        border.width: 1
        border.color: Qt.rgba(140 / 255, 82 / 255, 1, 0.7)
        clip: true

        Material.theme: Material.Dark
        Material.accent: panel.accent
        Material.primary: "#161230"
        Material.background: "#0d0b1f"

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ------------------------------------------------ header
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 8
                Layout.topMargin: 8
                Layout.preferredHeight: 48
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Label {
                        text: qsTr("Chat")
                        font.pixelSize: 20
                        font.bold: true
                    }

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        opacity: 0.55
                        font.pixelSize: 12
                        text: panel.players.length > 1
                              ? qsTr("%1 players in this session").arg(panel.players.length)
                              : qsTr("Everybody who joins can read along")
                    }
                }

                C.IconButton {
                    iconName: "close"
                    tip: qsTr("Close")
                    onClicked: panel.closeRequested()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Qt.rgba(1, 1, 1, 0.08)
            }

            // ------------------------------------------------ messages
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Column {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    spacing: 6
                    width: parent.width - 60

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "💬"
                        font.pixelSize: 40
                    }
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        font.pixelSize: 15
                        font.bold: true
                        text: qsTr("No messages yet")
                    }
                    Label {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        opacity: 0.6
                        font.pixelSize: 13
                        text: qsTr("Say hi! Links get a preview, and :fire: or :) turn into emoji.")
                    }
                }

                ListView {
                    id: list
                    /** At the newest message: it stays there as messages, reactions and pictures come. */
                    property bool stickToEnd: true
                    onMovementEnded: stickToEnd = atYEnd
                    onContentHeightChanged: {
                        if (stickToEnd)
                            Qt.callLater(positionViewAtEnd);
                    }
                    // Smaller while answering or choosing whom to mention: the end stays in view
                    onHeightChanged: {
                        if (stickToEnd)
                            Qt.callLater(positionViewAtEnd);
                    }
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    spacing: 2
                    model: panel.messages
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    delegate: ChatBubble {
                        id: entry
                        required property var modelData
                        required property int index
                        readonly property var previous: index > 0 ? panel.messages[index - 1] : null
                        readonly property var player: panel.playerFor(modelData.slot)
                        width: list.width - 4
                        mine: !!modelData.you
                        // A run of messages by the same player shows the name once
                        first: !(previous && previous.slot === modelData.slot && previous.name === modelData.name
                                 && modelData.time - previous.time < 120000)
                        showAuthor: true
                        name: modelData.name || qsTr("Player %1").arg(modelData.slot + 1)
                        nameColor: panel.slotColors[modelData.slot % panel.slotColors.length]
                        avatar: player ? (player.avatarUrl || "") : ""
                        host: !!modelData.host
                        html: modelData.html || ""
                        embed: modelData.embed || null
                        status: modelData.failed ? qsTr("Not sent — the host did not answer")
                                : modelData.pending ? qsTr("Sending…") : panel.timeText(modelData.time)
                        statusBad: !!modelData.failed
                        reactions: modelData.deleted ? [] : panel.reactionsOf(modelData)
                        readers: panel.readMarks[index] || []
                        deleted: !!modelData.deleted
                        mentionsMe: !!modelData.mentionsMe
                        quote: modelData.deleted ? null : panel.quoteOf(modelData)
                        // Reactions, replies and deleting need the host's number for the message
                        canReact: !!modelData.seq && !modelData.pending && !modelData.deleted
                        canReply: !!modelData.seq && !modelData.pending && !modelData.deleted
                        canDelete: !!modelData.you && !!modelData.seq && !modelData.pending && !modelData.deleted
                        onReact: (emoji) => panel.session.coplayReact(modelData.seq, emoji)
                        onReplyRequested: {
                            panel.replyTo = { seq: modelData.seq, name: modelData.you ? qsTr("yourself") : entry.name,
                                              text: modelData.text || "" };
                            composer.focusInput();
                        }
                        onDeleteRequested: {
                            const seq = modelData.seq;
                            deleteDialog.ask(() => panel.session.coplayDeleteChat(seq));
                        }
                        onQuoteClicked: panel.goTo(modelData.reply)
                        onMoreReactions: {
                            panel.reactingTo = modelData.seq;
                            reactionPicker.open();
                        }
                        onLinkActivated: (link) => panel.openLink(link)
                    }
                }
            }

            // ------------------------------------------------ writing
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Qt.rgba(1, 1, 1, 0.08)
            }

            // The same as in the friends' chats
            ChatComposer {
                id: composer
                objectName: "composer"
                Layout.fillWidth: true
                Layout.margins: 10
                hint: panel.session && panel.session.coplayActive
                      ? qsTr("Message everybody…") : qsTr("Chat works once somebody joins")
                inputEnabled: !!panel.session && panel.session.coplayActive
                maximumLength: 400
                accessibleName: qsTr("Message everybody")
                members: panel.mentionable
                replyTo: panel.replyTo
                onSend: (text) => panel.send(text)
                onEmojiClicked: emojiPicker.opened ? emojiPicker.close() : emojiPicker.open()
                onReplyCancelled: panel.replyTo = null
            }
        }

        // ------------------------------------------------ emoji
        EmojiPicker {
            id: emojiPicker
            x: 10
            y: parent.height - height - 64
            width: parent.width - 20
            height: Math.min(300, parent.height - 140)
            onPicked: (emoji) => composer.insert(emoji)
            onClosed: composer.focusInput()
        }

        // All emoji, to react to a message with
        EmojiPicker {
            id: reactionPicker
            once: true
            x: 10
            y: Math.max(60, parent.height / 2 - height / 2)
            width: parent.width - 20
            height: Math.min(300, parent.height - 140)
            onPicked: (emoji) => {
                if (panel.session && panel.reactingTo)
                    panel.session.coplayReact(panel.reactingTo, emoji);
            }
        }

        ChatDeleteDialog {
            id: deleteDialog
        }
    }
}
