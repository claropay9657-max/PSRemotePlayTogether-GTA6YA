import QtQuick
import QtQuick.Layouts
import QtQuick.Effects
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Voice & webcam: talking to and seeing the other players of a shared session, and its
// chat. Microphone and speakers are chosen on Sound & Wi-Fi; this page only shows them.
C.SettingsPage {
    id: page
    title: qsTr("Voice & webcam")
    description: qsTr("Everybody in a shared session can talk to and see each other, whether you host or join.")

    /** Side by side where there is room for two groups next to each other. */
    readonly property bool twoColumns: width >= 760
    /** The cameras to choose from (Chiaki.settings.coplayCameraDevices()), looked up when the page is shown. */
    property var cameras: []
    /** What each Chiaki.settings.coplayCameraQuality sends. */
    readonly property var cameraSizes: [qsTr("320 × 180, about 0.4 Mbit/s"),
        qsTr("480 × 270, about 1 Mbit/s"), qsTr("640 × 360, about 2.5 Mbit/s")]

    // All pages are made when the window opens; the page shown is the enabled one.
    // Cameras and sound devices are looked up only once somebody looks.
    function refresh() {
        cameras = Chiaki.settings.coplayCameraDevices();
        Chiaki.settings.refreshAudioDevices();
    }
    onEnabledChanged: {
        if (enabled)
            refresh();
        else
            Chiaki.deviceCheck.stopAll();
    }
    Component.onCompleted: if (enabled) refresh()
    Component.onDestruction: Chiaki.deviceCheck.stopAll()

    /** While playing, the session has the microphone, the speakers and the webcam. */
    readonly property bool playing: !!Chiaki.session

    /** The sound device that is used: the chosen one while it is there, else the system's default. */
    function deviceName(chosen, available) {
        return chosen && (available.length === 0 || available.indexOf(chosen) >= 0) ? chosen : qsTr("System default");
    }

    /**
     * Shows another page of the settings window (`key` as in SettingsView's pages) and lights up
     * the row called `rowTitle` there; `byKeys` moves the focus along, for keys and controller.
     */
    function openPage(key, rowTitle, byKeys) {
        for (let view = page.parent; view; view = view.parent) {
            if (typeof view.openPage === "function") {
                view.openPage(key, rowTitle, byKeys);
                return;
            }
        }
    }

    GridLayout {
        id: columns
        Layout.fillWidth: true
        columns: page.twoColumns ? 2 : 1
        columnSpacing: 20
        rowSpacing: 18

        // ------------------------------------------------ left: voice, chat
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.preferredWidth: page.twoColumns ? (columns.width - columns.columnSpacing) / 2 : columns.width
            spacing: 18

            C.SettingsGroup {
                title: qsTr("Voice chat")

                C.SettingsRow {
                    title: qsTr("Take part in the voice chat")
                    keywords: qsTr("voice talk speak microphone play together")
                    textMinimum: 180
                    switchItem: voice

                    C.Toggle {
                        id: voice
                        Accessible.name: qsTr("Take part in the voice chat")
                        checked: Chiaki.settings.coplayVoiceEnabled
                        onToggled: Chiaki.settings.coplayVoiceEnabled = checked
                    }
                }

                C.SettingsRow {
                    enabled: voice.checked
                    title: qsTr("Start with my microphone muted")
                    subtitle: qsTr("While you play, the stream menu mutes and unmutes you.")
                    keywords: qsTr("mute mic voice")
                    textMinimum: 180
                    switchItem: startMuted

                    C.Toggle {
                        id: startMuted
                        Accessible.name: qsTr("Start with my microphone muted")
                        checked: Chiaki.settings.coplayVoiceStartMuted
                        onToggled: Chiaki.settings.coplayVoiceStartMuted = checked
                    }
                }

                C.SettingsRow {
                    enabled: voice.checked
                    title: qsTr("Volume of the others")
                    keywords: qsTr("voice loudness players friends")
                    textMinimum: 140

                    C.ValueSlider {
                        // Chiaki.settings.coplayVoiceVolume: percent, 0 to 200
                        Layout.preferredWidth: 190
                        valueWidth: 48
                        Accessible.name: qsTr("Volume of the others")
                        from: 0
                        to: 200
                        stepSize: 5
                        snapMode: QQC.Slider.SnapAlways
                        value: Chiaki.settings.coplayVoiceVolume
                        valueText: qsTr("%1 %").arg(Math.round(value))
                        onMoved: Chiaki.settings.coplayVoiceVolume = Math.round(value)
                    }
                }

                // The microphone and speakers in use: chosen on Sound & Wi-Fi, not here
                Item {
                    id: devices
                    // Found by the search and scrolled to like a row (see SettingsRow)
                    readonly property bool isSettingRow: true
                    readonly property string title: qsTr("Microphone and speakers")
                    readonly property string subtitle: qsTr("The ones chosen under Sound & Wi-Fi.")
                    readonly property string keywords: qsTr("audio devices headset headphones input output")

                    Layout.fillWidth: true
                    implicitHeight: deviceLines.implicitHeight + 24

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Qt.rgba(1, 1, 1, 0.06)
                    }

                    ColumnLayout {
                        id: deviceLines
                        x: 18
                        y: 12
                        width: parent.width - 36
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumHeight: soundLink.implicitHeight
                            spacing: 10

                            C.Icon {
                                iconName: "mic"
                                size: 16
                                color: C.Theme.textSoft
                            }

                            Text {
                                Layout.fillWidth: true
                                text: page.deviceName(Chiaki.settings.audioInDevice, Chiaki.settings.availableAudioInDevices)
                                elide: Text.ElideRight
                                color: "#e7e4fa"
                                font.pixelSize: 14
                                Accessible.name: qsTr("Microphone: %1").arg(text)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            C.Icon {
                                iconName: "speaker"
                                size: 16
                                color: C.Theme.textSoft
                            }

                            Text {
                                Layout.fillWidth: true
                                text: page.deviceName(Chiaki.settings.audioOutDevice, Chiaki.settings.availableAudioOutDevices)
                                elide: Text.ElideRight
                                color: "#e7e4fa"
                                font.pixelSize: 14
                                Accessible.name: qsTr("Speakers: %1").arg(text)
                            }

                            C.TextLink {
                                id: soundLink
                                text: qsTr("Change in Sound & Wi-Fi")
                                onClicked: page.openPage("sound", qsTr("Speakers"), soundLink.visualFocus)
                            }
                        }
                    }
                }

                // How loud the microphone hears you, with the mark from which the others hear you
                C.SettingsRow {
                    id: micTest
                    readonly property var check: Chiaki.deviceCheck
                    title: qsTr("Microphone test")
                    subtitle: page.playing ? qsTr("While you play, the stream menu shows how loud you are.")
                              : check.micError.length > 0 ? qsTr("The microphone could not be opened: %1").arg(check.micError)
                              : check.micTesting ? (check.micLevel >= check.micGate
                                                    ? qsTr("Your friends hear this. Talk as you would while playing.")
                                                    : qsTr("Talk: from the mark on, your friends hear you."))
                              : qsTr("See how loud your microphone hears you. Nothing is sent.")
                    keywords: qsTr("microphone mic test level meter loud quiet check")

                    // The level, 0..100 as in the stream menu, and the voice chat's mark
                    Item {
                        visible: micTest.check.micTesting
                        implicitWidth: 132
                        implicitHeight: 10
                        Accessible.role: Accessible.ProgressBar
                        Accessible.name: qsTr("Microphone level")

                        Rectangle {
                            anchors.fill: parent
                            radius: 5
                            color: Qt.rgba(1, 1, 1, 0.08)
                        }

                        Rectangle {
                            height: parent.height
                            width: Math.max(height, parent.width * micTest.check.micLevel / 100)
                            visible: micTest.check.micLevel > 0
                            radius: 5
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0; color: "#8c52ff" }
                                GradientStop { position: 1; color: micTest.check.micLevel >= micTest.check.micGate ? "#4fe0b0" : "#b48cff" }
                            }
                            Behavior on width { NumberAnimation { duration: 60 } }
                        }

                        Rectangle {
                            x: Math.round(parent.width * micTest.check.micGate / 100) - 1
                            y: -3
                            width: 2
                            height: parent.height + 6
                            radius: 1
                            color: "#e7e4fa"
                            opacity: 0.8
                        }
                    }

                    C.PillButton {
                        small: true
                        enabled: !page.playing
                        iconName: micTest.check.micTesting ? "" : "mic"
                        text: micTest.check.micTesting ? qsTr("Stop") : qsTr("Test")
                        Accessible.name: micTest.check.micTesting ? qsTr("Stop the microphone test") : qsTr("Test the microphone")
                        onClicked: micTest.check.micTesting ? micTest.check.stopMic() : micTest.check.startMic()
                    }
                }

                // A chime on the left, then on the right, then on both
                C.SettingsRow {
                    id: speakerTest
                    readonly property var check: Chiaki.deviceCheck
                    readonly property var sides: ["", qsTr("Playing on the left…"), qsTr("Playing on the right…"), qsTr("Playing on both…")]
                    title: qsTr("Speaker test")
                    subtitle: page.playing ? qsTr("While you play, the game and your friends are the test.")
                              : check.speakerError.length > 0 ? qsTr("The speakers could not be opened: %1").arg(check.speakerError)
                              : check.speakerTesting ? sides[check.speakerSide] || ""
                              : qsTr("A chime on the left, then on the right, then on both.")
                    keywords: qsTr("speakers headphones sound test left right stereo chime check")

                    C.PillButton {
                        small: true
                        enabled: !page.playing && !speakerTest.check.speakerTesting
                        iconName: "speaker"
                        text: qsTr("Play")
                        Accessible.name: qsTr("Play a test chime on the speakers")
                        onClicked: speakerTest.check.playTestSound()
                    }
                }

                // A tip at the foot of the card
                Item {
                    Layout.fillWidth: true
                    implicitHeight: headphonesTip.implicitHeight + 22

                    // The card's colour with a bluish tint, opaque: rounded at the bottom
                    // like the card, square at the top
                    Rectangle {
                        anchors {
                            fill: parent
                            leftMargin: 1
                            rightMargin: 1
                            bottomMargin: 1
                        }
                        radius: 17
                        color: "#1d1b3c"
                    }

                    Rectangle {
                        x: 1
                        width: parent.width - 2
                        height: Math.min(parent.height, 20)
                        color: "#1d1b3c"
                    }

                    C.Icon {
                        x: 16
                        y: 12
                        iconName: "info"
                        size: 15
                        color: C.Theme.headingSoft
                    }

                    Text {
                        id: headphonesTip
                        x: 39
                        y: 10
                        width: parent.width - x - 16
                        text: qsTr("Use headphones. If a friend hears themselves, their voice is coming out of your speakers into your microphone.")
                        wrapMode: Text.WordWrap
                        lineHeight: 1.2
                        color: "#c9c5e6"
                        font.pixelSize: 13
                    }
                }
            }

            C.SettingsGroup {
                title: qsTr("Chat")

                C.SettingsRow {
                    title: qsTr("Preview links I post")
                    subtitle: qsTr("Your app opens the link once to make the preview.")
                    keywords: qsTr("chat link url preview")
                    textMinimum: 180
                    switchItem: linkPreviews

                    C.Toggle {
                        id: linkPreviews
                        Accessible.name: qsTr("Preview links I post")
                        checked: Chiaki.settings.coplayChatPreviews
                        onToggled: Chiaki.settings.coplayChatPreviews = checked
                    }
                }
            }

            // Chat messages on the game picture (Chiaki.hud.values.chat) live on Overlays & OBS, as the webcams there do
            Flow {
                Layout.fillWidth: true
                Layout.topMargin: -10
                Layout.leftMargin: 4
                spacing: 4

                Text {
                    width: Math.min(implicitWidth, parent.width)
                    height: Math.max(implicitHeight, chatLink.implicitHeight)
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("New messages on your game picture are set up under")
                    wrapMode: Text.WordWrap
                    color: C.Theme.textDim
                    font.pixelSize: 13
                }

                C.TextLink {
                    id: chatLink
                    text: qsTr("Overlays & OBS")
                    onClicked: page.openPage("overlays", qsTr("New chat messages"), chatLink.visualFocus)
                }
            }
        }

        // ------------------------------------------------ right: webcam
        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            Layout.fillWidth: true
            Layout.preferredWidth: page.twoColumns ? (columns.width - columns.columnSpacing) / 2 : columns.width
            spacing: 18

            C.SettingsGroup {
                title: qsTr("Webcam")

                C.SettingsRow {
                    id: cameraRow
                    title: qsTr("Camera")
                    subtitle: Chiaki.settings.coplayCameraSupported ? ""
                              : qsTr("This build of the app cannot use cameras. You still see the other players' webcams, and the test picture works.")
                    keywords: qsTr("webcam device video")
                    textMinimum: 90

                    C.Dropdown {
                        // Chiaki.settings.coplayCameraDevice: a name from the list, "" for the system's default
                        Layout.maximumWidth: cameraRow.width - cameraRow.leftPad - cameraRow.rightPad
                        Accessible.name: qsTr("Camera")
                        model: [qsTr("System default")].concat(page.cameras)
                        currentIndex: page.cameras.indexOf(Chiaki.settings.coplayCameraDevice) + 1
                        onActivated: (index) => Chiaki.settings.coplayCameraDevice = index > 0 ? page.cameras[index - 1] : ""
                    }
                }

                C.SettingsRow {
                    title: qsTr("Picture size")
                    subtitle: page.cameraSizes[Chiaki.settings.coplayCameraQuality] || ""
                    keywords: qsTr("webcam quality resolution bitrate small medium large")
                    textMinimum: 120

                    C.Segmented {
                        // Chiaki.settings.coplayCameraQuality: 0 small, 1 medium, 2 large
                        model: [qsTr("Small"), qsTr("Medium"), qsTr("Large")]
                        currentIndex: Chiaki.settings.coplayCameraQuality
                        onActivated: (index) => Chiaki.settings.coplayCameraQuality = index
                    }
                }

                // The picture as the others would get it, before a session
                C.SettingsRow {
                    id: cameraTest
                    readonly property var check: Chiaki.deviceCheck
                    title: qsTr("Your picture")
                    subtitle: page.playing ? qsTr("While you play, the stream menu shows it.")
                              : check.cameraError.length > 0 ? check.cameraError
                              : check.cameraTesting ? qsTr("As your friends see it, at the size chosen above.")
                              : qsTr("See your webcam before you play. Nothing is sent.")
                    keywords: qsTr("webcam camera preview test picture see myself check")

                    C.PillButton {
                        small: true
                        enabled: !page.playing
                        iconName: cameraTest.check.cameraTesting ? "" : "camera"
                        text: cameraTest.check.cameraTesting ? qsTr("Hide") : qsTr("Show")
                        Accessible.name: cameraTest.check.cameraTesting ? qsTr("Hide my webcam picture") : qsTr("Show my webcam picture")
                        onClicked: cameraTest.check.cameraTesting ? cameraTest.check.stopCamera() : cameraTest.check.startCamera()
                    }
                }

                Item {
                    Layout.fillWidth: true
                    visible: Chiaki.deviceCheck.cameraTesting
                    implicitHeight: visible ? preview.height + 18 : 0

                    // 16:9 like the picture that is sent, rounded like the webcam tiles in a session
                    Rectangle {
                        id: preview
                        x: 18
                        width: Math.min(parent.width - 36, 320)
                        height: Math.round(width * 9 / 16)
                        radius: 12
                        color: "#0d0b1f"

                        Image {
                            id: previewImage
                            anchors.fill: parent
                            visible: false
                            layer.enabled: true
                            fillMode: Image.PreserveAspectCrop
                            cache: false
                            source: Chiaki.deviceCheck.cameraFrame > 0
                                    ? "image://coplaycam/" + Chiaki.deviceCheck.cameraSlot + "/" + Chiaki.deviceCheck.cameraFrame : ""
                        }

                        Rectangle {
                            id: previewMask
                            anchors.fill: parent
                            radius: preview.radius
                            visible: false
                            layer.enabled: true
                        }

                        MultiEffect {
                            anchors.fill: parent
                            visible: Chiaki.deviceCheck.cameraFrame > 0
                            source: previewImage
                            maskEnabled: true
                            maskSource: previewMask
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: Chiaki.deviceCheck.cameraFrame === 0
                            text: qsTr("Starting the camera…")
                            color: C.Theme.textDim
                            font.pixelSize: 13
                        }
                    }
                }

                C.SettingsRow {
                    title: qsTr("On when a session starts")
                    subtitle: qsTr("While you play, the stream menu switches it on and off.")
                    keywords: qsTr("webcam camera switch on start automatically")
                    textMinimum: 180
                    switchItem: cameraOnStart

                    C.Toggle {
                        id: cameraOnStart
                        Accessible.name: qsTr("Webcam on when a session starts")
                        checked: Chiaki.settings.coplayCameraOnStart
                        onToggled: Chiaki.settings.coplayCameraOnStart = checked
                    }
                }

                C.SettingsRow {
                    title: qsTr("Show the other players' webcams")
                    keywords: qsTr("webcams cameras video friends")
                    textMinimum: 180
                    switchItem: showCams

                    C.Toggle {
                        id: showCams
                        Accessible.name: qsTr("Show the other players' webcams")
                        checked: Chiaki.settings.coplayShowCams
                        onToggled: Chiaki.settings.coplayShowCams = checked
                    }
                }
            }

            // Whether the webcams sit on the game picture (Chiaki.hud.values.cams_show) lives on Overlays & OBS
            Flow {
                Layout.fillWidth: true
                Layout.topMargin: -10
                Layout.leftMargin: 4
                spacing: 4

                Text {
                    width: Math.min(implicitWidth, parent.width)
                    height: Math.max(implicitHeight, overlaysLink.implicitHeight)
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("Webcams on your game picture are set up under")
                    wrapMode: Text.WordWrap
                    color: C.Theme.textDim
                    font.pixelSize: 13
                }

                C.TextLink {
                    id: overlaysLink
                    text: qsTr("Overlays & OBS")
                    onClicked: page.openPage("overlays", qsTr("Webcams in a corner"), overlaysLink.visualFocus)
                }
            }
        }
    }
}
