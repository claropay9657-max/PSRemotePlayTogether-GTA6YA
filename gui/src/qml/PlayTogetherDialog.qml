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
    /** 0 = join and share, 1 = voice chat and webcam, 2 = overlays */
    property int page: 0
    buttonText: Chiaki.coplayJoining ? qsTr("Connecting…") : qsTr("Join")
    buttonEnabled: !Chiaki.coplayJoining && inviteField.text.trim().length > 0
    buttonVisible: page === 0
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

        // Keyboard and controller: whatever gets the focus is scrolled into view
        Connections {
            target: dialog.Window.window
            function onActiveFocusItemChanged() {
                const item = dialog.Window.window ? dialog.Window.window.activeFocusItem : null;
                if (!item)
                    return;
                let inside = false;
                for (let p = item; p; p = p.parent) {
                    if (p === flick.contentItem) {
                        inside = true;
                        break;
                    }
                }
                if (!inside)
                    return;
                const top = item.mapToItem(flick.contentItem, 0, 0).y - 40;
                const bottom = top + item.height + 80;
                const max = Math.max(0, flick.contentHeight - flick.height);
                if (top < flick.contentY)
                    flick.contentY = Math.max(0, top);
                else if (bottom > flick.contentY + flick.height)
                    flick.contentY = Math.min(max, bottom - flick.height);
            }
        }

        // The three parts of Play Together
        Row {
            id: tabs
            anchors {
                top: parent.top
                topMargin: 18
                horizontalCenter: parent.horizontalCenter
            }
            spacing: 8

            Repeater {
                model: [qsTr("Join & share"), qsTr("Voice & webcam"), qsTr("Overlays")]

                Rectangle {
                    id: tab
                    required property string modelData
                    required property int index
                    readonly property bool current: dialog.page === index
                    width: tabLabel.implicitWidth + 44
                    height: 44
                    radius: 22
                    color: current ? "#8c52ff" : (tabArea.containsMouse || activeFocus ? Qt.rgba(1, 1, 1, 0.14) : Qt.rgba(1, 1, 1, 0.07))
                    border.width: activeFocus ? 2 : 1
                    border.color: activeFocus ? "#00f2ff" : (current ? "#8c52ff" : Qt.rgba(1, 1, 1, 0.12))
                    activeFocusOnTab: true
                    Keys.onReturnPressed: dialog.page = index
                    Keys.onSpacePressed: dialog.page = index

                    Label {
                        id: tabLabel
                        anchors.centerIn: parent
                        font.pixelSize: 17
                        font.bold: tab.current
                        text: tab.modelData
                    }

                    MouseArea {
                        id: tabArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            dialog.page = tab.index;
                            flick.contentY = 0;
                        }
                    }
                }
            }
        }

        Flickable {
            id: flick
            anchors {
                fill: parent
                topMargin: 76
            }
            contentWidth: width
            contentHeight: column.implicitHeight + 60
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            // Always there while the page is longer than the window: shows that there is
            // more below, and can be dragged
            ScrollBar.vertical: ScrollBar {
                policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                width: 12
            }

            ColumnLayout {
                id: column
                width: Math.min(flick.width - 80, dialog.page === 2 ? 1180 : 900)
                anchors {
                    top: parent.top
                    topMargin: 14
                    horizontalCenter: parent.horizontalCenter
                }
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: dialog.page === 0
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

                }

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: dialog.page === 1
                    spacing: 14

                    // ---------------------------------------------------- voice and webcam

                    C.GradientText {
                        text: qsTr("Voice chat and webcam")
                        font.bold: true
                        font.pixelSize: 24
                        to: "#7c86ff"
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        text: qsTr("Everybody in a shared session can talk to and see each other, whether you host or join. Microphone and speakers are the ones chosen in the audio settings. Use headphones, otherwise the others hear themselves through your microphone. While you play, the stream menu has buttons to mute yourself and to switch the webcam on and off.")
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        text: qsTr("You never hear your own voice in the app, only the others. If a friend hears themselves, it is their voice coming out of your speakers into your microphone: a headset fixes that.")
                    }

                    C.CheckBox {
                        id: voiceEnabledCheck
                        text: qsTr("Take part in the voice chat")
                        checked: Chiaki.settings.coplayVoiceEnabled
                        onToggled: Chiaki.settings.coplayVoiceEnabled = checked
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        rowSpacing: 12
                        columnSpacing: 20

                        Item { }

                        C.CheckBox {
                            enabled: voiceEnabledCheck.checked
                            opacity: enabled ? 1.0 : 0.4
                            text: qsTr("Start with my microphone muted")
                            checked: Chiaki.settings.coplayVoiceStartMuted
                            onToggled: Chiaki.settings.coplayVoiceStartMuted = checked
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            enabled: voiceEnabledCheck.checked
                            opacity: enabled ? 1.0 : 0.4
                            text: qsTr("Volume of the others:")
                        }

                        RowLayout {
                            enabled: voiceEnabledCheck.checked
                            opacity: enabled ? 1.0 : 0.4
                            spacing: 10

                            C.Slider {
                                Layout.preferredWidth: 300
                                from: 0
                                to: 200
                                stepSize: 5
                                value: Chiaki.settings.coplayVoiceVolume
                                onMoved: Chiaki.settings.coplayVoiceVolume = value
                            }

                            Label {
                                text: qsTr("%1 %").arg(Chiaki.settings.coplayVoiceVolume)
                            }
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Webcam:")
                        }

                        C.ComboBox {
                            id: cameraCombo
                            Layout.preferredWidth: 420
                            property var devices: Chiaki.settings.coplayCameraDevices()
                            model: [qsTr("The system's default camera")].concat(devices)
                            currentIndex: Math.max(0, devices.indexOf(Chiaki.settings.coplayCameraDevice) + 1)
                            onActivated: (index) => Chiaki.settings.coplayCameraDevice = index === 0 ? "" : devices[index - 1]
                        }

                        Label {
                            Layout.alignment: Qt.AlignRight
                            text: qsTr("Webcam picture:")
                        }

                        C.ComboBox {
                            Layout.preferredWidth: 420
                            model: [
                                qsTr("Small (320 × 180, about 0.4 Mbit/s)"),
                                qsTr("Medium (480 × 270, about 1 Mbit/s)"),
                                qsTr("Large (640 × 360, about 2.5 Mbit/s)"),
                            ]
                            currentIndex: Chiaki.settings.coplayCameraQuality
                            onActivated: (index) => Chiaki.settings.coplayCameraQuality = index
                        }

                        Item { }

                        ColumnLayout {
                            spacing: 0

                            C.CheckBox {
                                text: qsTr("Switch my webcam on when a session starts")
                                checked: Chiaki.settings.coplayCameraOnStart
                                onToggled: Chiaki.settings.coplayCameraOnStart = checked
                            }

                            C.CheckBox {
                                id: showCamsCheck
                                text: qsTr("Show the other players' webcams")
                                checked: Chiaki.settings.coplayShowCams
                                onToggled: Chiaki.settings.coplayShowCams = checked
                            }

                            C.CheckBox {
                                text: qsTr("Keep the webcams in a corner of the game picture while playing")
                                // The same switch as "Webcams" on the Overlays page
                                checked: Chiaki.hud.values.cams_show
                                onToggled: Chiaki.hud.values.cams_show = checked
                            }
                        }

                        Item {
                            visible: !Chiaki.settings.coplayCameraSupported
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: !Chiaki.settings.coplayCameraSupported
                            wrapMode: Text.WordWrap
                            color: Material.color(Material.Orange, Material.Shade300)
                            text: qsTr("This build of the app cannot use cameras. You still see the other players' webcams, and the test picture works.")
                        }
                    }

                }

                // ---------------------------------------------------- overlays

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: dialog.page === 2
                    spacing: 14

                    DisplayOptions {
                        Layout.fillWidth: true
                        inSession: false
                    }

                    RowLayout {
                        spacing: 12

                        Label {
                            opacity: 0.7
                            text: qsTr("Port of the OBS overlay:")
                        }

                        C.TextField {
                            Layout.preferredWidth: 120
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
                    }
                }
            }
        }
    }
}
