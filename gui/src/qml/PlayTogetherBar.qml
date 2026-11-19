import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

// The Play Together strip of the stream menu: who is here, who is talking,
// everybody's webcam, and the switches for one's own microphone, sound and
// webcam. The host also sets here who may control and can remove a guest.
Item {
    id: bar

    /** Where the down key leaves the strip to. */
    property Item downItem: null
    /** Where a key press from outside should enter the strip. */
    readonly property Item firstItem: !active ? displayButton : (voiceOn ? micButton : voiceButton)
    signal closeRequested()
    /** Open the display options (what is shown on the game picture and in OBS). */
    signal displayRequested()

    readonly property var session: Chiaki.session
    readonly property bool active: !!session && session.coplayActive
    readonly property bool voiceOn: active && session.coplayVoiceOn
    readonly property bool hosting: active && session.coplayHosting
    readonly property var players: active ? session.coplayPlayers : []
    readonly property var talking: active ? session.coplayTalking : ({})
    readonly property int camSerial: active ? session.coplayCamSerial : 0

    visible: !!session
    implicitHeight: 120

    // The invites of the running session: one per way a friend can reach this computer
    readonly property string inviteLan: hosting ? (Chiaki.settings.coplayInviteLan || "") : ""
    readonly property string invitePublic: hosting ? (Chiaki.settings.coplayInvitePublic || "") : ""
    readonly property string inviteIPv6: hosting ? (Chiaki.settings.coplayInviteIPv6 || "") : ""

    // Sharing has started: now the public address is worth asking for
    onHostingChanged: {
        if (hosting && Chiaki.settings.coplayLookupPublicAddress && (Chiaki.settings.coplayPublicLookupState || 0) === 0)
            Chiaki.settings.coplayLookupPublicAddress();
    }

    // Copies any text: a text item is the one thing in QML that reaches the clipboard
    TextEdit {
        id: clipboard
        visible: false
    }

    function copyText(text) {
        clipboard.text = text;
        clipboard.selectAll();
        clipboard.copy();
        clipboard.text = "";
        inviteCopied.restart();
    }

    readonly property color accent: "#8c52ff"
    readonly property color cyan: "#00f2ff"
    readonly property color green: "#34d399"
    readonly property color red: "#f0466e"

    component BarButton: ToolButton {
        id: sw
        /** Draws attention: a muted microphone, a running camera. */
        property color tint: "transparent"
        padding: 10
        leftPadding: 14
        rightPadding: 14
        font.pixelSize: 15
        font.bold: true
        activeFocusOnTab: true
        Keys.onReturnPressed: clicked()
        Keys.onEscapePressed: bar.closeRequested()
        KeyNavigation.down: bar.downItem
        background: Rectangle {
            radius: 10
            color: sw.tint.a > 0 ? sw.tint : Qt.rgba(1, 1, 1, sw.hovered || sw.activeFocus ? 0.18 : 0.09)
            border.width: sw.activeFocus ? 2 : 1
            border.color: sw.activeFocus ? bar.cyan : Qt.rgba(1, 1, 1, 0.14)
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 18

        // ------------------------------------------------ our own switches

        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: 8

            RowLayout {
                spacing: 8

                BarButton {
                    id: voiceButton
                    visible: bar.active && !bar.voiceOn
                    text: qsTr("Join voice chat")
                    onClicked: bar.session.coplayVoiceOn = true
                    KeyNavigation.right: camButton
                }

                BarButton {
                    id: micButton
                    visible: bar.voiceOn
                    text: bar.active && bar.session.coplayMicMuted ? qsTr("Mic muted") : qsTr("Mic on")
                    tint: bar.active && bar.session.coplayMicMuted ? Qt.rgba(0.94, 0.27, 0.43, 0.75) : "transparent"
                    onClicked: bar.session.coplayMicMuted = !bar.session.coplayMicMuted
                    KeyNavigation.right: soundButton
                }

                BarButton {
                    id: soundButton
                    visible: bar.voiceOn
                    text: bar.active && bar.session.coplayDeafened ? qsTr("Voices off") : qsTr("Voices on")
                    tint: bar.active && bar.session.coplayDeafened ? Qt.rgba(0.94, 0.27, 0.43, 0.75) : "transparent"
                    onClicked: bar.session.coplayDeafened = !bar.session.coplayDeafened
                    KeyNavigation.left: micButton
                    KeyNavigation.right: camButton
                }

                BarButton {
                    id: camButton
                    visible: bar.active
                    text: bar.active && bar.session.coplayCameraOn ? qsTr("Webcam on") : qsTr("Webcam off")
                    tint: bar.active && bar.session.coplayCameraOn ? Qt.rgba(0.2, 0.83, 0.6, 0.55) : "transparent"
                    onClicked: bar.session.coplayCameraOn = !bar.session.coplayCameraOn
                    KeyNavigation.left: bar.voiceOn ? soundButton : voiceButton
                    KeyNavigation.right: bar.voiceOn ? leaveVoiceButton : (bar.hosting ? modeCombo : camButton)
                }

                BarButton {
                    id: leaveVoiceButton
                    visible: bar.voiceOn
                    text: qsTr("Leave voice chat")
                    onClicked: bar.session.coplayVoiceOn = false
                    KeyNavigation.left: camButton
                    KeyNavigation.right: displayButton
                }

                BarButton {
                    id: displayButton
                    text: qsTr("Overlays…")
                    tint: Qt.rgba(140 / 255, 82 / 255, 255 / 255, 0.55)
                    onClicked: bar.displayRequested()
                    KeyNavigation.left: bar.voiceOn ? leaveVoiceButton : camButton
                    KeyNavigation.right: bar.hosting ? modeCombo : displayButton
                }
            }

            RowLayout {
                visible: bar.hosting
                spacing: 8

                Label {
                    text: qsTr("Players control:")
                    opacity: 0.8
                }

                ComboBox {
                    id: modeCombo
                    Layout.preferredWidth: 250
                    model: [qsTr("All at once"), qsTr("One at a time"), qsTr("Only me")]
                    currentIndex: bar.active ? bar.session.coplayMixMode : 0
                    onActivated: (index) => bar.session.coplayMixMode = index
                    Keys.onEscapePressed: bar.closeRequested()
                    KeyNavigation.left: bar.voiceOn ? leaveVoiceButton : camButton
                    KeyNavigation.right: inviteButton
                    KeyNavigation.down: bar.downItem
                }

                BarButton {
                    id: inviteButton
                    text: inviteCopied.running ? qsTr("Copied") : qsTr("Copy invite")
                    onClicked: {
                        // One invite: copy it. Several: ask which friend it is for.
                        if (bar.invitePublic.length === 0 && bar.inviteIPv6.length === 0) {
                            bar.session.coplayCopyInvite();
                            inviteCopied.restart();
                        } else {
                            inviteMenu.popup();
                        }
                    }
                    KeyNavigation.left: modeCombo
                    Timer {
                        id: inviteCopied
                        interval: 1500
                    }

                    Menu {
                        id: inviteMenu

                        MenuItem {
                            visible: bar.inviteLan.length > 0
                            height: visible ? implicitHeight : 0
                            text: qsTr("For a friend in the same network")
                            onTriggered: bar.copyText(bar.inviteLan)
                        }

                        MenuItem {
                            visible: bar.invitePublic.length > 0
                            height: visible ? implicitHeight : 0
                            text: qsTr("For a friend over the internet")
                            onTriggered: bar.copyText(bar.invitePublic)
                        }

                        MenuItem {
                            visible: bar.inviteIPv6.length > 0
                            height: visible ? implicitHeight : 0
                            text: qsTr("For a friend over the internet, IPv6")
                            onTriggered: bar.copyText(bar.inviteIPv6)
                        }
                    }
                }
            }

            Label {
                visible: !bar.hosting
                opacity: 0.7
                text: !bar.active ? qsTr("Playing alone. Overlays… sets what is shown on the game picture and in OBS.")
                      : (!bar.session.coplayVideoEnabled ? qsTr("Controller only: no picture from the console")
                                                         : qsTr("Click a player for more"))
            }
        }

        // ------------------------------------------------ everybody

        ListView {
            id: playerList
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: ListView.Horizontal
            spacing: 10
            clip: true
            model: bar.players

            delegate: Item {
                id: tile
                required property var modelData
                readonly property bool isTalking: !!bar.talking[String(modelData.slot)]
                readonly property bool isMuted: modelData.muted || modelData.silenced
                // The shape of a webcam picture
                width: Math.round(height * 16 / 9)
                height: playerList.height

                Rectangle {
                    id: frame
                    anchors.fill: parent
                    radius: 12
                    color: "#12121c"
                    border.width: tile.isTalking || tile.modelData.inControl ? 2 : 1
                    border.color: tile.isTalking ? bar.green : (tile.modelData.inControl ? bar.accent : Qt.rgba(1, 1, 1, 0.14))
                    clip: true

                    // The webcam, or the player's picture, or their initial
                    Image {
                        id: cam
                        anchors.fill: parent
                        anchors.margins: frame.border.width
                        visible: tile.modelData.camera
                        cache: false
                        fillMode: Image.PreserveAspectCrop
                        source: tile.modelData.camera ? "image://coplaycam/" + tile.modelData.slot + "/" + bar.camSerial : ""
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: -8
                        visible: !tile.modelData.camera
                        width: 54
                        height: 54
                        radius: 27
                        clip: true
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: bar.accent }
                            GradientStop { position: 1.0; color: "#f0469b" }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: !avatar.visible
                            text: (tile.modelData.name || "?").trim().charAt(0).toUpperCase()
                            font.bold: true
                            font.pixelSize: 24
                        }

                        Image {
                            id: avatar
                            anchors.fill: parent
                            visible: status === Image.Ready
                            fillMode: Image.PreserveAspectCrop
                            source: tile.modelData.avatarUrl || ""
                        }
                    }

                    // Name and state along the bottom
                    Rectangle {
                        anchors {
                            left: parent.left
                            right: parent.right
                            bottom: parent.bottom
                            margins: frame.border.width
                        }
                        height: 26
                        color: Qt.rgba(0, 0, 0, 0.62)

                        RowLayout {
                            anchors {
                                fill: parent
                                leftMargin: 8
                                rightMargin: 6
                            }
                            spacing: 6

                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.pixelSize: 13
                                font.bold: true
                                text: (tile.modelData.name || qsTr("Player %1").arg(tile.modelData.slot + 1))
                                      + (tile.modelData.you ? qsTr(" (you)") : "")
                            }

                            Rectangle {
                                visible: tile.isMuted || tile.modelData.mutedHere
                                radius: 8
                                color: bar.red
                                implicitWidth: mutedLabel.implicitWidth + 10
                                implicitHeight: 16
                                Label {
                                    id: mutedLabel
                                    anchors.centerIn: parent
                                    font.pixelSize: 9
                                    font.bold: true
                                    text: tile.modelData.mutedHere && !tile.isMuted ? qsTr("MUTED HERE") : qsTr("MUTED")
                                }
                            }

                            Rectangle {
                                visible: tile.modelData.voice && !tile.isMuted && !tile.modelData.mutedHere
                                width: 10
                                height: 10
                                radius: 5
                                color: tile.isTalking ? bar.green : Qt.rgba(1, 1, 1, 0.3)
                            }
                        }
                    }

                    // Host, and who controls
                    Row {
                        anchors {
                            left: parent.left
                            top: parent.top
                            margins: 6
                        }
                        spacing: 4

                        Rectangle {
                            visible: tile.modelData.host
                            radius: 8
                            color: Qt.rgba(0.55, 0.32, 1.0, 0.85)
                            width: hostLabel.implicitWidth + 10
                            height: 16
                            Label {
                                id: hostLabel
                                anchors.centerIn: parent
                                font.pixelSize: 9
                                font.bold: true
                                text: qsTr("HOST")
                            }
                        }

                        Rectangle {
                            visible: !tile.modelData.canControl
                            radius: 8
                            color: Qt.rgba(0, 0, 0, 0.6)
                            width: watchLabel.implicitWidth + 10
                            height: 16
                            Label {
                                id: watchLabel
                                anchors.centerIn: parent
                                font.pixelSize: 9
                                font.bold: true
                                text: qsTr("WATCHING")
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: !tile.modelData.you
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: playerMenu.popup()
                }

                Menu {
                    id: playerMenu

                    MenuItem {
                        text: tile.modelData.mutedHere ? qsTr("Hear %1 again").arg(tile.modelData.name) : qsTr("Mute %1 for me").arg(tile.modelData.name)
                        onTriggered: bar.session.coplaySetPlayerMuted(tile.modelData.slot, !tile.modelData.mutedHere)
                    }

                    MenuItem {
                        visible: bar.hosting
                        height: visible ? implicitHeight : 0
                        text: tile.modelData.silenced ? qsTr("Let everybody hear them again") : qsTr("Mute for everybody")
                        onTriggered: bar.session.coplaySetSilenced(tile.modelData.slot, !tile.modelData.silenced)
                    }

                    MenuItem {
                        visible: bar.hosting
                        height: visible ? implicitHeight : 0
                        text: tile.modelData.canControl ? qsTr("Only let them watch") : qsTr("Let them play")
                        onTriggered: bar.session.coplaySetCanControl(tile.modelData.slot, !tile.modelData.canControl)
                    }

                    MenuItem {
                        visible: bar.hosting
                        height: visible ? implicitHeight : 0
                        text: qsTr("Remove from the session")
                        onTriggered: bar.session.coplayKick(tile.modelData.slot)
                    }
                }
            }
        }
    }
}
