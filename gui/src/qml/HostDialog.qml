import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

// Host: everything about a session friends join, set up before it starts. Private:
// only friends with the invite get in. Public: the room is listed for everybody,
// with a title, a language and the game, and the host decides who gets in.
// "Start" connects to the console with all of this; "Play" on the start page plays
// without anybody.
DialogView {
    id: dialog
    title: qsTr("Host a session")

    /** Registered consoles that can be played: index into Chiaki.hosts, name, state. */
    readonly property var consoles: {
        let list = [];
        for (let i = 0; i < Chiaki.hosts.length; ++i) {
            const h = Chiaki.hosts[i];
            if (h.display && h.registered)
                list.push({ index: i, name: h.name, discovered: h.discovered, state: h.state, app: h.app || "", psn: !!h.duid });
        }
        return list;
    }
    property int consoleChoice: 0
    readonly property var chosen: consoles.length > 0 ? consoles[Math.min(consoleChoice, consoles.length - 1)] : null
    readonly property bool resting: !!chosen && chosen.state === "standby"
    readonly property bool publicRoom: !!Chiaki.settings.coplayHostPublic
    readonly property bool roomsOn: !!Chiaki.settings.coplayRoomsOn && !!Chiaki.settings.coplayMeetOn

    buttonText: resting ? qsTr("Wake up & start") : qsTr("Start")
    buttonEnabled: !!chosen && !(publicRoom && !roomsOn)
    onAccepted: {
        if (!chosen)
            return;
        // Whatever was typed last counts, also without pressing Enter
        Chiaki.settings.coplayRoomTitle = titleField.text;
        Chiaki.hostConsole(chosen.index, chosen.discovered ? chosen.name : "");
    }

    Item {
        Flickable {
            id: flick
            anchors {
                fill: parent
                topMargin: 10
            }
            contentWidth: width
            contentHeight: column.implicitHeight + 60
            clip: true
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                width: 12
            }

            ColumnLayout {
                id: column
                width: Math.min(flick.width - 80, 860)
                anchors {
                    top: parent.top
                    topMargin: 14
                    horizontalCenter: parent.horizontalCenter
                }
                spacing: 14

                // ---------------------------------------------------- console

                C.GradientText {
                    text: qsTr("Console")
                    font.bold: true
                    font.pixelSize: 24
                    to: "#7c86ff"
                }

                Label {
                    Layout.fillWidth: true
                    visible: dialog.consoles.length === 0
                    wrapMode: Text.WordWrap
                    color: "#ffb74d"
                    text: qsTr("No registered console yet. Add yours on the start page first (\"Add console\"), then come back here to host.")
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: dialog.consoles.length > 0
                    spacing: 16

                    C.ComboBox {
                        Layout.preferredWidth: 360
                        visible: dialog.consoles.length > 1
                        model: dialog.consoles.map((c) => c.name)
                        currentIndex: dialog.consoleChoice
                        onActivated: (index) => dialog.consoleChoice = index
                    }

                    Label {
                        visible: dialog.consoles.length === 1
                        font.pixelSize: 18
                        font.bold: true
                        text: dialog.chosen ? dialog.chosen.name : ""
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        text: {
                            const c = dialog.chosen;
                            if (!c)
                                return "";
                            if (c.state === "standby")
                                return qsTr("In rest mode: it is woken up when you start.");
                            if (c.app)
                                return qsTr("Running: %1").arg(c.app);
                            if (c.psn && !c.discovered)
                                return qsTr("Over the internet (PSN).");
                            return c.discovered ? qsTr("Ready.") : "";
                        }
                    }
                }

                // ---------------------------------------------------- who can join

                C.GradientText {
                    Layout.topMargin: 12
                    text: qsTr("Who can join")
                    font.bold: true
                    font.pixelSize: 24
                    to: "#7c86ff"
                }

                C.RadioButton {
                    text: qsTr("Private: only friends I send my invite to")
                    checked: !dialog.publicRoom
                    onToggled: if (checked) Chiaki.settings.coplayHostPublic = false
                }

                C.RadioButton {
                    text: qsTr("Public: listed for everybody in Join → Public rooms")
                    checked: dialog.publicRoom
                    onToggled: if (checked) Chiaki.settings.coplayHostPublic = true
                }

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 40
                    visible: dialog.publicRoom && !dialog.roomsOn
                    wrapMode: Text.WordWrap
                    color: "#ffb74d"
                    text: !Chiaki.settings.coplayRoomsOn
                          ? qsTr("Public rooms are switched off in the Play Together settings (Network).")
                          : qsTr("A public room needs the meeting point, which is switched off in the Play Together settings (Network).")
                }

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: 40
                    visible: dialog.publicRoom && dialog.roomsOn
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: qsTr("Players connect to your computer directly, as with every invite, and so learn its internet address. In a public room that can be anybody who reads the list. Your private invites stay private.")
                }

                GridLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 40
                    visible: dialog.publicRoom
                    columns: 2
                    rowSpacing: 12
                    columnSpacing: 20

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Title:")
                    }

                    C.TextField {
                        id: titleField
                        Layout.fillWidth: true
                        maximumLength: 60
                        placeholderText: qsTr("%1's room").arg(Chiaki.settings.coplayName || qsTr("Host"))
                        text: Chiaki.settings.coplayRoomTitle
                        onEditingFinished: Chiaki.settings.coplayRoomTitle = text
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Language:")
                    }

                    C.ComboBox {
                        id: languageCombo
                        Layout.preferredWidth: 300
                        // The first entry is "any": the room does not say
                        readonly property var codes: [""].concat(Chiaki.settings.coplayLanguages.map((l) => l[0]))
                        model: [qsTr("Any language")].concat(Chiaki.settings.coplayLanguages.map((l) => l[1]))
                        currentIndex: Math.max(0, codes.indexOf(Chiaki.settings.coplayRoomLanguage))
                        onActivated: (index) => Chiaki.settings.coplayRoomLanguage = codes[index]
                    }

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Who gets in:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 520
                        model: [
                            qsTr("I let each player in"),
                            qsTr("Everybody, watching until I let them play"),
                            qsTr("Everybody, playing at once"),
                        ]
                        currentIndex: Chiaki.settings.coplayJoinPolicy
                        onActivated: (index) => Chiaki.settings.coplayJoinPolicy = index
                    }

                    Item { }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        text: qsTr("The list shows the title, the language, the game your console runs and how many play. It names no address: players find you through the meeting point. A public room has an invite of its own, which changes when the room closes, so that strangers cannot come back later; your private invite does not lead into it. While playing, the menu shows who knocks, and lets you close the room to newcomers and remove and block a player.")
                    }
                }

                // ---------------------------------------------------- how it is played

                C.GradientText {
                    Layout.topMargin: 12
                    text: qsTr("Players")
                    font.bold: true
                    font.pixelSize: 24
                    to: "#7c86ff"
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    rowSpacing: 12
                    columnSpacing: 20

                    Label {
                        Layout.alignment: Qt.AlignRight
                        text: qsTr("Friends:")
                    }

                    C.ComboBox {
                        Layout.preferredWidth: 220
                        model: [1, 2, 3, 4, 5, 6, 7, 8].map((n) => n === 1 ? qsTr("1 friend") : qsTr("Up to %1 friends").arg(n))
                        currentIndex: Chiaki.settings.coplayMaxGuests - 1
                        onActivated: (index) => Chiaki.settings.coplayMaxGuests = index + 1
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
                }

                C.CheckBox {
                    visible: !!Chiaki.settings.coplayWebSupported
                    text: qsTr("Friends without the app can join from a web browser")
                    checked: !!Chiaki.settings.coplayWebGuests
                    onToggled: Chiaki.settings.coplayWebGuests = checked
                }

                C.CheckBox {
                    text: qsTr("Guests may put the console to sleep when they leave")
                    checked: !!Chiaki.settings.coplayGuestsMaySleep
                    onToggled: Chiaki.settings.coplayGuestsMaySleep = checked
                }

                // The same setting as "Picture and sound" in the settings, where the two can
                // also be switched off one by one
                C.CheckBox {
                    text: qsTr("I play on the TV: no picture or sound on this computer")
                    checked: (Chiaki.settings.audioVideoDisabled & 0x02) !== 0
                    onToggled: Chiaki.settings.audioVideoDisabled = checked ? 3 : 0
                }

                // ---------------------------------------------------- invites

                C.GradientText {
                    Layout.topMargin: 12
                    text: qsTr("Invite")
                    font.bold: true
                    font.pixelSize: 24
                    to: "#7c86ff"
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    text: dialog.publicRoom
                          ? qsTr("Players find your room in the list. To send friends straight to it, copy its invite. It is new for every public session.")
                          : qsTr("Send one of these to your friends. They work every time you host privately, until you make a new one in the Play Together settings.")
                }

                ColumnLayout {
                    id: invites
                    Layout.fillWidth: true
                    spacing: 8

                    readonly property int port: Chiaki.settings.coplayPort
                    /** 0 nothing asked yet, 1 looking, 2 found, 3 not found */
                    readonly property int lookup: Chiaki.settings.coplayPublicLookupState || 0
                    readonly property string publicInvite: Chiaki.settings.coplayInvitePublic || ""
                    readonly property string ipv6Invite: Chiaki.settings.coplayInviteIPv6 || ""

                    // The address on the internet is asked from a server out there: only now
                    // that somebody wants an invite
                    Component.onCompleted: {
                        if (lookup === 0 && Chiaki.settings.coplayLookupPublicAddress)
                            Chiaki.settings.coplayLookupPublicAddress();
                    }

                    C.InviteRow {
                        title: qsTr("Internet")
                        secret: Chiaki.settings.streamerMode
                        invite: invites.publicInvite
                        busy: invites.lookup === 1
                        actionText: invites.lookup === 3 ? qsTr("Try again") : ""
                        onAction: Chiaki.settings.coplayLookupPublicAddress()
                        note: {
                            const meet = !!Chiaki.settings.coplayMeetOn;
                            if (meet && invites.lookup === 1)
                                return qsTr("Finding your public address…");
                            if (meet && invites.publicInvite.length > 0) {
                                return qsTr("For friends anywhere. With most routers they get in by themselves, through the meeting point. If a friend cannot connect, forward UDP port %1 to this computer in your router.").arg(invites.port)
                                    + (invites.lookup === 3 ? " " + qsTr("Your public address could not be found, so this invite works through the meeting point only.") : "");
                            }
                            if (invites.publicInvite.length > 0)
                                return qsTr("For friends anywhere. UDP port %1 has to reach this computer: forward it in your router.").arg(invites.port);
                            if (invites.lookup === 1)
                                return qsTr("Finding your public address…");
                            if (invites.lookup === 3)
                                return qsTr("Your public address could not be found. Check the internet connection, or enter your address in the Play Together settings.");
                            return qsTr("For friends anywhere.");
                        }
                    }

                    C.InviteRow {
                        title: qsTr("Same network")
                        dot: "#00f2ff"
                        secret: Chiaki.settings.streamerMode
                        invite: Chiaki.settings.coplayInviteLan || Chiaki.settings.coplayInvite
                        note: qsTr("For friends in your home network or in the same VPN. Works right away.")
                    }

                    C.InviteRow {
                        visible: invites.ipv6Invite.length > 0
                        title: qsTr("Internet, IPv6")
                        dot: "#f0469b"
                        secret: Chiaki.settings.streamerMode
                        invite: invites.ipv6Invite
                        note: qsTr("Try this one if the other internet invite does not work: many connections share their IPv4 address with others and can only be reached over IPv6. Your friend needs IPv6 too, and your router has to let UDP port %1 through.").arg(invites.port)
                    }

                    C.InviteRow {
                        visible: !!Chiaki.settings.coplayWebSupported && !!Chiaki.settings.coplayWebGuests
                        title: qsTr("Web browser")
                        dot: "#34d399"
                        secret: Chiaki.settings.streamerMode
                        secretText: "https://••••••••••••••••"
                        invite: Chiaki.settings.coplayInviteWeb || ""
                        note: !Chiaki.settings.coplayMeetOn
                              ? qsTr("Friends without the app could join from a web browser, but the meeting point that leads a browser to you is switched off in the Play Together settings.")
                              : qsTr("For friends without the app: they open this link in Chrome, Edge, Firefox or Safari, plug in a controller and play along.")
                    }
                }

                C.Button {
                    Layout.topMargin: 8
                    text: qsTr("Profile & network settings…")
                    onClicked: root.showPlayTogetherDialog()
                }
            }
        }
    }
}
