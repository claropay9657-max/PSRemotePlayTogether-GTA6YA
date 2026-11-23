import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Window

import org.streetpea.chiaking

import "controls" as C

// Play Together text chat, beside the game picture. A window of its own like the
// overlay options, so that it shows with every renderer and takes the keyboard
// while it is open; the game gets it back when it closes.
Window {
    id: panel

    /** The stream view: gives the place of the game picture on screen. */
    required property Item view
    property bool open: false

    readonly property var session: Chiaki.session
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

    function timeText(ms) {
        const d = new Date(ms);
        return Qt.formatTime(d, "hh:mm");
    }

    function openLink(link) {
        if (/^https?:\/\//i.test(link))
            Qt.openUrlExternally(link);
    }

    function send() {
        if (!session || input.text.trim().length === 0)
            return;
        session.coplaySendChat(input.text);
        input.clear();
        list.positionViewAtEnd();
    }

    transientParent: view.hostWindow
    flags: Qt.Tool | Qt.FramelessWindowHint
    color: "transparent"
    width: Math.min(400, Math.max(300, view.width - 40))
    height: Math.max(320, view.height - 80)
    x: view.streamGlobalX + view.width - width - 20
    y: view.streamGlobalY + 40
    visible: open && !!session

    onOpenChanged: {
        if (open) {
            raise();
            requestActivate();
            input.forceActiveFocus();
            if (session)
                session.coplayMarkChatRead();
            list.positionViewAtEnd();
        } else if (view.hostWindow) {
            view.hostWindow.requestActivate();
        }
    }

    onMessagesChanged: {
        if (open && session)
            session.coplayMarkChatRead();
        if (list.atYEnd || list.count < 4)
            Qt.callLater(list.positionViewAtEnd);
    }

    Shortcut {
        sequence: StandardKey.Cancel
        onActivated: {
            if (emojiPicker.opened)
                emojiPicker.close();
            else
                panel.open = false;
        }
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

                ToolButton {
                    text: "×"
                    font.pixelSize: 26
                    onClicked: panel.open = false
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
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    spacing: 2
                    model: panel.messages
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    delegate: Item {
                        id: entry
                        required property var modelData
                        required property int index
                        readonly property var previous: index > 0 ? panel.messages[index - 1] : null
                        // A run of messages by the same player shows the name once
                        readonly property bool continued: !!previous && previous.slot === modelData.slot
                                                         && previous.name === modelData.name
                                                         && modelData.time - previous.time < 120000
                        readonly property var player: panel.playerFor(modelData.slot)
                        readonly property var embed: modelData.embed || null
                        width: list.width - 8
                        height: row.implicitHeight + (continued ? 4 : 12)

                        Rectangle {
                            anchors.fill: parent
                            radius: 10
                            color: hover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                        }

                        HoverHandler { id: hover }

                        RowLayout {
                            id: row
                            anchors {
                                left: parent.left
                                right: parent.right
                                top: parent.top
                                topMargin: entry.continued ? 2 : 8
                                leftMargin: 8
                                rightMargin: 8
                            }
                            spacing: 10

                            Item {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 34
                                Layout.preferredHeight: entry.continued ? 1 : 34

                                C.Avatar {
                                    visible: !entry.continued
                                    anchors.fill: parent
                                    source: entry.player ? (entry.player.avatarUrl || "") : ""
                                    name: entry.modelData.name || "?"
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3

                                RowLayout {
                                    visible: !entry.continued
                                    spacing: 6

                                    Label {
                                        text: entry.modelData.name || qsTr("Player %1").arg(entry.modelData.slot + 1)
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: panel.slotColors[entry.modelData.slot % panel.slotColors.length]
                                    }

                                    Rectangle {
                                        visible: entry.modelData.host
                                        radius: 7
                                        implicitWidth: hostTag.implicitWidth + 10
                                        implicitHeight: 14
                                        color: Qt.rgba(140 / 255, 82 / 255, 1, 0.8)
                                        Label {
                                            id: hostTag
                                            anchors.centerIn: parent
                                            font.pixelSize: 8
                                            font.bold: true
                                            text: qsTr("HOST")
                                        }
                                    }

                                    Label {
                                        opacity: 0.45
                                        font.pixelSize: 11
                                        text: panel.timeText(entry.modelData.time)
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: (entry.modelData.text || "").length > 0
                                    wrapMode: Text.Wrap
                                    textFormat: Text.StyledText
                                    color: "white"
                                    opacity: entry.modelData.pending ? 0.55 : 1
                                    linkColor: panel.cyan
                                    font.pixelSize: 14
                                    text: entry.modelData.html || ""
                                    onLinkActivated: (link) => panel.openLink(link)

                                    HoverHandler {
                                        cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.IBeamCursor
                                    }
                                }

                                Label {
                                    visible: entry.modelData.pending || entry.modelData.failed
                                    font.pixelSize: 11
                                    color: entry.modelData.failed ? panel.red : Qt.rgba(1, 1, 1, 0.5)
                                    text: entry.modelData.failed ? qsTr("Not sent — the host did not answer")
                                                                 : qsTr("Sending…")
                                }

                                // The link preview the writer's app made
                                Rectangle {
                                    id: card
                                    visible: !!entry.embed
                                    Layout.fillWidth: true
                                    Layout.maximumWidth: 300
                                    Layout.topMargin: 2
                                    implicitHeight: cardColumn.implicitHeight + 20
                                    radius: 10
                                    color: Qt.rgba(1, 1, 1, cardHover.hovered ? 0.08 : 0.05)
                                    border.color: Qt.rgba(1, 1, 1, 0.08)

                                    Rectangle {
                                        anchors {
                                            left: parent.left
                                            top: parent.top
                                            bottom: parent.bottom
                                            topMargin: 8
                                            bottomMargin: 8
                                        }
                                        width: 3
                                        radius: 1.5
                                        color: panel.accent
                                    }

                                    HoverHandler {
                                        id: cardHover
                                        cursorShape: entry.embed && entry.embed.url ? Qt.PointingHandCursor : Qt.ArrowCursor
                                    }
                                    TapHandler {
                                        onTapped: {
                                            if (entry.embed && entry.embed.url)
                                                panel.openLink(entry.embed.url);
                                        }
                                    }

                                    ColumnLayout {
                                        id: cardColumn
                                        anchors {
                                            left: parent.left
                                            right: parent.right
                                            top: parent.top
                                            margins: 10
                                            leftMargin: 14
                                        }
                                        spacing: 4

                                        Label {
                                            Layout.fillWidth: true
                                            visible: text.length > 0
                                            elide: Text.ElideRight
                                            opacity: 0.6
                                            font.pixelSize: 11
                                            text: entry.embed ? (entry.embed.site || "") : ""
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            visible: text.length > 0
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 2
                                            elide: Text.ElideRight
                                            color: panel.cyan
                                            font.pixelSize: 14
                                            font.bold: true
                                            text: entry.embed ? (entry.embed.title || "") : ""
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            visible: text.length > 0
                                            wrapMode: Text.Wrap
                                            maximumLineCount: 3
                                            elide: Text.ElideRight
                                            opacity: 0.75
                                            font.pixelSize: 12
                                            text: entry.embed ? (entry.embed.description || "") : ""
                                        }
                                        Image {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: visible ? Math.round(width * Math.max(0.3, Math.min(1.2, sourceSize.height / Math.max(1, sourceSize.width)))) : 0
                                            visible: status === Image.Ready
                                            fillMode: Image.PreserveAspectFit
                                            source: entry.embed ? (entry.embed.image || "") : ""
                                        }
                                        Rectangle {
                                            // The picture is still on its way
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 60
                                            visible: !!entry.embed && !!entry.embed.imageKey && !entry.embed.image
                                            radius: 6
                                            color: Qt.rgba(1, 1, 1, 0.05)
                                            BusyIndicator {
                                                anchors.centerIn: parent
                                                width: 26
                                                height: 26
                                                running: parent.visible
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------ writing
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Qt.rgba(1, 1, 1, 0.08)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 10
                spacing: 6

                ToolButton {
                    id: emojiButton
                    text: "😊"
                    font.pixelSize: 20
                    onClicked: emojiPicker.opened ? emojiPicker.close() : emojiPicker.open()
                }

                TextField {
                    id: input
                    Layout.fillWidth: true
                    placeholderText: panel.session && panel.session.coplayActive
                                     ? qsTr("Message everybody…") : qsTr("Chat works once somebody joins")
                    enabled: !!panel.session && panel.session.coplayActive
                    maximumLength: 400
                    font.pixelSize: 14
                    selectByMouse: true
                    onAccepted: panel.send()
                }

                RoundButton {
                    id: sendButton
                    enabled: input.text.trim().length > 0
                    text: "➤"
                    font.pixelSize: 16
                    Material.background: enabled ? panel.accent : Qt.rgba(1, 1, 1, 0.08)
                    onClicked: panel.send()
                }
            }
        }

        // ------------------------------------------------ emoji
        Popup {
            id: emojiPicker
            x: 10
            y: parent.height - height - 64
            width: parent.width - 20
            height: Math.min(300, parent.height - 140)
            padding: 8
            onClosed: input.forceActiveFocus()

            readonly property var groups: [
                { icon: "😀", list: "😀 😃 😄 😁 😆 😅 😂 🤣 🙂 🙃 😉 😊 😇 🥰 😍 🤩 😘 😋 😛 😜 🤪 🤗 🤭 🤫 🤔 🤐 🤨 😐 😑 😶 😏 😒 🙄 😬 😌 😔 😪 😴 😷 🤒 🥵 🥶 🥴 😵 🤯 🤠 🥳 😎 🤓 🧐 😕 😟 🙁 😮 😯 😲 😳 🥺 😦 😨 😰 😥 😢 😭 😱 😖 😣 😞 😓 😩 😫 🥱 😤 😡 😠 🤬 😈 💀 ☠️ 💩 🤡 👻 👽 🤖" },
                { icon: "👍", list: "👍 👎 👌 🤌 🤏 ✌️ 🤞 🤟 🤘 🤙 👈 👉 👆 👇 ☝️ 👋 🤚 🖐️ ✋ 👏 🙌 👐 🤲 🙏 ✍️ 💪 🦾 🫡 🤝 👀 🧠 🫶" },
                { icon: "❤️", list: "❤️ 🧡 💛 💚 💙 💜 🖤 🤍 🤎 💔 ❤️‍🔥 💕 💞 💓 💗 💖 💘 💝 💯 💢 💥 💫 💦 💨 💤 ✨ ⭐ 🌟 🔥 ⚡ 🌈" },
                { icon: "🎮", list: "🎮 🕹️ 👾 🏆 🥇 🥈 🥉 🏅 🎯 🎲 ♟️ 🧩 🎉 🎊 🎁 🎈 🍕 🍔 🍟 🌮 🍿 🍩 🍪 🍺 🍻 🥤 ☕ 🚗 🏎️ 🚀 ✈️ 🛸 ⚔️ 🛡️ 🏹 🔫 💣 🗡️ 🪓 💰 💎 🔑 🗺️" },
                { icon: "✅", list: "✅ ❌ ❓ ❗ ‼️ ⁉️ ⚠️ 🚫 ⛔ 🔴 🟠 🟡 🟢 🔵 🟣 ⚫ ⚪ ⬆️ ⬇️ ⬅️ ➡️ 🔄 ⏸️ ▶️ ⏩ ⏪ 🆗 🆒 🆕 🆙 🔝 💬 💭 🗯️ 📢 🔔 🎵 🎶 ⏰ ⌛" }
            ]
            property int group: 0

            background: Rectangle {
                radius: 14
                color: "#17132d"
                border.color: Qt.rgba(1, 1, 1, 0.12)
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 6

                RowLayout {
                    spacing: 4
                    Repeater {
                        model: emojiPicker.groups
                        delegate: ToolButton {
                            required property var modelData
                            required property int index
                            text: modelData.icon
                            font.pixelSize: 18
                            checkable: true
                            checked: emojiPicker.group === index
                            onClicked: emojiPicker.group = index
                        }
                    }
                }

                GridView {
                    id: emojiGrid
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 42)))
                    cellHeight: 40
                    model: emojiPicker.groups[emojiPicker.group].list.split(" ")
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ToolButton {
                        required property string modelData
                        width: emojiGrid.cellWidth
                        height: emojiGrid.cellHeight
                        text: modelData
                        font.pixelSize: 22
                        onClicked: {
                            input.insert(input.cursorPosition, modelData);
                        }
                    }
                }
            }
        }
    }
}
