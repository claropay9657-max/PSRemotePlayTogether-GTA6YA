import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects

import org.streetpea.chiaking

import "controls" as C

// Play Together: join a session a friend shares, and set up sharing of your own.
DialogView {
    id: dialog
    title: qsTr("Play Together")
    buttonText: Chiaki.coplayJoining ? qsTr("Connecting…") : qsTr("Join")
    buttonEnabled: !Chiaki.coplayJoining && inviteField.text.trim().length > 0
    onAccepted: {
        errorLabel.text = "";
        Chiaki.joinCoPlay(inviteField.text, nameField.text, controllerOnlyCheck.checked);
    }

    Item {
        Connections {
            target: Chiaki

            // On success the stream view replaces this dialog by itself
            function onCoplayJoinFinished(ok, message) {
                if (!ok)
                    errorLabel.text = message;
            }
        }

        Flickable {
            id: flick
            anchors.fill: parent
            contentWidth: width
            contentHeight: column.implicitHeight + 60
            clip: true
            ScrollBar.vertical: ScrollBar { }

            ColumnLayout {
                id: column
                width: Math.min(flick.width - 80, 900)
                anchors {
                    top: parent.top
                    topMargin: 30
                    horizontalCenter: parent.horizontalCenter
                }
                spacing: 14

                // ---------------------------------------------------- profile

                C.GradientText {
                    text: qsTr("Your profile")
                    font.bold: true
                    font.pixelSize: 24
                    to: "#7c86ff"
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 20

                    Item {
                        id: avatarFrame
                        Layout.preferredWidth: 84
                        Layout.preferredHeight: 84

                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: Material.accent
                            visible: avatarImage.status !== Image.Ready

                            Label {
                                anchors.centerIn: parent
                                text: (nameField.text.trim().charAt(0) || "?").toUpperCase()
                                font.pixelSize: 36
                                font.bold: true
                            }
                        }

                        Image {
                            id: avatarImage
                            anchors.fill: parent
                            source: Chiaki.settings.coplayAvatarUrl
                            fillMode: Image.PreserveAspectCrop
                            cache: false
                            visible: false
                            layer.enabled: true
                        }

                        Rectangle {
                            id: avatarMask
                            anchors.fill: parent
                            radius: width / 2
                            visible: false
                            layer.enabled: true
                        }

                        // The picture, cut to a circle
                        MultiEffect {
                            anchors.fill: parent
                            source: avatarImage
                            maskEnabled: true
                            maskSource: avatarMask
                            visible: avatarImage.status === Image.Ready
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        C.TextField {
                            id: nameField
                            Layout.fillWidth: true
                            firstInFocusChain: true
                            maximumLength: 32
                            placeholderText: qsTr("Your name, shown to the other players")
                            text: Chiaki.settings.coplayName
                            onEditingFinished: Chiaki.settings.coplayName = text
                        }

                        RowLayout {
                            spacing: 10

                            C.Button {
                                text: qsTr("Choose picture")
                                onClicked: Chiaki.settings.coplayChooseAvatar()
                            }

                            C.Button {
                                text: qsTr("Remove")
                                visible: Chiaki.settings.coplayAvatarUrl.length > 0
                                onClicked: Chiaki.settings.coplayClearAvatar()
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 16
                    Layout.bottomMargin: 6
                    height: 1
                    opacity: 0.45
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#8c52ff" }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                }

                // ---------------------------------------------------- join

                C.GradientText {
                    text: qsTr("Join a friend")
                    font.bold: true
                    font.pixelSize: 24
                    to: "#7c86ff"
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: qsTr("Paste the invite your friend sent you. You get their picture and sound, and your controller plays along on their console.")
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 12
                    columnSpacing: 20

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Invite:")
                    }

                    C.TextField {
                        id: inviteField
                        Layout.fillWidth: true
                        placeholderText: "psrpt://…"
                        text: Chiaki.settings.coplayLastInvite
                        echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                    }

                    Item { }

                    C.CheckBox {
                        id: controllerOnlyCheck
                        text: qsTr("Controller only (no picture or sound)")
                        checked: Chiaki.settings.coplayControllerOnly
                    }

                    Item { visible: errorLabel.visible }

                    Label {
                        id: errorLabel
                        Layout.fillWidth: true
                        visible: text.length > 0
                        wrapMode: Text.WordWrap
                        color: Material.color(Material.Red, Material.Shade300)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 16
                    Layout.bottomMargin: 6
                    height: 1
                    opacity: 0.45
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#8c52ff" }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                }

                // ---------------------------------------------------- share

                C.GradientText {
                    text: qsTr("Share your console")
                    font.bold: true
                    font.pixelSize: 24
                    to: "#7c86ff"
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: qsTr("While this is on, friends with your invite can join whenever you are connected to your console. The console sees everybody as one controller.")
                }

                C.CheckBox {
                    id: hostEnabledCheck
                    text: qsTr("Let friends join my sessions")
                    checked: Chiaki.settings.coplayHostEnabled
                    onToggled: Chiaki.settings.coplayHostEnabled = checked
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 12
                    columnSpacing: 20
                    enabled: hostEnabledCheck.checked
                    opacity: enabled ? 1.0 : 0.4

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Invite for friends:")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        // Shown for copying, never edited
                        TextField {
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            focusPolicy: Qt.ClickFocus
                            text: hostEnabledCheck.checked ? Chiaki.settings.coplayInvite : ""
                            echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                        }

                        C.Button {
                            text: copiedTimer.running ? qsTr("Copied") : qsTr("Copy")
                            onClicked: {
                                Chiaki.settings.coplayCopyInvite();
                                copiedTimer.restart();
                            }
                            Timer {
                                id: copiedTimer
                                interval: 1500
                            }
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Players control:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 420
                        model: [
                            qsTr("All at once"),
                            qsTr("One at a time, passing the controller"),
                            qsTr("Only me, guests watch"),
                        ]
                        currentIndex: Chiaki.settings.coplayMixMode
                        onActivated: (index) => Chiaki.settings.coplayMixMode = index
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Address in the invite:")
                    }

                    C.TextField {
                        Layout.fillWidth: true
                        placeholderText: Chiaki.settings.coplayLocalAddress
                            ? qsTr("%1 (this network only)").arg(Chiaki.settings.coplayLocalAddress)
                            : qsTr("Your address")
                        text: Chiaki.settings.coplayAddress
                        onEditingFinished: Chiaki.settings.coplayAddress = text
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("UDP port:")
                    }

                    C.TextField {
                        Layout.preferredWidth: 140
                        validator: IntValidator { bottom: 1; top: 65535 }
                        text: Chiaki.settings.coplayPort
                        onEditingFinished: {
                            if (acceptableInput)
                                Chiaki.settings.coplayPort = parseInt(text);
                            else
                                text = Chiaki.settings.coplayPort;
                        }
                    }

                    Item { }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        text: qsTr("Friends outside your home network need your public address (or a DNS name) in the invite, and the UDP port forwarded to this computer in your router.")
                    }

                    Item { }

                    C.Button {
                        text: qsTr("Make a new invite")
                        onClicked: root.showConfirmDialog(qsTr("New invite"),
                            qsTr("Invites you handed out before will stop working. Continue?"),
                            () => Chiaki.settings.coplayNewKey())
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 16
                    Layout.bottomMargin: 6
                    height: 1
                    opacity: 0.45
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#8c52ff" }
                        GradientStop { position: 1.0; color: "transparent" }
                    }
                }

                // ---------------------------------------------------- overlay

                C.GradientText {
                    text: qsTr("Stream overlay for OBS")
                    font.bold: true
                    font.pixelSize: 24
                    to: "#7c86ff"
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: qsTr("Shows your viewers who is playing, who is in control right now and everybody's controller, live. Add the address below to OBS as a Browser Source. It works for every player, and when you play alone it shows your own controller.")
                }

                C.CheckBox {
                    id: overlayEnabledCheck
                    text: qsTr("Turn on the overlay")
                    checked: Chiaki.settings.overlayEnabled
                    onToggled: Chiaki.settings.overlayEnabled = checked
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 12
                    columnSpacing: 20
                    enabled: overlayEnabledCheck.checked
                    opacity: enabled ? 1.0 : 0.4

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Address for OBS:")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        TextField {
                            Layout.fillWidth: true
                            readOnly: true
                            selectByMouse: true
                            focusPolicy: Qt.ClickFocus
                            text: Chiaki.overlayUrl
                        }

                        C.Button {
                            text: overlayCopiedTimer.running ? qsTr("Copied") : qsTr("Copy")
                            enabled: Chiaki.overlayUrl.length > 0
                            onClicked: {
                                Chiaki.copyOverlayUrl();
                                overlayCopiedTimer.restart();
                            }
                            Timer {
                                id: overlayCopiedTimer
                                interval: 1500
                            }
                        }
                    }

                    Item { visible: overlayErrorLabel.visible }

                    Label {
                        id: overlayErrorLabel
                        Layout.fillWidth: true
                        visible: overlayEnabledCheck.checked && Chiaki.overlayError.length > 0
                        wrapMode: Text.WordWrap
                        color: Material.color(Material.Red, Material.Shade300)
                        text: qsTr("The overlay could not start: %1. Try another port.").arg(Chiaki.overlayError)
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight | Qt.AlignTop
                        Layout.topMargin: 12
                        text: qsTr("Show:")
                    }

                    ColumnLayout {
                        spacing: 0

                        C.CheckBox {
                            text: qsTr("Names and pictures")
                            checked: Chiaki.settings.overlayPlayers
                            onToggled: Chiaki.settings.overlayPlayers = checked
                        }

                        C.CheckBox {
                            text: qsTr("Each player's controller")
                            checked: Chiaki.settings.overlayPads
                            onToggled: Chiaki.settings.overlayPads = checked
                        }

                        C.CheckBox {
                            text: qsTr("The combined controller the game gets")
                            checked: Chiaki.settings.overlayCombined
                            onToggled: Chiaki.settings.overlayCombined = checked
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Controller look:")
                    }

                    C.ComboBox {
                        id: skinCombo
                        Layout.preferredWidth: 420
                        model: [qsTr("Match each player's controller")].concat(Chiaki.settings.overlaySkins)
                        currentIndex: Math.max(0, Chiaki.settings.overlaySkins.indexOf(Chiaki.settings.overlaySkin) + 1)
                        onActivated: (index) => Chiaki.settings.overlaySkin = index === 0 ? "auto" : Chiaki.settings.overlaySkins[index - 1]
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Controller size:")
                    }

                    RowLayout {
                        spacing: 12

                        C.Slider {
                            Layout.preferredWidth: 340
                            from: 10
                            to: 100
                            stepSize: 5
                            value: Chiaki.settings.overlayScale
                            onMoved: Chiaki.settings.overlayScale = value
                        }

                        Label {
                            text: qsTr("%1 %").arg(Chiaki.settings.overlayScale)
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Players:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 420
                        model: [qsTr("Side by side"), qsTr("Below each other")]
                        currentIndex: Chiaki.settings.overlayLayout === "column" ? 1 : 0
                        onActivated: (index) => Chiaki.settings.overlayLayout = index === 1 ? "column" : "row"
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Port:")
                    }

                    C.TextField {
                        Layout.preferredWidth: 140
                        lastInFocusChain: true
                        validator: IntValidator { bottom: 1; top: 65535 }
                        text: Chiaki.settings.overlayPort
                        onEditingFinished: {
                            if (acceptableInput)
                                Chiaki.settings.overlayPort = parseInt(text);
                            else
                                text = Chiaki.settings.overlayPort;
                        }
                    }

                    Item { }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        text: qsTr("To arrange your scene before a session, add ?demo=1 to the address: it shows made-up players. Several sources can show different parts, for example ?show=players or ?show=combined.")
                    }
                }
            }
        }
    }
}
