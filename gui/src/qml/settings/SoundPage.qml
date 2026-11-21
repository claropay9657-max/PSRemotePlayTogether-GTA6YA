import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Sound & Wi-Fi: speakers and microphone (also the voice chat's), the game sound, the
// console microphone's speech processing, and what the app does on a weak connection.
C.SettingsPage {
    id: page
    title: qsTr("Sound & Wi-Fi")
    description: qsTr("Your speakers and microphone, the game sound, and how the app copes with a weak connection.")

    /** Side by side where there is room for two groups next to each other. */
    readonly property bool twoColumns: width >= 760
    readonly property bool hasSpeechProcessing: typeof Chiaki.settings.speechProcessing !== "undefined"

    // The devices are looked up again each time the page is shown: a headset may have come
    // or gone since. All pages are made when the window opens, the page shown is the enabled one.
    onEnabledChanged: if (enabled) Chiaki.settings.refreshAudioDevices()
    Component.onCompleted: if (enabled) Chiaki.settings.refreshAudioDevices()

    GridLayout {
        id: columns
        Layout.fillWidth: true
        columns: page.twoColumns ? 2 : 1
        columnSpacing: 20
        rowSpacing: 18

        // ------------------------------------------------ left: devices, game sound
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.preferredWidth: page.twoColumns ? (columns.width - columns.columnSpacing) / 2 : columns.width
            spacing: 18

            C.SettingsGroup {
                title: qsTr("Devices")
                note: qsTr("also for the voice chat")

                C.SettingsRow {
                    id: speakersRow
                    title: qsTr("Speakers")
                    keywords: qsTr("audio output device headphones headset auto default")
                    textMinimum: 90

                    C.Dropdown {
                        id: speakers
                        // Chiaki.settings.audioOutDevice: a name from the list, "" for the system's default
                        readonly property var devices: Chiaki.settings.availableAudioOutDevices
                        // Both as wide as the longer one, but never wider than the row
                        Layout.preferredWidth: Math.max(speakers.implicitWidth, microphone.implicitWidth)
                        Layout.maximumWidth: speakersRow.width - speakersRow.leftPad - speakersRow.rightPad
                        Accessible.name: qsTr("Speakers")
                        model: [qsTr("System default")].concat(devices)
                        currentIndex: devices.indexOf(Chiaki.settings.audioOutDevice) + 1
                        onActivated: (index) => Chiaki.settings.audioOutDevice = index > 0 ? devices[index - 1] : ""
                    }
                }

                C.SettingsRow {
                    id: microphoneRow
                    title: qsTr("Microphone")
                    keywords: qsTr("audio input device headset mic auto default")
                    textMinimum: 90

                    C.Dropdown {
                        id: microphone
                        // Chiaki.settings.audioInDevice: a name from the list, "" for the system's default
                        readonly property var devices: Chiaki.settings.availableAudioInDevices
                        Layout.preferredWidth: Math.max(speakers.implicitWidth, microphone.implicitWidth)
                        Layout.maximumWidth: microphoneRow.width - microphoneRow.leftPad - microphoneRow.rightPad
                        Accessible.name: qsTr("Microphone")
                        model: [qsTr("System default")].concat(devices)
                        currentIndex: devices.indexOf(Chiaki.settings.audioInDevice) + 1
                        onActivated: (index) => Chiaki.settings.audioInDevice = index > 0 ? devices[index - 1] : ""
                    }
                }
            }

            C.SettingsGroup {
                title: qsTr("Game sound")

                C.SettingsRow {
                    title: qsTr("Volume")
                    keywords: qsTr("audio loudness game sound")
                    textMinimum: 90

                    C.ValueSlider {
                        // Chiaki.settings.audioVolume: 0 to 128 (SDL's full volume); shown in
                        // percent, in steps of 5 so that the keys and a controller get there quickly
                        Layout.preferredWidth: 190
                        valueWidth: 48
                        Accessible.name: qsTr("Volume")
                        from: 0
                        to: 100
                        stepSize: 5
                        snapMode: QQC.Slider.SnapAlways
                        value: Math.round(Chiaki.settings.audioVolume / 128 * 100)
                        valueText: qsTr("%1 %").arg(Math.round(value))
                        onMoved: Chiaki.settings.audioVolume = Math.round(value * 128 / 100)
                    }
                }

                C.SettingsRow {
                    title: qsTr("Sound buffer")
                    subtitle: qsTr("Larger is smoother on a weak line, smaller has less delay.")
                    keywords: qsTr("audio buffer size latency delay crackling")
                    textMinimum: 180

                    C.ValueSlider {
                        // Chiaki.settings.audioBufferSize: in steps of 1920 (10 ms); 0 is the default, 50 ms
                        Layout.preferredWidth: 190
                        valueWidth: 48
                        Accessible.name: qsTr("Sound buffer")
                        from: 1
                        to: 10
                        stepSize: 1
                        snapMode: QQC.Slider.SnapAlways
                        value: Chiaki.settings.audioBufferSize / 1920 || 5
                        valueText: qsTr("%1 ms").arg(Math.round(value * 10))
                        onMoved: Chiaki.settings.audioBufferSize = Math.round(value) * 1920
                    }
                }

                C.SettingsRow {
                    title: qsTr("Console microphone on at the start")
                    subtitle: qsTr("The microphone the console hears, not the voice chat.")
                    keywords: qsTr("mic unmuted muted start")
                    textMinimum: 180
                    switchItem: startMic

                    C.Toggle {
                        id: startMic
                        Accessible.name: qsTr("Console microphone on at the start")
                        checked: Chiaki.settings.startMicUnmuted
                        onToggled: Chiaki.settings.startMicUnmuted = checked
                    }
                }
            }
        }

        // ------------------------------------------------ right: speech, connection
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.preferredWidth: page.twoColumns ? (columns.width - columns.columnSpacing) / 2 : columns.width
            spacing: 18

            // Only in builds with Speex
            C.SettingsGroup {
                visible: page.hasSpeechProcessing
                title: qsTr("Cleaner voice")

                C.SettingsRow {
                    title: qsTr("Remove noise and echo")
                    subtitle: qsTr("Noise suppression and echo cancellation for the microphone the console hears.")
                    keywords: qsTr("speech processing speex microphone")
                    textMinimum: 180
                    switchItem: speech

                    C.Toggle {
                        id: speech
                        Accessible.name: qsTr("Remove noise and echo")
                        checked: page.hasSpeechProcessing && Chiaki.settings.speechProcessing
                        onToggled: Chiaki.settings.speechProcessing = checked
                    }
                }

                C.SettingsRow {
                    sub: true
                    enabled: speech.checked
                    title: qsTr("Noise")
                    keywords: qsTr("noise to suppress suppression dB")
                    textMinimum: 90

                    C.ValueSlider {
                        Layout.preferredWidth: 190
                        valueWidth: 48
                        Accessible.name: qsTr("Noise to remove")
                        from: 0
                        to: 60
                        stepSize: 1
                        snapMode: QQC.Slider.SnapAlways
                        value: page.hasSpeechProcessing ? Chiaki.settings.noiseSuppressLevel : 0
                        valueText: qsTr("%1 dB").arg(Math.round(value))
                        onMoved: Chiaki.settings.noiseSuppressLevel = Math.round(value)
                    }
                }

                C.SettingsRow {
                    sub: true
                    enabled: speech.checked
                    title: qsTr("Echo")
                    keywords: qsTr("echo to suppress cancellation dB")
                    textMinimum: 90

                    C.ValueSlider {
                        Layout.preferredWidth: 190
                        valueWidth: 48
                        Accessible.name: qsTr("Echo to remove")
                        from: 0
                        to: 60
                        stepSize: 1
                        snapMode: QQC.Slider.SnapAlways
                        value: page.hasSpeechProcessing ? Chiaki.settings.echoSuppressLevel : 0
                        valueText: qsTr("%1 dB").arg(Math.round(value))
                        onMoved: Chiaki.settings.echoSuppressLevel = Math.round(value)
                    }
                }
            }

            C.SettingsGroup {
                title: qsTr("Weak connection")

                C.SettingsRow {
                    title: qsTr("Warn about weak Wi-Fi")
                    subtitle: qsTr("From this share of lost packets on.")
                    keywords: qsTr("wifi wlan warning notification packet loss dropped")
                    textMinimum: 180

                    C.ValueSlider {
                        // Chiaki.settings.wifiDroppedNotif: percent
                        Layout.preferredWidth: 190
                        valueWidth: 48
                        Accessible.name: qsTr("Warn about weak Wi-Fi")
                        from: 0
                        to: 100
                        stepSize: 1
                        snapMode: QQC.Slider.SnapAlways
                        value: Chiaki.settings.wifiDroppedNotif
                        valueText: qsTr("%1 %").arg(Math.round(value))
                        onMoved: Chiaki.settings.wifiDroppedNotif = Math.round(value)
                    }
                }

                C.SettingsRow {
                    title: qsTr("Loss told to the console")
                    subtitle: qsTr("At most this, so the console keeps the quality up.")
                    keywords: qsTr("packet loss reported maximum quality bitrate")
                    textMinimum: 180

                    C.ValueSlider {
                        // Chiaki.settings.packetLossReportedMax: percent
                        Layout.preferredWidth: 190
                        valueWidth: 48
                        Accessible.name: qsTr("Loss told to the console")
                        from: 0
                        to: 100
                        stepSize: 1
                        snapMode: QQC.Slider.SnapAlways
                        value: Chiaki.settings.packetLossReportedMax
                        valueText: qsTr("%1 %").arg(Math.round(value))
                        onMoved: Chiaki.settings.packetLossReportedMax = Math.round(value)
                    }
                }

                C.SettingsRow {
                    title: qsTr("Fresh picture after a failed repair")
                    subtitle: qsTr("When error correction cannot fix the picture, asks the console for a key frame.")
                    keywords: qsTr("idr fec keyframe full picture artifacts")
                    textMinimum: 180
                    switchItem: freshPicture

                    C.Toggle {
                        id: freshPicture
                        Accessible.name: qsTr("Fresh picture after a failed repair")
                        checked: Chiaki.settings.iDROnFECFailureEnabled
                        onToggled: Chiaki.settings.iDROnFECFailureEnabled = checked
                    }
                }

                C.SettingsRow {
                    title: qsTr("Stream statistics while playing")
                    keywords: qsTr("stats bitrate latency fps packet loss numbers")
                    textMinimum: 180
                    switchItem: stats

                    C.Toggle {
                        id: stats
                        Accessible.name: qsTr("Stream statistics while playing")
                        checked: Chiaki.settings.showStreamStats
                        onToggled: Chiaki.settings.showStreamStats = checked
                    }
                }
            }
        }
    }
}
