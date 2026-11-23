import QtQuick
import QtQuick.Layouts

import org.streetpea.chiaking

import "../controls" as C

// Network & invites: how friends reach this computer when you host. On top, how it
// stands right now (meeting point, your address on the internet); then your own address
// and ports, the private invites, and the servers the app asks.
//
// The meeting point and the public rooms are switched off by the word "off" in their
// address setting (coplayMeetServer, coplayRoomsServer; coplayMeetOn / coplayRoomsOn only
// read that). Their switches here write "off", and "" (the default server) when switched
// on again: an address of one's own is not kept over off and on.
C.SettingsPage {
    id: page
    title: qsTr("Network & invites")
    description: qsTr("How friends reach this computer when you host. With most routers nothing here has to change.")

    readonly property int port: Chiaki.settings.coplayPort
    /** 0 not asked yet, 1 looking, 2 known (found, or written down), 3 not found */
    readonly property int lookup: Chiaki.settings.coplayPublicLookupState || 0
    readonly property bool ownAddress: (Chiaki.settings.coplayAddress || "").trim().length > 0
    readonly property bool stunOff: isOff(Chiaki.settings.coplayStunServer)
    /** The meeting point is asked: switched on and its address works. */
    readonly property bool meetOn: !!Chiaki.settings.coplayMeetOn
    readonly property bool meetSwitchedOff: isOff(Chiaki.settings.coplayMeetServer)

    /** Two columns where there is room for both. */
    readonly property bool wide: width >= 740
    readonly property real columnWidth: wide ? Math.floor((width - 20) / 2) : width

    function isOff(setting) {
        return String(setting || "").trim().toLowerCase() === "off";
    }

    // The address on the internet is asked from a server out there: only once somebody
    // looks at this page (all pages are made when the window opens), and again after the
    // server to ask has changed
    function lookUp() {
        if (enabled && lookup === 0 && Chiaki.settings.coplayLookupPublicAddress)
            Chiaki.settings.coplayLookupPublicAddress();
    }
    onEnabledChanged: lookUp()
    Component.onCompleted: lookUp()

    /**
     * Which server a service uses: the default one, or one's own (typed in when wanted).
     * Under the row that names the service; its address folds away while it is the default.
     */
    component ServerRow: C.SettingsRow {
        id: server
        /** The setting as stored: "" is the default server. */
        property string value
        property string defaultTitle
        property string defaultAddress
        property string ownTitle: qsTr("Your own server")
        property string ownHint: defaultAddress
        /** "off" in the setting is shown as such here (for a service without a switch). */
        property bool offHere: false
        property string offTitle
        property string offText
        /** What is wrong with the own address, if anything. */
        property string problem
        property bool editing: false
        readonly property bool off: value.trim().toLowerCase() === "off"
        readonly property bool own: value.trim().length > 0 && (!off || offHere)
        readonly property bool open: editing || own
        signal commit(string text)

        sub: true
        textMinimum: 150
        title: off && offHere ? offTitle : own ? ownTitle : defaultTitle
        subtitle: off && offHere ? offText : open ? problem : defaultAddress

        C.TextLink {
            id: useOwn
            visible: !server.open
            text: qsTr("Use my own…")
            onClicked: {
                server.editing = true;
                field.text = "";
                field.forceActiveFocus(Qt.TabFocusReason);
                field.readOnly = false;
            }
        }

        C.Field {
            id: field
            visible: server.open
            Layout.preferredWidth: 300
            Layout.fillWidth: true
            Accessible.name: server.title
            hint: server.ownHint
            text: server.own ? server.value : ""
            onEditingFinished: server.commit(text.trim())
        }

        C.TextLink {
            visible: server.open
            text: qsTr("Default")
            onClicked: {
                const keys = visualFocus;
                field.text = "";
                server.editing = false;
                server.commit("");
                if (keys)
                    useOwn.forceActiveFocus(Qt.TabFocusReason);
            }
        }
    }

    // ------------------------------------------------ how it stands

    Rectangle {
        id: banner
        Layout.fillWidth: true
        implicitHeight: bannerRow.implicitHeight + 24
        radius: 16
        Accessible.role: Accessible.StaticText
        Accessible.name: bannerTitle.text + " " + bannerText.text

        /** "good", "wait" or "bad" */
        readonly property string tone: {
            if (page.meetOn)
                return page.lookup === 3 && !page.ownAddress ? "wait" : "good";
            if (!page.meetSwitchedOff)
                return "bad";
            return page.ownAddress || page.lookup === 2 || page.lookup === 1 ? "wait" : "bad";
        }
        readonly property color ink: tone === "good" ? C.Theme.good : tone === "wait" ? C.Theme.wait : C.Theme.bad
        readonly property color titleColour: tone === "good" ? "#d7fbe9" : tone === "wait" ? "#fde9c0" : "#ffd3de"
        readonly property color textColour: tone === "good" ? "#b5e8cf" : tone === "wait" ? "#f1d9a4" : "#f3b8c7"

        color: Qt.rgba(ink.r, ink.g, ink.b, 0.10)
        border.width: 1
        border.color: Qt.rgba(ink.r, ink.g, ink.b, 0.40)

        RowLayout {
            id: bannerRow
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
                leftMargin: 16
                rightMargin: 16
            }
            spacing: 12

            Rectangle {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 34
                implicitHeight: 34
                radius: 17
                color: Qt.rgba(banner.ink.r, banner.ink.g, banner.ink.b, 0.18)

                C.Icon {
                    anchors.centerIn: parent
                    iconName: banner.tone === "good" ? "check" : "info"
                    size: 18
                    color: banner.titleColour
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    id: bannerTitle
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: banner.titleColour
                    font.pixelSize: 14
                    font.weight: Font.ExtraBold
                    text: page.meetOn ? qsTr("Friends get in through the meeting point. No port to open.")
                        : !page.meetSwitchedOff ? qsTr("The address of your own meeting point does not work.")
                        : qsTr("The meeting point is off. Friends need a forwarded port.")
                }

                Text {
                    id: bannerText
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    lineHeight: 1.1
                    color: banner.textColour
                    font.pixelSize: 13
                    text: {
                        const direct = qsTr("Picture, sound and controllers go straight between you and your friends.");
                        const forward = qsTr("Forward UDP port %1 to this computer in your router.").arg(page.port);
                        if (page.meetOn) {
                            if (page.ownAddress)
                                return qsTr("Your invites carry the internet address you entered.") + " " + direct;
                            if (page.lookup === 2)
                                return qsTr("Your internet address was found by itself.") + " " + direct;
                            if (page.lookup === 1)
                                return qsTr("Looking for your internet address…");
                            if (page.stunOff)
                                return qsTr("Your internet address is not looked up, so the Internet invite works through the meeting point only.");
                            if (page.lookup === 3)
                                return qsTr("Your internet address could not be found, so the Internet invite works through the meeting point only.");
                            return direct;
                        }
                        if (!page.meetSwitchedOff)
                            return qsTr("No meeting point is asked, so friends need a forwarded port. Check the address under Servers, or go back to the default one.") + " " + forward;
                        if (page.ownAddress || page.lookup === 2)
                            return forward + " " + qsTr("Friends in a web browser and public rooms need the meeting point.");
                        if (page.lookup === 1)
                            return qsTr("Looking for your internet address…");
                        if (page.stunOff)
                            return qsTr("Your internet address is not looked up either (address lookup is off), so friends elsewhere cannot get in. Switch the meeting point on, or enter your own internet address below.");
                        return qsTr("Your internet address could not be found either, so friends elsewhere cannot get in. Switch the meeting point on, or enter your own internet address below.");
                    }
                }
            }

            C.PillButton {
                // Ask again, after a lookup that failed
                visible: page.lookup === 3 && !page.ownAddress && !page.stunOff
                Layout.alignment: Qt.AlignVCenter
                small: true
                iconName: "refresh"
                text: qsTr("Try again")
                onClicked: Chiaki.settings.coplayRefreshPublicAddress()
            }
        }
    }

    // ------------------------------------------------ the two columns

    Item {
        Layout.fillWidth: true
        implicitHeight: page.wide ? Math.max(leftColumn.implicitHeight, serversGroup.implicitHeight)
                                  : leftColumn.implicitHeight + 20 + serversGroup.implicitHeight

        ColumnLayout {
            id: leftColumn
            width: page.columnWidth
            spacing: 18

            C.SettingsGroup {
                title: qsTr("Your connection")

                C.SettingsRow {
                    title: qsTr("Own internet address")
                    subtitle: qsTr("Only for a name of your own, such as myhome.dyndns.org, that stays valid when your address changes.")
                    keywords: qsTr("public ip address dyndns ddns host name invite")
                    textMinimum: 150

                    C.Field {
                        Layout.preferredWidth: 360
                        Layout.fillWidth: true
                        Accessible.name: qsTr("Own internet address")
                        // Streamer mode: hidden unless it is being typed
                        echoMode: Chiaki.settings.streamerMode && readOnly ? TextInput.Password : TextInput.Normal
                        hint: page.lookup === 1 ? qsTr("Looking…")
                            : page.lookup === 2 && Chiaki.settings.coplayPublicAddress && !Chiaki.settings.streamerMode
                              ? qsTr("Found by itself: %1").arg(Chiaki.settings.coplayPublicAddress)
                            : page.lookup === 2 ? qsTr("Found by itself")
                            : page.stunOff ? qsTr("Not looked up: address lookup is off")
                            : page.lookup === 3 ? qsTr("Not found by itself")
                            : qsTr("Found by itself")
                        text: Chiaki.settings.coplayAddress
                        onEditingFinished: Chiaki.settings.coplayAddress = text
                    }
                }

                C.SettingsRow {
                    title: qsTr("UDP port")
                    subtitle: qsTr("Forward it in your router only if a friend cannot connect.")
                    keywords: qsTr("port forwarding router nat firewall sharing")
                    textMinimum: 150

                    C.Field {
                        Layout.preferredWidth: 96
                        Accessible.name: qsTr("UDP port")
                        validator: IntValidator { bottom: 1; top: 65535 }
                        inputMethodHints: Qt.ImhDigitsOnly
                        text: Chiaki.settings.coplayPort
                        onEditingFinished: {
                            if (acceptableInput)
                                Chiaki.settings.coplayPort = parseInt(text);
                            else
                                text = Chiaki.settings.coplayPort;
                        }
                    }
                }

                C.SettingsRow {
                    visible: !!Chiaki.settings.coplayWebSupported
                    title: qsTr("UDP port for browsers")
                    subtitle: qsTr("Friends in a web browser come in on a port of their own. Forward it as well if one cannot connect.")
                    keywords: qsTr("web browser port forwarding router webrtc")
                    textMinimum: 150

                    C.Field {
                        Layout.preferredWidth: 96
                        Accessible.name: qsTr("UDP port for browsers")
                        validator: IntValidator { bottom: 1; top: 65535 }
                        inputMethodHints: Qt.ImhDigitsOnly
                        // Empty is the port after the one above; -1 (any free one) cannot be typed
                        hint: Chiaki.settings.coplayWebPort < 0 ? qsTr("Any") : String(page.port + 1)
                        text: Chiaki.settings.coplayWebPort > 0 ? Chiaki.settings.coplayWebPort : ""
                        onEditingFinished: {
                            if (text.trim().length === 0)
                                Chiaki.settings.coplayWebPort = 0;
                            else if (acceptableInput)
                                Chiaki.settings.coplayWebPort = parseInt(text);
                            else
                                text = Chiaki.settings.coplayWebPort > 0 ? Chiaki.settings.coplayWebPort : "";
                        }
                    }
                }
            }

            C.SettingsGroup {
                title: qsTr("Your private invites")

                C.SettingsRow {
                    title: qsTr("Make new invites")
                    subtitle: qsTr("The ones you handed out before stop working. Copy the new ones on the Host page.")
                    keywords: qsTr("invite key secret reset revoke new link")
                    textMinimum: 150

                    C.PillButton {
                        small: true
                        kind: "danger"
                        text: qsTr("New invites…")
                        onClicked: root.showConfirmDialog(qsTr("New invites"),
                            qsTr("Invites you handed out before stop working. Make new ones?"),
                            () => {
                                // A private session that runs takes the new key at once (the stream menu does the same)
                                const s = Chiaki.session;
                                if (s && s.coplayHosting && !s.coplayPublic)
                                    s.coplayNewInvite();
                                else
                                    Chiaki.settings.coplayNewKey();
                            })
                    }
                }
            }
        }

        C.SettingsGroup {
            id: serversGroup
            x: page.wide ? page.columnWidth + 20 : 0
            y: page.wide ? 0 : leftColumn.implicitHeight + 20
            width: page.columnWidth
            title: qsTr("Servers")

            C.SettingsRow {
                title: qsTr("Meeting point")
                subtitle: meetSwitch.checked
                          ? qsTr("Lets friends in without a forwarded port. Each side leaves a short sealed note with its address there; nothing else passes through it.")
                          : qsTr("Off: friends need a forwarded port, friends in a web browser cannot come, and you cannot join others through it either.")
                keywords: qsTr("rendezvous nat hole punching port forwarding server off")
                switchItem: meetSwitch
                textMinimum: 150

                C.Toggle {
                    id: meetSwitch
                    Accessible.name: qsTr("Meeting point")
                    checked: !page.meetSwitchedOff
                    // "off" goes into the address; an own address waits meanwhile (coplayMeetServerKept)
                    onToggled: Chiaki.settings.coplayMeetServer = checked ? (Chiaki.settings.coplayMeetServerKept || "") : "off"
                }
            }

            ServerRow {
                enabled: meetSwitch.checked
                // Switched off: the address it comes back with
                value: meetSwitch.checked ? (Chiaki.settings.coplayMeetServer || "") : (Chiaki.settings.coplayMeetServerKept || "")
                defaultTitle: qsTr("Default server")
                defaultAddress: Chiaki.settings.coplayMeetDefault || ""
                keywords: qsTr("meeting point server address own")
                problem: own && !page.meetOn ? qsTr("This address does not work, so no meeting point is used.") : ""
                onCommit: (text) => Chiaki.settings.coplayMeetServer = text
            }

            C.SettingsRow {
                title: qsTr("Public rooms")
                subtitle: roomsSwitch.checked
                          ? qsTr("The list under Join and on the Host page. It only lists rooms.")
                          : qsTr("Off: no list of public rooms under Join, and no public room of your own.")
                keywords: qsTr("public rooms list lobby server off")
                switchItem: roomsSwitch
                textMinimum: 150

                C.Toggle {
                    id: roomsSwitch
                    Accessible.name: qsTr("Public rooms")
                    checked: !page.isOff(Chiaki.settings.coplayRoomsServer)
                    onToggled: Chiaki.settings.coplayRoomsServer = checked ? (Chiaki.settings.coplayRoomsServerKept || "") : "off"
                }
            }

            ServerRow {
                enabled: roomsSwitch.checked
                value: roomsSwitch.checked ? (Chiaki.settings.coplayRoomsServer || "") : (Chiaki.settings.coplayRoomsServerKept || "")
                defaultTitle: qsTr("Default server")
                defaultAddress: Chiaki.settings.coplayRoomsDefault || ""
                keywords: qsTr("public rooms server address own")
                problem: own && !Chiaki.settings.coplayRoomsOn
                         ? qsTr("This address does not work. It needs https://, except on this computer.") : ""
                onCommit: (text) => Chiaki.settings.coplayRoomsServer = text
            }

            C.SettingsRow {
                visible: !!Chiaki.settings.coplayWebSupported
                title: qsTr("Page for browsers")
                subtitle: qsTr("Where friends without the app open your link.")
                keywords: qsTr("web browser page link url website")
                textMinimum: 150
            }

            ServerRow {
                visible: !!Chiaki.settings.coplayWebSupported
                value: Chiaki.settings.coplayWebUrl || ""
                defaultTitle: qsTr("Default page")
                defaultAddress: Chiaki.settings.coplayWebDefault || ""
                ownTitle: qsTr("Your own copy of the page")
                keywords: qsTr("web browser page own copy url")
                onCommit: (text) => Chiaki.settings.coplayWebUrl = text
            }

            C.SettingsRow {
                title: qsTr("Address lookup (STUN)")
                subtitle: qsTr("Asked only for your internet address, for the Internet invite.")
                keywords: qsTr("stun server public ip address lookup off")
                textMinimum: 150
            }

            ServerRow {
                value: Chiaki.settings.coplayStunServer || ""
                defaultTitle: qsTr("Google's, then Cloudflare's")
                defaultAddress: "stun.l.google.com:19302, stun.cloudflare.com:3478"
                ownHint: qsTr("host:port, or “off”")
                keywords: qsTr("stun server own host port off never")
                offHere: true
                offTitle: qsTr("Never asked")
                offText: qsTr("Your internet address is not looked up. Enter it yourself, or rely on the meeting point.")
                problem: own && !off && page.lookup === 3
                         ? qsTr("It did not answer: %1").arg(Chiaki.settings.coplayPublicLookupError || qsTr("no reply")) : ""
                onCommit: (text) => {
                    Chiaki.settings.coplayStunServer = text;
                    // What the other server said is forgotten: ask this one
                    page.lookUp();
                }
            }
        }
    }
}
