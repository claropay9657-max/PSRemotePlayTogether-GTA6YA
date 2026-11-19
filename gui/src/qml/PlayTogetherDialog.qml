import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

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

                // ---------------------------------------------------- join

                Label {
                    text: qsTr("Join a friend")
                    font.bold: true
                    font.pixelSize: 22
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
                        firstInFocusChain: true
                        placeholderText: "psrpt://…"
                        text: Chiaki.settings.coplayLastInvite
                        echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Your name:")
                    }

                    C.TextField {
                        id: nameField
                        Layout.fillWidth: true
                        maximumLength: 32
                        placeholderText: qsTr("Shown to the other players")
                        text: Chiaki.settings.coplayName
                        onEditingFinished: Chiaki.settings.coplayName = text
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
                    color: Material.dividerColor
                }

                // ---------------------------------------------------- share

                Label {
                    text: qsTr("Share your console")
                    font.bold: true
                    font.pixelSize: 22
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
                        lastInFocusChain: true
                        text: qsTr("Make a new invite")
                        onClicked: root.showConfirmDialog(qsTr("New invite"),
                            qsTr("Invites you handed out before will stop working. Continue?"),
                            () => Chiaki.settings.coplayNewKey())
                    }
                }
            }
        }
    }
}
