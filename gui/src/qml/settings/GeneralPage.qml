import QtQuick
import QtQuick.Layouts

import org.streetpea.chiaking

import "../controls" as C

// General: what happens when a session ends, and while you stream.
C.SettingsPage {
    id: page
    title: qsTr("General")
    description: qsTr("What happens when a session ends, and while you stream.")

    /** The buttons a combination can use, by Chiaki.settings.streamMenuShortcut1…4; 0 is none. */
    readonly property var buttonNames: [qsTr("None"), qsTr("Cross"), qsTr("Circle"), qsTr("Square"), qsTr("Triangle"),
        qsTr("D-pad left"), qsTr("D-pad right"), qsTr("D-pad up"), qsTr("D-pad down"),
        "L1", "R1", "L3", "R3", qsTr("Options"), qsTr("Create"), qsTr("Touchpad"), "PS"]
    /** The same on a key cap. */
    readonly property var buttonCaps: ["", "✕", "○", "□", "△", "←", "→", "↑", "↓",
        "L1", "R1", "L3", "R3", qsTr("Options"), qsTr("Create"), qsTr("Touchpad"), "PS"]
    readonly property var menuButtons: [Chiaki.settings.streamMenuShortcut1, Chiaki.settings.streamMenuShortcut2,
        Chiaki.settings.streamMenuShortcut3, Chiaki.settings.streamMenuShortcut4].filter((b) => b > 0)
    property bool changingCombination: false

    C.SettingsGroup {
        title: qsTr("When a session ends")

        C.SettingsRow {
            title: qsTr("Closing the stream")
            subtitle: qsTr("What happens to the console when you leave.")
            keywords: qsTr("disconnect quit rest mode sleep standby")

            C.Segmented {
                // Chiaki.settings.disconnectAction: 0 leave it on, 1 rest mode, 2 ask
                readonly property var values: [2, 1, 0]
                model: [qsTr("Ask me"), qsTr("Rest mode"), qsTr("Leave it on")]
                currentIndex: values.indexOf(Chiaki.settings.disconnectAction)
                onActivated: (index) => Chiaki.settings.disconnectAction = values[index]
            }
        }

        C.SettingsRow {
            title: qsTr("When this computer sleeps")
            subtitle: qsTr("What happens to the console then.")
            keywords: qsTr("suspend rest mode standby")

            C.Segmented {
                // Chiaki.settings.suspendAction: 0 leave it on, 1 rest mode
                readonly property var values: [1, 0]
                model: [qsTr("Rest mode"), qsTr("Leave it on")]
                currentIndex: values.indexOf(Chiaki.settings.suspendAction)
                onActivated: (index) => Chiaki.settings.suspendAction = values[index]
            }
        }

        C.SettingsRow {
            title: qsTr("Reconnect by itself")
            subtitle: qsTr("Gets a lost connection back without asking: to the console, or as a guest to the host.")
            switchItem: reconnect

            C.Toggle {
                id: reconnect
                Accessible.name: qsTr("Reconnect by itself")
                checked: Chiaki.settings.coplayAutoReconnect
                onToggled: Chiaki.settings.coplayAutoReconnect = checked
            }
        }
    }

    C.SettingsGroup {
        title: qsTr("While streaming")

        C.SettingsRow {
            title: qsTr("On this computer")
            subtitle: qsTr("Both off when you play on the TV and only use this computer for your controller. Friends who join still get both. The Host page has the same as “I play on the TV”.")
            keywords: qsTr("audio video picture sound disabled tv")

            // Chiaki.settings.audioVideoDisabled: 1 is no sound, 2 no picture, 3 neither
            C.ChipToggle {
                iconName: "monitor"
                text: qsTr("Picture")
                checked: (Chiaki.settings.audioVideoDisabled & 2) === 0
                onToggled: Chiaki.settings.audioVideoDisabled = checked ? Chiaki.settings.audioVideoDisabled & ~2 : Chiaki.settings.audioVideoDisabled | 2
            }

            C.ChipToggle {
                iconName: "speaker"
                text: qsTr("Sound")
                checked: (Chiaki.settings.audioVideoDisabled & 1) === 0
                onToggled: Chiaki.settings.audioVideoDisabled = checked ? Chiaki.settings.audioVideoDisabled & ~1 : Chiaki.settings.audioVideoDisabled | 1
            }
        }

        C.SettingsRow {
            title: qsTr("Streamer mode")
            subtitle: qsTr("Hides addresses, invites and console IDs while your screen is on a stream.")
            keywords: qsTr("privacy hide twitch obs")
            switchItem: streamerMode

            C.Toggle {
                id: streamerMode
                Accessible.name: qsTr("Streamer mode")
                checked: Chiaki.settings.streamerMode
                onToggled: Chiaki.settings.streamerMode = checked
            }
        }

        C.SettingsRow {
            title: qsTr("Stream menu on the controller")
            subtitle: Qt.platform.os === "osx"
                      ? qsTr("Hold these buttons together. On the keyboard it is always ⌘O.")
                      : qsTr("Hold these buttons together. On the keyboard it is always Ctrl+O.")
            keywords: qsTr("button combination shortcut open menu")
            switchItem: streamMenu

            Row {
                visible: Chiaki.settings.streamMenuEnabled
                spacing: 5

                Repeater {
                    model: page.menuButtons

                    Row {
                        required property var modelData
                        required property int index
                        spacing: 5

                        Text {
                            visible: parent.index > 0
                            anchors.verticalCenter: parent.verticalCenter
                            text: "+"
                            color: C.Theme.textDim
                            font.pixelSize: 12
                        }

                        C.Keycap {
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.buttonCaps[parent.modelData] || "?"
                        }
                    }
                }

                Text {
                    visible: page.menuButtons.length === 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("No buttons")
                    color: C.Theme.textDim
                    font.pixelSize: 13
                }
            }

            C.PillButton {
                visible: Chiaki.settings.streamMenuEnabled
                Layout.leftMargin: 4
                small: true
                text: page.changingCombination ? qsTr("Done") : qsTr("Change…")
                onClicked: page.changingCombination = !page.changingCombination
            }

            C.Toggle {
                id: streamMenu
                Layout.leftMargin: 4
                Accessible.name: qsTr("Stream menu on the controller")
                checked: Chiaki.settings.streamMenuEnabled
                onToggled: Chiaki.settings.streamMenuEnabled = checked
            }
        }

        C.SettingsRow {
            visible: page.changingCombination && Chiaki.settings.streamMenuEnabled
            sub: true
            title: qsTr("Buttons")
            subtitle: qsTr("Up to four, held together.")
            textMinimum: 120

            Repeater {
                model: 4

                C.Dropdown {
                    required property int index
                    readonly property string setting: "streamMenuShortcut" + (index + 1)
                    Accessible.name: qsTr("Button %1").arg(index + 1)
                    model: page.buttonNames
                    currentIndex: Chiaki.settings[setting]
                    onActivated: (choice) => Chiaki.settings[setting] = choice
                }
            }
        }
    }

    C.SettingsGroup {
        visible: typeof Chiaki.settings.steamDeckHaptics !== "undefined" || typeof Chiaki.settings.verticalDeck !== "undefined"
        title: qsTr("Steam Deck")

        C.SettingsRow {
            visible: typeof Chiaki.settings.steamDeckHaptics !== "undefined"
            title: qsTr("True haptics")
            subtitle: qsTr("Better quality, but noisier.")
            switchItem: deckHaptics

            C.Toggle {
                id: deckHaptics
                Accessible.name: qsTr("True haptics")
                checked: !!Chiaki.settings.steamDeckHaptics
                onToggled: Chiaki.settings.steamDeckHaptics = checked
            }
        }

        C.SettingsRow {
            visible: typeof Chiaki.settings.verticalDeck !== "undefined"
            title: qsTr("Held upright")
            subtitle: qsTr("For motion controls with the Steam Deck held upright.")
            keywords: qsTr("vertical orientation gyro")
            switchItem: deckUpright

            C.Toggle {
                id: deckUpright
                Accessible.name: qsTr("Held upright")
                checked: !!Chiaki.settings.verticalDeck
                onToggled: Chiaki.settings.verticalDeck = checked
            }
        }
    }
}
