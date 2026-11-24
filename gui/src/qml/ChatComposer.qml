import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import "controls" as C

// Writing a message, the same in a session's chat (RoomChat) and in the friends' chats
// (FriendsPanel): the emoji button, the field and the send button; above them the message being
// answered ("Replying to …", ✕ or Esc lets go of it) and, while a name is typed after "@", the
// people to mention (↑ ↓ choose, Return or Tab take one, Esc closes).
ColumnLayout {
    id: composer

    property string hint: qsTr("Message")
    property int maximumLength: 4000
    property string accessibleName: qsTr("Message")
    /** Whom "@" offers: each name, avatar. Empty: no list (a chat with one friend). */
    property var members: []
    /** The message being answered: name, text; null if none. */
    property var replyTo: null
    property bool inputEnabled: true

    readonly property alias text: field.text

    signal send(string text)
    signal emojiClicked()
    signal replyCancelled()

    function focusInput() {
        field.readOnly = false;
        field.forceActiveFocus();
    }

    function insert(text) {
        field.insert(field.cursorPosition, text);
        mentions.look();
    }

    function clear() {
        field.clear();
        mentions.at = -1;
    }

    /** Lets go of the @ list or the reply; false if there was neither. */
    function closeExtras() {
        if (mentions.shown) {
            mentions.at = -1;
            return true;
        }
        if (replyTo) {
            replyCancelled();
            return true;
        }
        return false;
    }

    function submit() {
        if (mentions.shown) {
            mentions.take(mentions.current);
            return;
        }
        const text = field.text.trim();
        if (!text || !inputEnabled)
            return;
        send(text);
    }

    spacing: 6

    // ---- the message being answered
    Rectangle {
        Layout.fillWidth: true
        visible: !!composer.replyTo
        implicitHeight: replyColumn.implicitHeight + 14
        radius: 10
        color: Qt.rgba(1, 1, 1, 0.05)
        border.color: C.Theme.line

        Rectangle {
            anchors {
                left: parent.left
                top: parent.top
                bottom: parent.bottom
                margins: 6
            }
            width: 3
            radius: 1.5
            color: "#b18cff"
        }

        ColumnLayout {
            id: replyColumn
            anchors {
                left: parent.left
                right: replyClose.left
                verticalCenter: parent.verticalCenter
                leftMargin: 16
                rightMargin: 6
            }
            spacing: 1

            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.pixelSize: 11
                font.bold: true
                color: "#b18cff"
                text: composer.replyTo ? qsTr("Replying to %1").arg(composer.replyTo.name || qsTr("a message")) : ""
            }

            Label {
                Layout.fillWidth: true
                textFormat: Text.PlainText
                elide: Text.ElideRight
                maximumLineCount: 1
                font.pixelSize: 12
                color: C.Theme.textSoft
                text: composer.replyTo ? (composer.replyTo.text || "") : ""
            }
        }

        C.IconButton {
            id: replyClose
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            small: true
            iconName: "close"
            tip: qsTr("Don't reply")
            onClicked: composer.replyCancelled()
        }
    }

    // ---- whom "@" offers
    Rectangle {
        id: mentions
        Layout.fillWidth: true
        visible: shown
        implicitHeight: Math.min(4, matches.length) * 40 + 8
        radius: 12
        color: "#17132d"
        border.color: Qt.rgba(1, 1, 1, 0.12)

        /** Where the "@" is in the text; -1 while no name is being typed. */
        property int at: -1
        property int current: 0
        readonly property string typed: at >= 0 ? field.text.substring(at + 1, field.cursorPosition) : ""
        readonly property var matches: {
            if (at < 0)
                return [];
            const query = typed.toLowerCase();
            const out = [];
            for (let i = 0; i < composer.members.length; ++i) {
                const name = composer.members[i].name || "";
                if (name && name.toLowerCase().startsWith(query))
                    out.push(composer.members[i]);
            }
            for (let i = 0; i < composer.members.length; ++i) {
                const name = composer.members[i].name || "";
                if (name && !name.toLowerCase().startsWith(query) && query.length > 0 && name.toLowerCase().indexOf(query) > 0)
                    out.push(composer.members[i]);
            }
            return out;
        }
        readonly property bool shown: at >= 0 && matches.length > 0
        onMatchesChanged: current = Math.min(current, Math.max(0, matches.length - 1))

        /** After each change of the text: is a name being typed after "@"? */
        function look() {
            if (composer.members.length === 0) {
                at = -1;
                return;
            }
            const text = field.text;
            const cursor = field.cursorPosition;
            const start = text.lastIndexOf("@", cursor - 1);
            // "@" at the start or after a space or punctuation, at most a name's length back, on this line
            if (start < 0 || cursor - start > 33 || text.substring(start, cursor).indexOf("\n") >= 0
                    || (start > 0 && /[\p{L}\p{N}]/u.test(text.charAt(start - 1)))) {
                at = -1;
                return;
            }
            if (start !== at)
                current = 0;
            at = start;
        }

        function take(index) {
            const person = matches[index];
            if (!person)
                return;
            const before = field.text.substring(0, at);
            const after = field.text.substring(field.cursorPosition);
            const name = "@" + person.name + " ";
            field.text = before + name + after;
            field.cursorPosition = before.length + name.length;
            at = -1;
            composer.focusInput();
        }

        ListView {
            id: mentionList
            anchors.fill: parent
            anchors.margins: 4
            clip: true
            interactive: count > 4
            model: mentions.matches
            currentIndex: mentions.current

            delegate: ItemDelegate {
                id: person
                required property var modelData
                required property int index
                width: mentionList.width
                height: 40
                highlighted: index === mentions.current
                onClicked: mentions.take(index)

                background: Rectangle {
                    radius: 8
                    color: person.highlighted || person.hovered ? Qt.rgba(140 / 255, 82 / 255, 1, 0.25) : "transparent"
                }

                contentItem: RowLayout {
                    spacing: 10

                    C.Avatar {
                        Layout.preferredWidth: 26
                        Layout.preferredHeight: 26
                        source: person.modelData.avatar || ""
                        name: person.modelData.name || "?"
                    }

                    Label {
                        Layout.fillWidth: true
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        font.pixelSize: 14
                        font.bold: true
                        color: C.Theme.text
                        text: person.modelData.name || ""
                    }
                }
            }
        }
    }

    // ---- writing
    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        ToolButton {
            id: emojiButton
            text: "😊"
            font.pixelSize: 20
            enabled: composer.inputEnabled
            Accessible.name: qsTr("Emoji")
            onClicked: composer.emojiClicked()
        }

        C.Field {
            id: field
            Layout.fillWidth: true
            hint: composer.hint
            enabled: composer.inputEnabled
            maximumLength: composer.maximumLength
            Accessible.name: composer.accessibleName
            onTextEdited: mentions.look()
            onCursorPositionChanged: {
                if (mentions.at >= 0)
                    mentions.look();
            }
            // Return sends while typing (the field's own Return only starts typing)
            Keys.onReturnPressed: (event) => {
                if (field.readOnly)
                    field.readOnly = false;
                else
                    composer.submit();
            }
            Keys.onEnterPressed: (event) => {
                if (field.readOnly)
                    field.readOnly = false;
                else
                    composer.submit();
            }
            Keys.onTabPressed: (event) => {
                if (mentions.shown)
                    mentions.take(mentions.current);
                else
                    event.accepted = false;
            }
            Keys.onUpPressed: (event) => {
                if (mentions.shown)
                    mentions.current = (mentions.current + mentions.matches.length - 1) % mentions.matches.length;
                else
                    event.accepted = false;
            }
            Keys.onDownPressed: (event) => {
                if (mentions.shown)
                    mentions.current = (mentions.current + 1) % mentions.matches.length;
                else
                    event.accepted = false;
            }
            Keys.onEscapePressed: (event) => {
                if (!composer.closeExtras())
                    event.accepted = false;
            }
        }

        C.IconButton {
            iconName: "send"
            tip: qsTr("Send")
            enabled: composer.inputEnabled && field.text.trim().length > 0
            onClicked: composer.submit()
        }
    }
}
