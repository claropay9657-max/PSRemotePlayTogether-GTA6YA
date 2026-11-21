import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Live streaming: going live to Twitch (or any RTMP server) straight from the app, with
// what this player sees and hears. Set up here; "Go live" and "Stop live" are in the
// stream menu. Everything lives in Chiaki.live (livecontroller.h); the stream key goes to
// the system's password store, never into the settings file.
C.SettingsPage {
    id: page
    title: qsTr("Live streaming")
    description: qsTr("Stream what you see and hear to Twitch, straight from this computer, without OBS. Host and guests each go live on their own; everybody in the session sees who is live.")

    readonly property var live: Chiaki.live
    readonly property var values: live ? live.values : ({})
    readonly property bool custom: values.service === "custom"
    readonly property var qualities: ["720p30", "720p60", "1080p30", "1080p60"]
    readonly property var qualityNames: [qsTr("720p, 30 frames a second"), qsTr("720p, 60 frames a second"),
        qsTr("1080p, 30 frames a second"), qsTr("1080p, 60 frames a second")]
    /** With OpenGL the display cannot be drawn into the stream (see StreamView's live overlay). */
    readonly property bool overlaysPossible: Chiaki.window.runtimeRendererBackend !== 1

    function set(key, value) {
        live.set(key, value);
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

    // ------------------------------------------------ where it goes

    C.SettingsGroup {
        title: qsTr("Where to")

        C.SettingsRow {
            title: qsTr("Service")
            subtitle: page.custom ? qsTr("Any service that takes RTMP or RTMPS: YouTube, Kick, your own server.")
                                  : qsTr("Twitch picks its nearest server, and the stream goes there encrypted (RTMPS).")
            keywords: qsTr("twitch youtube kick rtmp rtmps server stream platform")
            textMinimum: 200

            C.Segmented {
                model: [qsTr("Twitch"), qsTr("Other service")]
                currentIndex: page.custom ? 1 : 0
                onActivated: (index) => page.set("service", index === 1 ? "custom" : "twitch")
            }
        }

        C.SettingsRow {
            visible: page.custom
            sub: true
            title: qsTr("Server address")
            subtitle: qsTr("As your service shows it, starting with rtmp:// or rtmps://.")
            keywords: qsTr("rtmp url ingest server")
            textMinimum: 180

            C.Field {
                Layout.preferredWidth: 320
                Layout.fillWidth: true
                Accessible.name: qsTr("Server address")
                // Streamer mode: hidden unless it is being typed (some services put the key in it)
                echoMode: Chiaki.settings.streamerMode && readOnly ? TextInput.Password : TextInput.Normal
                hint: "rtmps://"
                text: page.values.server || ""
                onEditingFinished: page.set("server", text.trim())
            }
        }

        C.SettingsRow {
            id: keyRow
            title: qsTr("Stream key")
            subtitle: {
                let where = page.live.keyStore ? qsTr("Kept in %1, never in the settings file.").arg(page.live.keyStore)
                                               : qsTr("This computer has no password store the app can use: the key is kept until the app quits.");
                if (page.custom)
                    return qsTr("If your service gives you one; else leave it empty.") + " " + where;
                return qsTr("On Twitch: Creator Dashboard → Settings → Stream.") + " " + where;
            }
            keywords: qsTr("key secret password twitch dashboard")
            textMinimum: 180
            property string note: ""

            ColumnLayout {
                spacing: 6

                RowLayout {
                    spacing: 8

                    C.Badge {
                        tone: page.live.keySaved ? "good" : "wait"
                        dot: true
                        text: page.live.keySaved ? qsTr("Saved") : qsTr("Not saved yet")
                    }

                    C.TextLink {
                        visible: page.live.keySaved
                        text: qsTr("Remove")
                        onClicked: {
                            page.live.clearKey();
                            keyRow.note = "";
                        }
                    }

                    C.TextLink {
                        visible: !page.custom
                        text: qsTr("Open Twitch")
                        onClicked: Qt.openUrlExternally("https://dashboard.twitch.tv/settings/stream")
                    }
                }

                RowLayout {
                    spacing: 8

                    C.Field {
                        id: keyField
                        Layout.preferredWidth: 240
                        Accessible.name: qsTr("Stream key")
                        // Never shown, not even while typing
                        echoMode: TextInput.Password
                        hint: page.live.keySaved ? qsTr("Paste a new key to replace it") : qsTr("Paste your stream key")
                        onAccepted: saveKey.clicked()
                    }

                    C.PillButton {
                        id: saveKey
                        small: true
                        kind: "primary"
                        enabled: keyField.text.trim().length > 0
                        text: qsTr("Save")
                        onClicked: {
                            keyRow.note = page.live.saveKey(keyField.text);
                            keyField.text = "";
                        }
                    }
                }

                Text {
                    visible: keyRow.note.length > 0
                    Layout.maximumWidth: 330
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: C.Theme.wait
                    text: keyRow.note
                }
            }
        }
    }

    // ------------------------------------------------ quality

    C.SettingsGroup {
        title: qsTr("Quality")
        note: qsTr("Changes here apply the next time you go live.")

        C.SettingsRow {
            title: qsTr("Picture")
            subtitle: qsTr("720p at 30 frames a second suits most uploads; 1080p and 60 frames need more.")
            keywords: qsTr("resolution fps frame rate 720 1080 60")
            textMinimum: 200

            C.Dropdown {
                Accessible.name: qsTr("Picture")
                model: page.qualityNames
                currentIndex: Math.max(0, page.qualities.indexOf(page.values.quality || "720p30"))
                onActivated: (choice) => page.set("quality", page.qualities[choice])
            }
        }

        C.SettingsRow {
            title: qsTr("Bitrate")
            subtitle: (page.values.video_kbps || 0) > 0
                      ? qsTr("Your own choice. What Twitch recommends for this picture: %1 kbit/s.").arg(page.live.defaultKbps)
                      : qsTr("What Twitch recommends for this picture. Keep it well below your upload speed.")
            keywords: qsTr("bitrate upload kbps mbps bandwidth")
            textMinimum: 200

            RowLayout {
                spacing: 10

                C.ValueSlider {
                    Layout.preferredWidth: 190
                    valueWidth: 86
                    Accessible.name: qsTr("Bitrate")
                    from: 1000
                    to: 9000
                    stepSize: 250
                    snapMode: QQC.Slider.SnapAlways
                    value: (page.values.video_kbps || 0) > 0 ? page.values.video_kbps : page.live.defaultKbps
                    valueText: qsTr("%1 kbit/s").arg(Math.round(value))
                    onMoved: page.set("video_kbps", Math.round(value))
                }

                C.TextLink {
                    visible: (page.values.video_kbps || 0) > 0
                    text: qsTr("Default")
                    onClicked: page.set("video_kbps", 0)
                }
            }
        }
    }

    // ------------------------------------------------ what goes in

    C.SettingsGroup {
        title: qsTr("In the stream")
        note: qsTr("The stream never shows the stream menu, the chat or invites. Players who asked to be left out of streams are not heard, and appear without name, picture or webcam.")

        C.SettingsRow {
            title: qsTr("The display on the game picture")
            subtitle: page.overlaysPossible
                      ? qsTr("Players, webcams and controllers, as arranged under Overlays & OBS.")
                      : qsTr("Needs the Vulkan renderer (Picture → Rendering). With OpenGL the stream shows the game alone.")
            keywords: qsTr("overlay hud players webcams controllers")
            textMinimum: 200
            switchItem: overlaysToggle

            C.Toggle {
                id: overlaysToggle
                enabled: page.overlaysPossible
                Accessible.name: qsTr("The display on the game picture")
                checked: !!page.values.overlays && page.overlaysPossible
                onToggled: page.set("overlays", checked)
            }
        }

        C.SettingsRow {
            title: qsTr("Game sound")
            subtitle: qsTr("Whatever the volume on this computer.")
            keywords: qsTr("audio volume console game")
            textMinimum: 160

            C.ValueSlider {
                Layout.preferredWidth: 190
                valueWidth: 48
                Accessible.name: qsTr("Game sound")
                from: 0
                to: 200
                stepSize: 5
                snapMode: QQC.Slider.SnapAlways
                value: page.values.game_level === undefined ? 100 : page.values.game_level
                valueText: qsTr("%1 %").arg(Math.round(value))
                onMoved: page.set("game_level", Math.round(value))
            }
        }

        C.SettingsRow {
            title: qsTr("The others' voices")
            subtitle: qsTr("As you hear them in the voice chat.")
            keywords: qsTr("voice chat players friends audio")
            textMinimum: 160
            switchItem: voicesToggle

            C.Toggle {
                id: voicesToggle
                Accessible.name: qsTr("The others' voices")
                checked: !!page.values.voices
                onToggled: page.set("voices", checked)
            }
        }

        C.SettingsRow {
            sub: true
            enabled: voicesToggle.checked
            title: qsTr("Their volume in the stream")
            keywords: qsTr("voice loudness")
            textMinimum: 160

            C.ValueSlider {
                Layout.preferredWidth: 190
                valueWidth: 48
                Accessible.name: qsTr("Their volume in the stream")
                from: 0
                to: 200
                stepSize: 5
                snapMode: QQC.Slider.SnapAlways
                value: page.values.voices_level === undefined ? 100 : page.values.voices_level
                valueText: qsTr("%1 %").arg(Math.round(value))
                onMoved: page.set("voices_level", Math.round(value))
            }
        }

        C.SettingsRow {
            title: qsTr("My microphone")
            subtitle: qsTr("What the others hear of you. Muted is muted in the stream too.")
            keywords: qsTr("mic own voice microphone")
            textMinimum: 160
            switchItem: micToggle

            C.Toggle {
                id: micToggle
                Accessible.name: qsTr("My microphone")
                checked: !!page.values.own_mic
                onToggled: page.set("own_mic", checked)
            }
        }

        C.SettingsRow {
            sub: true
            enabled: micToggle.checked
            title: qsTr("My volume in the stream")
            keywords: qsTr("mic loudness")
            textMinimum: 160

            C.ValueSlider {
                Layout.preferredWidth: 190
                valueWidth: 48
                Accessible.name: qsTr("My volume in the stream")
                from: 0
                to: 200
                stepSize: 5
                snapMode: QQC.Slider.SnapAlways
                value: page.values.mic_level === undefined ? 100 : page.values.mic_level
                valueText: qsTr("%1 %").arg(Math.round(value))
                onMoved: page.set("mic_level", Math.round(value))
            }
        }
    }

    // ------------------------------------------------ the others' streams

    C.SettingsGroup {
        title: qsTr("When others go live")

        C.SettingsRow {
            title: qsTr("Leave me out of their streams")
            subtitle: qsTr("Your voice, webcam and name stay out of what the other players stream. Players with an older version of the app do not know this yet.")
            keywords: qsTr("privacy consent opt out stream twitch")
            textMinimum: 200
            switchItem: leaveOut

            C.Toggle {
                id: leaveOut
                Accessible.name: qsTr("Leave me out of their streams")
                checked: !!page.values.leave_me_out
                onToggled: page.set("leave_me_out", checked)
            }
        }
    }

    // ------------------------------------------------ while live

    C.SettingsGroup {
        visible: page.live.live || page.live.state === "failed"
        title: qsTr("Right now")

        C.SettingsRow {
            readonly property var stats: page.live.stats
            title: page.live.state === "live" ? qsTr("Live on %1").arg(page.custom ? page.live.server.replace(/^rtmps?:\/\//, "").split("/")[0] : "Twitch")
                 : page.live.state === "failed" ? qsTr("Not live") : qsTr("Going live…")
            subtitle: {
                if (page.live.state === "failed")
                    return stats.error || "";
                let parts = [];
                if (stats.encoder)
                    parts.push(qsTr("Encoder: %1").arg(stats.encoder));
                parts.push(qsTr("%1 kbit/s out").arg(stats.kbps || 0));
                parts.push(qsTr("%1 pictures left out").arg(stats.dropped || 0));
                if (stats.reconnects > 0)
                    parts.push(qsTr("connected again %n time(s)", "", stats.reconnects));
                return parts.join(" · ");
            }
            textMinimum: 200

            C.PillButton {
                small: true
                kind: "danger"
                visible: page.live.live
                text: qsTr("Stop live")
                onClicked: page.live.stop()
            }
        }
    }
}
