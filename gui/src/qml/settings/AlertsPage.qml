import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Alerts: what Twitch tells about the channel as a card over the stream (Chiaki.alerts),
// one after the other. Each kind has its own switch, Test, sound, volume, time and
// minimum. Shown in the app's own live stream, on this screen if wanted, and in OBS.
C.SettingsPage {
    id: page
    title: qsTr("Alerts")
    description: qsTr("A card over your stream for every follow, sub, cheer, raid, channel point reward and more, one after the other, with a sound. Log in under Twitch; Test shows each one without it, and you hear it here.")

    readonly property var alerts: Chiaki.alerts
    readonly property var values: alerts ? alerts.values : ({})
    readonly property string obsUrl: Chiaki.overlayUrl ? (Chiaki.overlayUrl.endsWith("/") ? Chiaki.overlayUrl : Chiaki.overlayUrl + "/") + "alerts" : ""

    function set(key, value) {
        alerts.set(key, value);
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

    C.SettingsGroup {
        title: qsTr("Showing them")

        C.SettingsRow {
            visible: !Chiaki.twitch || Chiaki.twitch.state !== "logged_in"
            title: qsTr("Twitch is not connected")
            subtitle: qsTr("Alerts come from your channel once you log in. Test works without it.")
            textMinimum: 200

            C.PillButton {
                small: true
                text: qsTr("Twitch")
                onClicked: page.openPage("twitch", "", false)
            }
        }

        C.SettingsRow {
            title: qsTr("On my screen too")
            subtitle: qsTr("Besides your stream, the alerts show over the game here, with their sound.")
            keywords: qsTr("local screen see alerts")
            textMinimum: 200
            switchItem: onScreen

            C.Toggle {
                id: onScreen
                Accessible.name: qsTr("On my screen too")
                checked: !!page.values.on_screen
                onToggled: page.set("on_screen", checked)
            }
        }

        C.SettingsRow {
            title: qsTr("Wait during Be right back")
            subtitle: qsTr("While Be right back or Technical difficulties is on your stream, alerts wait and come after.")
            keywords: qsTr("hold pause scenes brb")
            textMinimum: 200
            switchItem: holdScenes

            C.Toggle {
                id: holdScenes
                Accessible.name: qsTr("Wait during Be right back")
                checked: !!page.values.hold_scenes
                onToggled: page.set("hold_scenes", checked)
            }
        }

        C.SettingsRow {
            title: qsTr("Hidden words")
            subtitle: qsTr("Hidden in viewers' names and messages, separated by commas.")
            keywords: qsTr("filter blocked words bad language")
            textMinimum: 200

            C.Field {
                Layout.preferredWidth: 300
                Layout.fillWidth: true
                Accessible.name: qsTr("Hidden words")
                hint: qsTr("word, another word")
                text: page.values.blocked || ""
                onEditingFinished: page.set("blocked", text)
            }
        }

        C.SettingsRow {
            visible: page.obsUrl.length > 0
            title: qsTr("In OBS")
            subtitle: qsTr("%1 as a Browser Source of 1920 × 1080; tick “Control audio via OBS” for the sounds.").arg(page.obsUrl)
            keywords: qsTr("obs browser source address url")
            textMinimum: 200

            C.PillButton {
                small: true
                iconName: "copy"
                text: qsTr("Copy")
                onClicked: Chiaki.scenes.copy(page.obsUrl)
            }
        }
    }

    C.SettingsGroup {
        title: qsTr("Sounds")

        C.SettingsRow {
            title: qsTr("Alert sounds")
            subtitle: qsTr("One level for all of them, on top of each kind's own. Off: alerts come without a sound.")
            keywords: qsTr("volume loudness mute sound alerts")
            textMinimum: 200
            switchItem: soundOn

            C.ValueSlider {
                Layout.preferredWidth: 190
                valueWidth: 48
                enabled: soundOn.checked
                Accessible.name: qsTr("Volume of all alert sounds")
                from: 0
                to: 100
                stepSize: 5
                value: page.values.sound_volume || 0
                valueText: qsTr("%1 %").arg(Math.round(value))
                onMoved: page.set("sound_volume", Math.round(value))
                // Heard at the new level once it is let go
                onPressedChanged: if (!pressed) page.alerts.preview("follow")
            }

            C.Toggle {
                id: soundOn
                Accessible.name: qsTr("Alert sounds")
                checked: !!page.values.sound_on
                onToggled: page.set("sound_on", checked)
            }
        }

        C.SettingsRow {
            title: qsTr("Chat sound")
            subtitle: qsTr("A soft sound when somebody writes, in Twitch's chat or the room's – never for your own messages, at most one every 1.5 seconds.")
            keywords: qsTr("chat message sound ding notification volume")
            textMinimum: 200
            switchItem: chatSound

            C.ValueSlider {
                Layout.preferredWidth: 190
                valueWidth: 48
                enabled: chatSound.checked && soundOn.checked
                Accessible.name: qsTr("Volume of the chat sound")
                from: 0
                to: 100
                stepSize: 5
                value: page.values.chat_volume || 0
                valueText: qsTr("%1 %").arg(Math.round(value))
                onMoved: page.set("chat_volume", Math.round(value))
                onPressedChanged: if (!pressed) page.alerts.preview("chat")
            }

            C.Toggle {
                id: chatSound
                Accessible.name: qsTr("Chat sound")
                checked: !!page.values.chat_sound
                onToggled: page.set("chat_sound", checked)
            }
        }

        C.SettingsRow {
            sub: true
            enabled: chatSound.checked
            title: qsTr("Also on stream")
            subtitle: qsTr("Your viewers hear it too. Off: only you hear it.")
            keywords: qsTr("chat sound stream viewers")
            textMinimum: 200
            switchItem: chatStream

            C.Toggle {
                id: chatStream
                Accessible.name: qsTr("Chat sound also on stream")
                checked: !!page.values.chat_stream
                onToggled: page.set("chat_stream", checked)
            }
        }
    }

    C.SettingsGroup {
        title: qsTr("Each kind")

        Repeater {
            model: page.alerts ? page.alerts.kinds : []

            ColumnLayout {
                id: kindRows
                required property var modelData
                readonly property string id: modelData.id
                readonly property string sound: page.values[id + "_sound"] || "app"
                Layout.fillWidth: true
                spacing: 0

                C.SettingsRow {
                    title: kindRows.modelData.name
                    subtitle: kindRows.modelData.what
                    keywords: qsTr("alert") + " " + kindRows.id
                    textMinimum: 200
                    switchItem: kindToggle

                    C.PillButton {
                        small: true
                        text: qsTr("Test")
                        onClicked: page.alerts.test(kindRows.id)
                    }

                    C.Toggle {
                        id: kindToggle
                        Accessible.name: kindRows.modelData.name
                        checked: !!page.values[kindRows.id + "_on"]
                        onToggled: page.set(kindRows.id + "_on", checked)
                    }
                }

                C.SettingsRow {
                    sub: true
                    enabled: kindToggle.checked
                    title: qsTr("Sound")
                    subtitle: kindRows.sound !== "app" && kindRows.sound !== "none" ? kindRows.sound.split(/[\\/]/).pop() : ""
                    textMinimum: 120

                    C.Segmented {
                        model: [qsTr("The app's"), qsTr("None"), qsTr("Own…")]
                        currentIndex: kindRows.sound === "app" ? 0 : (kindRows.sound === "none" ? 1 : 2)
                        onActivated: (index) => {
                            if (index === 0)
                                page.alerts.setSound(kindRows.id, "app");
                            else if (index === 1)
                                page.alerts.setSound(kindRows.id, "none");
                            else
                                page.alerts.chooseSoundFile(kindRows.id);
                        }
                    }

                    C.ValueSlider {
                        Layout.preferredWidth: 150
                        valueWidth: 48
                        enabled: kindRows.sound !== "none"
                        Accessible.name: qsTr("Volume of %1").arg(kindRows.modelData.name)
                        from: 0
                        to: 100
                        stepSize: 5
                        snapMode: QQC.Slider.SnapAlways
                        value: page.values[kindRows.id + "_volume"] || 0
                        valueText: qsTr("%1 %").arg(Math.round(value))
                        onMoved: page.set(kindRows.id + "_volume", Math.round(value))
                        // Heard at the new level once it is let go
                        onPressedChanged: if (!pressed) page.alerts.preview(kindRows.id)
                    }
                }

                C.SettingsRow {
                    sub: true
                    enabled: kindToggle.checked
                    title: qsTr("On screen for")
                    textMinimum: 120

                    C.ValueSlider {
                        Layout.preferredWidth: 190
                        valueWidth: 48
                        Accessible.name: qsTr("Time on screen of %1").arg(kindRows.modelData.name)
                        from: 2
                        to: 20
                        stepSize: 1
                        snapMode: QQC.Slider.SnapAlways
                        value: page.values[kindRows.id + "_seconds"] || 6
                        valueText: qsTr("%1 s").arg(Math.round(value))
                        onMoved: page.set(kindRows.id + "_seconds", Math.round(value))
                    }
                }

                C.SettingsRow {
                    visible: !!kindRows.modelData.hasMin
                    sub: true
                    enabled: kindToggle.checked
                    title: kindRows.modelData.minName || ""
                    textMinimum: 120

                    C.Field {
                        Layout.preferredWidth: 110
                        Accessible.name: kindRows.modelData.minName || ""
                        validator: IntValidator { bottom: 1; top: 1000000 }
                        inputMethodHints: Qt.ImhDigitsOnly
                        text: page.values[kindRows.modelData.minKey] || 1
                        onEditingFinished: if (acceptableInput) page.set(kindRows.modelData.minKey, parseInt(text))
                    }
                }
            }
        }
    }
}
