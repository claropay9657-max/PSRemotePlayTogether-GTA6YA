import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

// Join: a friend's session with the invite they sent, or a public room from the list.
// Either way the stream view takes over as soon as the host has let us in.
DialogView {
    id: dialog
    title: qsTr("Join a session")
    /** 0 = with an invite, 1 = public rooms */
    property int page: 0

    readonly property bool joining: !!Chiaki.coplayJoining
    readonly property bool waiting: !!Chiaki.coplayJoinWaiting
    /** The room being joined from the list, for what is said while waiting. */
    property string joiningTitle: ""

    buttonText: joining ? qsTr("Connecting…") : qsTr("Join")
    buttonEnabled: !joining && page === 0 && inviteField.text.trim().length > 0
    buttonVisible: page === 0
    onAccepted: join(inviteField.text, "")

    /** A small label on a room: its language, how full it is, how one gets in. */
    component Tag: Rectangle {
        property alias text: tagLabel.text
        property color tone: Qt.rgba(1, 1, 1, 0.1)
        radius: 9
        color: tone
        implicitWidth: tagLabel.implicitWidth + 16
        implicitHeight: 22
        Label {
            id: tagLabel
            anchors.centerIn: parent
            font.pixelSize: 12
            font.bold: true
        }
    }

    function join(invite, title) {
        errorLabel.text = "";
        joiningTitle = title;
        Chiaki.joinCoPlay(invite, Chiaki.settings.coplayName, controllerOnlyCheck.checked);
    }

    Item {
        Connections {
            target: Chiaki

            // On success the stream view replaces this page by itself
            function onCoplayJoinFinished(ok, message) {
                if (!ok)
                    errorLabel.text = message;
            }
        }

        // The list stays fresh while it is looked at
        Timer {
            interval: 15000
            repeat: true
            running: dialog.page === 1 && dialog.visible && !dialog.joining
            onTriggered: Chiaki.settings.coplayRefreshRooms()
        }

        Row {
            id: tabs
            anchors {
                top: parent.top
                topMargin: 18
                horizontalCenter: parent.horizontalCenter
            }
            spacing: 8

            Repeater {
                model: [qsTr("With an invite"), qsTr("Public rooms")]

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
                    Keys.onReturnPressed: tab.choose()
                    Keys.onSpacePressed: tab.choose()

                    function choose() {
                        dialog.page = index;
                        if (index === 1)
                            Chiaki.settings.coplayRefreshRooms();
                    }

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
                        onClicked: tab.choose()
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
            ScrollBar.vertical: ScrollBar {
                policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                width: 12
            }

            ColumnLayout {
                id: column
                width: Math.min(flick.width - 80, 900)
                anchors {
                    top: parent.top
                    topMargin: 14
                    horizontalCenter: parent.horizontalCenter
                }
                spacing: 14

                // ---------------------------------------------------- who joins

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 14

                    C.Avatar {
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        source: Chiaki.settings.coplayAvatarUrl
                        name: Chiaki.settings.coplayName || "?"
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 16
                        text: Chiaki.settings.coplayName ? qsTr("You join as <b>%1</b>.").arg(Chiaki.settings.coplayName)
                                                         : qsTr("You have no name yet: the others will see \"Guest\".")
                    }

                    C.Button {
                        text: qsTr("Change…")
                        onClicked: root.showPlayTogetherDialog()
                    }
                }

                C.CheckBox {
                    id: controllerOnlyCheck
                    text: qsTr("With my controller only (no picture or sound: I sit in front of the same TV)")
                    checked: Chiaki.settings.coplayControllerOnly
                }

                // While joining: what happens, and a way out
                Rectangle {
                    Layout.fillWidth: true
                    visible: dialog.joining
                    implicitHeight: joiningRow.implicitHeight + 24
                    radius: 12
                    color: Qt.rgba(140 / 255, 82 / 255, 1, 0.14)
                    border.color: Qt.rgba(140 / 255, 82 / 255, 1, 0.5)

                    RowLayout {
                        id: joiningRow
                        anchors {
                            fill: parent
                            margins: 12
                        }
                        spacing: 12

                        BusyIndicator {
                            Layout.preferredWidth: 32
                            Layout.preferredHeight: 32
                            running: dialog.joining
                        }

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 15
                            text: dialog.waiting
                                  ? (dialog.joiningTitle ? qsTr("Waiting for the host of \"%1\" to let you in…").arg(dialog.joiningTitle)
                                                         : qsTr("Waiting for the host to let you in…"))
                                  : qsTr("Looking for the host…")
                        }

                        C.Button {
                            text: qsTr("Cancel")
                            onClicked: Chiaki.coplayCancelJoin()
                        }
                    }
                }

                Label {
                    id: errorLabel
                    Layout.fillWidth: true
                    visible: text.length > 0
                    wrapMode: Text.WordWrap
                    color: Material.color(Material.Red, Material.Shade300)
                }

                // ---------------------------------------------------- with an invite

                ColumnLayout {
                    Layout.fillWidth: true
                    visible: dialog.page === 0
                    spacing: 12

                    C.GradientText {
                        Layout.topMargin: 8
                        text: qsTr("Invite")
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

                    C.TextField {
                        id: inviteField
                        Layout.fillWidth: true
                        placeholderText: "psrpt://…"
                        text: Chiaki.settings.coplayLastInvite
                        echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                        onAccepted: if (dialog.buttonEnabled) dialog.join(text, "")
                    }
                }

                // ---------------------------------------------------- public rooms

                ColumnLayout {
                    id: rooms
                    Layout.fillWidth: true
                    visible: dialog.page === 1
                    spacing: 12

                    readonly property var all: Chiaki.settings.coplayRooms || []
                    readonly property int state: Chiaki.settings.coplayRoomsState
                    property string search: ""
                    property string language: ""
                    property bool hideFull: false
                    /** Only rooms one gets into without waiting for the host. */
                    property bool openOnly: false
                    /** 0 most players, 1 most free places, 2 newest, 3 open longest */
                    property int sort: 0
                    readonly property var shown: {
                        // Every word has to be found in the title, the game, the host or the language
                        const words = search.toLowerCase().split(/\s+/).filter((w) => w.length > 0);
                        let list = all.filter((r) => {
                            if (language && r.lang && r.lang !== language)
                                return false;
                            if (hideFull && r.full)
                                return false;
                            if (openOnly && r.policy === "ask")
                                return false;
                            const text = (r.title + " " + r.game + " " + r.host + " " + languageName(r.lang)).toLowerCase();
                            return words.every((w) => text.indexOf(w) >= 0);
                        });
                        const by = [
                            (a, b) => b.players - a.players,
                            (a, b) => (b.max - b.players) - (a.max - a.players),
                            (a, b) => a.since - b.since,
                            (a, b) => b.since - a.since,
                        ][sort];
                        // Full rooms last, whatever the order
                        return list.sort((a, b) => (a.full === b.full ? by(a, b) : (a.full ? 1 : -1)));
                    }

                    function languageName(code) {
                        const list = Chiaki.settings.coplayLanguages;
                        for (let i = 0; i < list.length; ++i) {
                            if (list[i][0] === code)
                                return list[i][1];
                        }
                        return code ? code.toUpperCase() : qsTr("Any language");
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        spacing: 14

                        C.GradientText {
                            text: qsTr("Public rooms")
                            font.bold: true
                            font.pixelSize: 24
                            to: "#7c86ff"
                        }

                        Label {
                            visible: rooms.state === 2
                            opacity: 0.6
                            text: rooms.shown.length === rooms.all.length ? qsTr("%n room(s)", "", rooms.all.length)
                                                                          : qsTr("%1 of %2").arg(rooms.shown.length).arg(rooms.all.length)
                        }

                        Item { Layout.fillWidth: true }

                        C.Button {
                            text: rooms.state === 1 ? qsTr("Loading…") : qsTr("Refresh")
                            enabled: rooms.state !== 1
                            onClicked: Chiaki.settings.coplayRefreshRooms()
                        }
                    }

                    // Finding a room: words, language, how to get in, order
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Search: title, game or host")
                            selectByMouse: true
                            onTextChanged: rooms.search = text
                            Keys.onEscapePressed: (event) => {
                                if (text.length > 0) {
                                    text = "";
                                    event.accepted = true;
                                } else {
                                    event.accepted = false;
                                }
                            }
                        }

                        C.ComboBox {
                            Layout.preferredWidth: 200
                            readonly property var codes: [""].concat(Chiaki.settings.coplayLanguages.map((l) => l[0]))
                            model: [qsTr("All languages")].concat(Chiaki.settings.coplayLanguages.map((l) => l[1]))
                            onActivated: (index) => rooms.language = codes[index]
                        }

                        C.ComboBox {
                            Layout.preferredWidth: 220
                            model: [qsTr("Most players"), qsTr("Most free places"), qsTr("Newest"), qsTr("Open longest")]
                            currentIndex: rooms.sort
                            onActivated: (index) => rooms.sort = index
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 20

                        C.CheckBox {
                            text: qsTr("Hide full rooms")
                            checked: rooms.hideFull
                            onToggled: rooms.hideFull = checked
                        }

                        C.CheckBox {
                            text: qsTr("Only rooms I get into right away")
                            checked: rooms.openOnly
                            onToggled: rooms.openOnly = checked
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        visible: rooms.state !== 4
                        text: qsTr("Sessions their hosts want to be found. Join one like with an invite: you connect to the host directly. Some hosts let each player in themselves, others let you watch first.")
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: rooms.state === 4
                        wrapMode: Text.WordWrap
                        color: "#ffb74d"
                        text: qsTr("Public rooms are switched off in the Play Together settings (Network).")
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: rooms.state === 3
                        wrapMode: Text.WordWrap
                        color: "#ffb74d"
                        text: qsTr("The list of public rooms could not be read: %1").arg(Chiaki.settings.coplayRoomsError || "")
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.topMargin: 20
                        visible: rooms.state === 2 && rooms.shown.length === 0
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                        font.pixelSize: 16
                        text: rooms.all.length === 0 ? qsTr("No public rooms right now. Host one yourself: choose Public on the Host page.")
                                                     : qsTr("No room matches. Try other words, another language, or show all rooms.")
                    }

                    Repeater {
                        model: rooms.shown

                        Rectangle {
                            id: roomCard
                            required property var modelData
                            readonly property bool joinable: !modelData.full && modelData.compatible
                            Layout.fillWidth: true
                            implicitHeight: roomRow.implicitHeight + 28
                            radius: 14
                            color: Qt.rgba(1, 1, 1, cardArea.containsMouse ? 0.075 : 0.045)
                            border.width: 1
                            border.color: Qt.rgba(1, 1, 1, 0.09)

                            MouseArea {
                                id: cardArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.NoButton
                            }

                            RowLayout {
                                id: roomRow
                                anchors {
                                    fill: parent
                                    margins: 14
                                    leftMargin: 18
                                }
                                spacing: 16

                                // The game's picture where the host's app found one, otherwise its initial
                                Rectangle {
                                    Layout.preferredWidth: 72
                                    Layout.preferredHeight: 72
                                    Layout.alignment: Qt.AlignVCenter
                                    radius: 12
                                    clip: true
                                    gradient: Gradient {
                                        GradientStop { position: 0.0; color: "#8c52ff" }
                                        GradientStop { position: 1.0; color: "#f0469b" }
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        visible: cover.status !== Image.Ready
                                        font.pixelSize: 30
                                        font.bold: true
                                        text: (roomCard.modelData.game || roomCard.modelData.title || "?").trim().charAt(0).toUpperCase()
                                    }

                                    Image {
                                        id: cover
                                        anchors.fill: parent
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        sourceSize.width: 144
                                        sourceSize.height: 144
                                        // Sony's image server makes a small one
                                        source: roomCard.modelData.cover ? roomCard.modelData.cover + "?w=192" : ""
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.pixelSize: 18
                                        font.bold: true
                                        text: roomCard.modelData.title
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        opacity: 0.8
                                        font.pixelSize: 14
                                        text: roomCard.modelData.game
                                              ? qsTr("%1 plays %2").arg(roomCard.modelData.host).arg(roomCard.modelData.game)
                                              : qsTr("Hosted by %1").arg(roomCard.modelData.host)
                                    }

                                    Row {
                                        spacing: 8

                                        Tag {
                                            text: rooms.languageName(roomCard.modelData.lang)
                                        }
                                        Tag {
                                            text: qsTr("%1 of %2 players").arg(roomCard.modelData.players).arg(roomCard.modelData.max)
                                            tone: roomCard.modelData.full ? Qt.rgba(0.94, 0.27, 0.43, 0.45) : Qt.rgba(1, 1, 1, 0.1)
                                        }
                                        Tag {
                                            text: roomCard.modelData.control === "turns" ? qsTr("One plays at a time")
                                                : roomCard.modelData.control === "host" ? qsTr("Only the host plays")
                                                : qsTr("Everybody plays at once")
                                            tone: Qt.rgba(0, 0.95, 1, 0.22)
                                        }
                                        Tag {
                                            text: roomCard.modelData.policy === "ask" ? qsTr("Host lets you in")
                                                : roomCard.modelData.policy === "watch" ? qsTr("Watch first") : qsTr("Open")
                                            tone: Qt.rgba(140 / 255, 82 / 255, 1, 0.35)
                                        }
                                        Tag {
                                            visible: !!roomCard.modelData.web
                                            text: qsTr("Browsers welcome")
                                            tone: Qt.rgba(0.2, 0.83, 0.6, 0.35)
                                        }
                                    }
                                }

                                ColumnLayout {
                                    spacing: 4

                                    C.Button {
                                        Layout.alignment: Qt.AlignRight
                                        text: qsTr("Join")
                                        highlighted: true
                                        enabled: roomCard.joinable && !dialog.joining
                                        onClicked: dialog.join(roomCard.modelData.invite, roomCard.modelData.title)
                                    }

                                    Label {
                                        Layout.alignment: Qt.AlignRight
                                        visible: !roomCard.joinable
                                        opacity: 0.7
                                        font.pixelSize: 12
                                        text: !roomCard.modelData.compatible ? qsTr("Needs another version of the app") : qsTr("Full")
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
