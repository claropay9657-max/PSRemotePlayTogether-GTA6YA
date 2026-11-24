import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import "controls" as C

// The emoji to choose from, by groups: for writing (the session's chat, the friends' chats)
// and for reacting to a message. picked(emoji) says which one; the picker closes itself when
// it is for a reaction.
Popup {
    id: picker

    signal picked(string emoji)
    /** For a reaction: one pick closes it. */
    property bool once: false

    readonly property var groups: [
        { icon: "😀", list: "😀 😃 😄 😁 😆 😅 😂 🤣 🙂 🙃 😉 😊 😇 🥰 😍 🤩 😘 😋 😛 😜 🤪 🤗 🤭 🤫 🤔 🤐 🤨 😐 😑 😶 😏 😒 🙄 😬 😌 😔 😪 😴 😷 🤒 🥵 🥶 🥴 😵 🤯 🤠 🥳 😎 🤓 🧐 😕 😟 🙁 😮 😯 😲 😳 🥺 😦 😨 😰 😥 😢 😭 😱 😖 😣 😞 😓 😩 😫 🥱 😤 😡 😠 🤬 😈 💀 ☠️ 💩 🤡 👻 👽 🤖" },
        { icon: "👍", list: "👍 👎 👌 🤌 🤏 ✌️ 🤞 🤟 🤘 🤙 👈 👉 👆 👇 ☝️ 👋 🤚 🖐️ ✋ 👏 🙌 👐 🤲 🙏 ✍️ 💪 🦾 🫡 🤝 👀 🧠 🫶" },
        { icon: "❤️", list: "❤️ 🧡 💛 💚 💙 💜 🖤 🤍 🤎 💔 ❤️‍🔥 💕 💞 💓 💗 💖 💘 💝 💯 💢 💥 💫 💦 💨 💤 ✨ ⭐ 🌟 🔥 ⚡ 🌈" },
        { icon: "🎮", list: "🎮 🕹️ 👾 🏆 🥇 🥈 🥉 🏅 🎯 🎲 ♟️ 🧩 🎉 🎊 🎁 🎈 🍕 🍔 🍟 🌮 🍿 🍩 🍪 🍺 🍻 🥤 ☕ 🚗 🏎️ 🚀 ✈️ 🛸 ⚔️ 🛡️ 🏹 🔫 💣 🗡️ 🪓 💰 💎 🔑 🗺️" },
        { icon: "✅", list: "✅ ❌ ❓ ❗ ‼️ ⁉️ ⚠️ 🚫 ⛔ 🔴 🟠 🟡 🟢 🔵 🟣 ⚫ ⚪ ⬆️ ⬇️ ⬅️ ➡️ 🔄 ⏸️ ▶️ ⏩ ⏪ 🆗 🆒 🆕 🆙 🔝 💬 💭 🗯️ 📢 🔔 🎵 🎶 ⏰ ⌛" }
    ]
    property int group: 0

    padding: 8

    background: Rectangle {
        radius: 14
        color: "#17132d"
        border.color: Qt.rgba(1, 1, 1, 0.12)
    }

    contentItem: ColumnLayout {
        spacing: 6

        RowLayout {
            spacing: 4

            Repeater {
                model: picker.groups

                delegate: ToolButton {
                    required property var modelData
                    required property int index
                    text: modelData.icon
                    font.pixelSize: 18
                    checkable: true
                    checked: picker.group === index
                    onClicked: picker.group = index
                }
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 42)))
            cellHeight: 40
            model: picker.groups[picker.group].list.split(" ")
            ScrollBar.vertical: ScrollBar {}

            delegate: ToolButton {
                required property string modelData
                width: grid.cellWidth
                height: grid.cellHeight
                text: modelData
                font.pixelSize: 22
                onClicked: {
                    picker.picked(modelData);
                    if (picker.once)
                        picker.close();
                }
            }
        }
    }
}
