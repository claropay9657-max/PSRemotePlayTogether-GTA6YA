import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Stream scenes: Starting soon, Lobby, Be right back, Play with us, Technical difficulties
// and Thanks for watching. Chosen in the stream menu (Scenes); shown in the app's own live
// stream and, for OBS, as browser sources of the overlay server. Here: what the player
// fills in once, the texts, and the addresses for OBS. Everything lives in Chiaki.scenes.
C.SettingsPage {
    id: page
    title: qsTr("Stream scenes")
    description: qsTr("Pictures for your stream that are not the game: starting soon, a lobby with everybody's webcams, be right back, play with us, technical difficulties and thanks for watching. Pick one in the stream menu under Scenes; the app fills in the players, the game and the room.")

    readonly property var scenes: Chiaki.scenes
    readonly property var values: scenes ? scenes.values : ({})
    readonly property var urls: scenes && Chiaki.overlayUrl ? scenes.obsUrls() : ({})
    readonly property bool vulkan: Chiaki.window.runtimeRendererBackend !== 1

    function set(key, value) {
        scenes.set(key, value);
    }

    /** For a link to another page: the settings window does it (SettingsView.openPage). */
    function openPage(key, rowTitle, byKeys) {
        for (let p = page.parent; p; p = p.parent) {
            if (typeof p.openPage === "function") {
                p.openPage(key, rowTitle, byKeys);
                return;
            }
        }
    }

    /** One text of a scene: a field that saves when you leave it. */
    component TextRow: C.SettingsRow {
        id: textRow
        property string key
        textMinimum: 200

        C.Field {
            Layout.preferredWidth: 380
            Layout.fillWidth: true
            Accessible.name: textRow.title
            text: page.values[textRow.key] || ""
            onEditingFinished: page.set(textRow.key, text)
        }
    }

    // ------------------------------------------------ filled in once

    C.SettingsGroup {
        title: qsTr("Filled in once")

        C.SettingsRow {
            title: qsTr("Your channel")
            subtitle: qsTr("Shown as twitch.tv/… on Starting soon and Thanks for watching. Leave it empty to show none.")
            keywords: qsTr("twitch channel name follow")
            textMinimum: 200

            C.Field {
                Layout.preferredWidth: 260
                Accessible.name: qsTr("Your channel")
                hint: qsTr("channel name")
                text: page.values.channel || ""
                onEditingFinished: page.set("channel", text.trim())
            }
        }

        C.SettingsRow {
            title: qsTr("Next stream")
            subtitle: qsTr("On Thanks for watching, for example “Friday, 8 pm”. Leave it empty to show none.")
            keywords: qsTr("schedule next time date")
            textMinimum: 200

            C.Field {
                Layout.preferredWidth: 260
                Accessible.name: qsTr("Next stream")
                hint: qsTr("Friday, 8 pm")
                text: page.values.next_stream || ""
                onEditingFinished: page.set("next_stream", text.trim())
            }
        }

        C.SettingsRow {
            title: qsTr("Countdown")
            subtitle: qsTr("Starting soon counts down from here when you pick it.")
            keywords: qsTr("timer minutes starting soon")
            textMinimum: 200

            C.ValueSlider {
                Layout.preferredWidth: 190
                valueWidth: 64
                Accessible.name: qsTr("Countdown")
                from: 1
                to: 30
                stepSize: 1
                snapMode: QQC.Slider.SnapAlways
                value: page.values.countdown_minutes || 5
                valueText: qsTr("%1 min").arg(Math.round(value))
                onMoved: page.set("countdown_minutes", Math.round(value))
            }
        }

        C.SettingsRow {
            title: qsTr("Break")
            subtitle: qsTr("Be right back says “back in about … min” from here.")
            keywords: qsTr("pause minutes be right back brb")
            textMinimum: 200

            C.ValueSlider {
                Layout.preferredWidth: 190
                valueWidth: 64
                Accessible.name: qsTr("Break")
                from: 1
                to: 30
                stepSize: 1
                snapMode: QQC.Slider.SnapAlways
                value: page.values.break_minutes || 5
                valueText: qsTr("%1 min").arg(Math.round(value))
                onMoved: page.set("break_minutes", Math.round(value))
            }
        }
    }

    // ------------------------------------------------ showing them

    C.SettingsGroup {
        title: qsTr("While you stream")
        note: page.vulkan ? "" : qsTr("Scenes in the app's own stream need the Vulkan renderer (Picture → Rendering). In OBS they work either way.")

        C.SettingsRow {
            title: qsTr("Technical difficulties by itself")
            subtitle: qsTr("While the app gets the console back after a lost connection, the stream shows Technical difficulties instead of a frozen picture.")
            keywords: qsTr("reconnect connection lost automatic")
            textMinimum: 200
            switchItem: autoToggle

            C.Toggle {
                id: autoToggle
                Accessible.name: qsTr("Technical difficulties by itself")
                checked: !!page.values.auto_difficulties
                onToggled: page.set("auto_difficulties", checked)
            }
        }
    }

    // ------------------------------------------------ music

    C.SettingsGroup {
        title: qsTr("Music")
        note: qsTr("Plays in your stream and in OBS while the scene is on, fading in and out.")

        Repeater {
            model: [
                { id: "starting", name: qsTr("Starting soon") },
                { id: "brb", name: qsTr("Be right back") },
                { id: "ending", name: qsTr("Thanks for watching") },
            ]

            ColumnLayout {
                id: musicRows
                required property var modelData
                required property int index
                readonly property string id: modelData.id
                readonly property bool own: page.values["music_" + id + "_source"] === "own"
                readonly property var files: page.values["music_" + id + "_files"] || []
                readonly property string note: page.scenes && page.scenes.musicNotes ? (page.scenes.musicNotes[id] || "") : ""
                Layout.fillWidth: true
                spacing: 0

                // Counts as a row above this one: the line between two scenes stays
                Item {
                    readonly property bool isSettingRow: true
                    visible: musicRows.index > 0
                    Layout.preferredHeight: 0
                }

                C.SettingsRow {
                    title: musicRows.modelData.name
                    // Said once when something chosen could not be played
                    subtitle: musicRows.note
                    keywords: qsTr("music background loop song sound") + " " + musicRows.id
                    textMinimum: 180
                    switchItem: musicOn

                    C.ValueSlider {
                        Layout.preferredWidth: 170
                        valueWidth: 48
                        enabled: musicOn.checked
                        Accessible.name: qsTr("Volume of the music of %1").arg(musicRows.modelData.name)
                        from: 0
                        to: 100
                        stepSize: 5
                        value: page.values["music_" + musicRows.id + "_volume"] || 0
                        valueText: qsTr("%1 %").arg(Math.round(value))
                        onMoved: page.set("music_" + musicRows.id + "_volume", Math.round(value))
                    }

                    C.Toggle {
                        id: musicOn
                        Accessible.name: qsTr("Music on %1").arg(musicRows.modelData.name)
                        checked: !!page.values["music_" + musicRows.id + "_on"]
                        onToggled: page.set("music_" + musicRows.id + "_on", checked)
                    }
                }

                C.SettingsRow {
                    sub: true
                    enabled: musicOn.checked
                    title: qsTr("Music")
                    subtitle: musicRows.own ? qsTr("Pick one or more files; they play in order and start over.")
                                            : qsTr("Our own music, safe to stream.")
                    textMinimum: 150

                    C.Segmented {
                        model: [qsTr("Built-in track"), qsTr("Your own music")]
                        currentIndex: musicRows.own ? 1 : 0
                        onActivated: (index) => {
                            if (index === 1 && musicRows.files.length === 0)
                                page.scenes.chooseMusicFiles(musicRows.id);
                            else
                                page.set("music_" + musicRows.id + "_source", index === 1 ? "own" : "builtin");
                        }
                    }
                }

                // The player's own files: each can be removed
                Repeater {
                    model: musicRows.own ? musicRows.files : []

                    C.SettingsRow {
                        required property string modelData
                        required property int index
                        sub: true
                        enabled: musicOn.checked
                        title: modelData.split(/[\\/]/).pop()
                        textMinimum: 150

                        C.PillButton {
                            small: true
                            iconName: "trash"
                            text: qsTr("Remove")
                            // The row goes away with it: the focus must not, or the page jumps to the top
                            focusPolicy: Qt.NoFocus
                            onClicked: page.scenes.removeMusicFile(musicRows.id, index)
                        }
                    }
                }

                C.SettingsRow {
                    visible: musicRows.own
                    sub: true
                    enabled: musicOn.checked
                    title: qsTr("Shuffle")
                    subtitle: qsTr("Only use music you are allowed to stream – Twitch may mute or flag copyrighted songs.")
                    textMinimum: 150
                    switchItem: shuffleToggle

                    C.PillButton {
                        small: true
                        iconName: "plus"
                        text: qsTr("Choose files…")
                        onClicked: page.scenes.chooseMusicFiles(musicRows.id)
                    }

                    C.Toggle {
                        id: shuffleToggle
                        Accessible.name: qsTr("Shuffle the music of %1").arg(musicRows.modelData.name)
                        checked: !!page.values["music_" + musicRows.id + "_shuffle"]
                        onToggled: page.set("music_" + musicRows.id + "_shuffle", checked)
                    }
                }
            }
        }
    }

    // ------------------------------------------------ OBS

    C.SettingsGroup {
        title: qsTr("In OBS")
        note: Chiaki.overlayUrl ? qsTr("Add an address as a Browser Source of 1920 × 1080. “Follows the stream menu” shows whichever scene you pick in the app, and nothing while the game is on.")
                                : ""

        C.SettingsRow {
            visible: !Chiaki.overlayUrl
            title: qsTr("The OBS overlay is off")
            subtitle: qsTr("The scenes come from the same small server on this computer as the OBS overlay.")
            textMinimum: 200

            C.PillButton {
                small: true
                text: qsTr("Overlays & OBS")
                onClicked: page.openPage("overlays", "", false)
            }
        }

        Repeater {
            model: Chiaki.overlayUrl && page.scenes ? page.scenes.scenes : []

            C.SettingsRow {
                required property var modelData
                title: modelData.id === "game" ? qsTr("Follows the stream menu") : modelData.name
                subtitle: page.urls[modelData.id] || ""
                keywords: qsTr("obs browser source address url")
                textMinimum: 200

                C.PillButton {
                    small: true
                    iconName: "copy"
                    text: qsTr("Copy")
                    onClicked: page.scenes.copy(page.urls[modelData.id] || "")
                }
            }
        }
    }

    // ------------------------------------------------ the texts

    C.SettingsGroup {
        title: qsTr("Texts")
        note: qsTr("{game}, {host}, {me}, {players} and {channel} fill themselves in. A line whose name is not known is left out.")

        TextRow { key: "starting_title"; title: qsTr("Starting soon"); keywords: qsTr("title") }
        TextRow { key: "starting_line"; title: qsTr("Starting soon, below"); sub: true }
        TextRow { key: "lobby_title"; title: qsTr("Lobby"); keywords: qsTr("title just chatting") }
        TextRow { key: "lobby_line"; title: qsTr("Lobby, below"); sub: true }
        TextRow { key: "brb_title"; title: qsTr("Be right back"); keywords: qsTr("title brb") }
        TextRow { key: "brb_line"; title: qsTr("Be right back, below"); sub: true }
        TextRow { key: "play_title"; title: qsTr("Play with us"); keywords: qsTr("title") }
        TextRow { key: "play_line"; title: qsTr("Play with us, below"); sub: true }
        TextRow { key: "difficulties_title"; title: qsTr("Technical difficulties"); keywords: qsTr("title hang tight") }
        TextRow { key: "difficulties_line"; title: qsTr("Technical difficulties, below"); sub: true }
        TextRow { key: "difficulties_footer"; title: qsTr("Technical difficulties, at the bottom"); sub: true }
        TextRow { key: "ending_title"; title: qsTr("Thanks for watching"); keywords: qsTr("title ending") }
    }
}
