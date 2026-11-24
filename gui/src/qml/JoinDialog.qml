import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects
import QtQuick.Shapes

import org.streetpea.chiaking

import "controls" as C

// Join: a friend's session with the invite they sent, or a public room from the list,
// both on one page: the invite sits in the title bar like an address, the rooms below.
// Either way the stream view takes over as soon as the host has let us in.
FocusScope {
    id: dialog
    property string title: qsTr("Join a session")
    /** Where the page starts: 0 = at the invite, 1 = at the public rooms (in the search). */
    property int page: 0
    property Item restoreFocusItem

    readonly property bool joining: !!Chiaki.coplayJoining
    readonly property bool waiting: !!Chiaki.coplayJoinWaiting
    /** The room being joined from the list, for what is said while waiting. */
    property string joiningTitle: ""
    /** The invite of the room being joined from the list; empty when joined with an invite. */
    property string joiningRoom: ""
    /** The key of the session being joined again from the recent ones; empty otherwise. */
    property string joiningRecent: ""
    /** An invite to join at once when the page opens: a friend's session (Main.qml joinFriendRoom). */
    property string startInvite: ""
    property string startTitle: ""
    /** The room's own card says that we wait; otherwise it is said under the invite. */
    readonly property bool joiningShown: joiningRoom.length > 0 && rooms.shown.some((r) => r.invite === joiningRoom)
    readonly property bool canJoinInvite: !joining && inviteField.text.trim().length > 0

    /** When the list was last read while this page is open (0: not yet), for "Updated …". */
    property double roomsUpdatedAt: 0
    property double now: Date.now()

    // Where the keyboard or controller was, to find the place again when what had the
    // focus goes away: the list read anew, a Join button resting, a Cancel no longer needed
    property string focusedRoom: ""
    property string focusedPlace: ""
    property int focusedReason: Qt.TabFocusReason
    property bool restoringFocus: false

    // Two columns from about 900 px; below, everything stacks and scrolls as one
    readonly property bool wide: width >= 900
    readonly property bool narrowHeader: width < 760
    readonly property int pad: width < 600 ? 16 : 32
    readonly property int topPad: 20
    readonly property int bottomPad: 24
    readonly property real innerWidth: Math.max(0, Math.min(width - 2 * pad, 1400 - 64))
    readonly property real contentLeft: (width - innerWidth) / 2
    readonly property real asideWidth: Math.max(260, Math.min(300, innerWidth * 0.26))
    /** The finding and the profile stay in view while the list scrolls past them. */
    readonly property bool stickyAside: wide && aside.implicitHeight + topPad + bottomPad <= flick.height

    function close() {
        root.closeDialog();
    }

    function join(invite, title, fromList = false) {
        errorLabel.text = "";
        joiningTitle = title;
        joiningRoom = fromList ? invite : "";
        joiningRecent = "";
        Chiaki.joinCoPlay(invite, Chiaki.settings.coplayName, controllerOnlyCheck.checked);
    }

    /** One of the sessions joined before, with the invite it had then. */
    function joinRecent(entry) {
        join(entry.invite, entry.title || "");
        joiningRecent = entry.key;
    }

    function focusStart(reason = Qt.TabFocusReason) {
        if (page === 1) {
            flick.contentY = 0;
            searchField.forceActiveFocus(reason);
        } else {
            inviteField.forceActiveFocus(reason);
        }
    }

    function isInside(item, ancestor) {
        for (let p = item; p; p = p.parent) {
            if (p === ancestor)
                return true;
        }
        return false;
    }

    /** What had the focus is gone or resting (the focus fell back onto the page), or hidden. */
    function focusLost() {
        const item = dialog.Window.activeFocusItem;
        if (!item)
            return false;
        return item === dialog || item === flick || item === flick.contentItem || (!item.visible && isInside(item, dialog));
    }

    /** Gives the focus to a room's card, found by its invite. */
    function focusRoom(key, reason) {
        for (let i = 0; i < roomRepeater.count; ++i) {
            const card = roomRepeater.itemAt(i);
            if (card && card.modelData.invite === key)
                return card.takeFocus(reason);
        }
        return false;
    }

    function recoverFocus() {
        if (!focusLost() || !visible || dialog.StackView.status !== StackView.Active)
            return;
        restoringFocus = true;
        let done = false;
        if (joining) {
            // Its Join button rests while joining: on to the way out
            if (inviteWaiting.visible) {
                inviteCancel.forceActiveFocus(focusedReason);
                done = true;
            } else if (joiningRoom.length > 0) {
                done = focusRoom(joiningRoom, focusedReason);
            }
        } else if (focusedPlace === "room") {
            done = focusRoom(focusedRoom, focusedReason);
        }
        if (!done) {
            if (focusedPlace === "header")
                inviteField.forceActiveFocus(focusedReason);
            else
                searchField.forceActiveFocus(focusedReason);
        }
        restoringFocus = false;
    }

    function scrollTo(item) {
        const top = item.mapToItem(flick.contentItem, 0, 0).y - 40;
        const bottom = top + item.height + 80;
        const max = Math.max(0, flick.contentHeight - flick.height);
        if (top < flick.contentY)
            flick.contentY = Math.max(0, top);
        else if (bottom > flick.contentY + flick.height)
            flick.contentY = Math.min(max, bottom - flick.height);
    }

    /** Every room again: no words, any language, full ones and those that ask. */
    function showAllRooms() {
        searchField.text = "";
        languageCombo.currentIndex = 0;
        rooms.language = "";
        consoleCombo.currentIndex = 0;
        rooms.system = "";
        rooms.hideFull = false;
        rooms.openOnly = false;
        rooms.twitchOnly = false;
    }

    function openFor(seconds) {
        const minutes = Math.floor(seconds / 60);
        if (minutes < 1)
            return qsTr("just opened");
        if (minutes < 60)
            return qsTr("open for %1 min").arg(minutes);
        const hours = Math.floor(minutes / 60);
        return minutes % 60 ? qsTr("open for %1 h %2 min").arg(hours).arg(minutes % 60)
                            : qsTr("open for %1 h").arg(hours);
    }

    function updatedText() {
        const minutes = Math.floor((now - roomsUpdatedAt) / 60000);
        return minutes < 1 ? qsTr("Updated just now") : qsTr("Updated %n min ago", "", minutes);
    }

    Keys.onEscapePressed: close()

    Keys.onMenuPressed: {
        if (canJoinInvite)
            join(inviteField.text, "");
    }

    // Nothing has the focus: Up or Down brings it back to where the page starts
    Keys.onPressed: (event) => {
        if ((event.key === Qt.Key_Up || event.key === Qt.Key_Down) && focusLost()) {
            focusStart();
            event.accepted = true;
        }
    }

    StackView.onDeactivating: {
        restoreFocusItem = Window.window.activeFocusItem;
    }

    StackView.onActivated: {
        if (!restoreFocusItem) {
            focusStart();
        } else {
            restoreFocusItem.forceActiveFocus(Qt.TabFocusReason);
            restoreFocusItem = null;
            // Back from the settings, where the list may have been switched on or off
            if (!joining)
                Chiaki.settings.coplayRefreshRooms();
        }
    }

    // The rooms are on this page whatever it opens with (the start page reads them itself
    // when it opens the page at the public rooms)
    Component.onCompleted: {
        if (page !== 1)
            Chiaki.settings.coplayRefreshRooms();
        // A friend's session: the invite shows in the field, and joining starts at once
        if (startInvite.length > 0) {
            inviteField.text = startInvite;
            Qt.callLater(() => dialog.join(startInvite, startTitle));
        }
    }

    Connections {
        target: Chiaki

        // On success the stream view replaces this page by itself
        function onCoplayJoinFinished(ok, message) {
            if (!ok)
                errorLabel.text = message;
        }
    }

    Connections {
        target: Chiaki.settings

        function onCoplayRoomsChanged() {
            if (Chiaki.settings.coplayRoomsState === 2) {
                dialog.now = Date.now();
                dialog.roomsUpdatedAt = dialog.now;
            }
        }
    }

    // Keyboard and controller: whatever gets the focus is scrolled into view, and found
    // again when it goes away
    Connections {
        target: dialog.Window.window

        function onActiveFocusItemChanged() {
            if (dialog.focusLost()) {
                if (dialog.visible)
                    Qt.callLater(dialog.recoverFocus);
                return;
            }
            const item = dialog.Window.activeFocusItem;
            if (!item || !dialog.isInside(item, dialog))
                return;
            dialog.focusedReason = item.focusReason !== undefined ? item.focusReason : Qt.TabFocusReason;
            dialog.focusedRoom = item.roomKey !== undefined ? item.roomKey : "";
            dialog.focusedPlace = dialog.isInside(item, titleBar) ? "header" : (dialog.focusedRoom.length > 0 ? "room" : "page");
            if (dialog.restoringFocus || !dialog.isInside(item, flick.contentItem))
                return;
            if (dialog.stickyAside && dialog.isInside(item, aside))
                return;
            dialog.scrollTo(item);
        }
    }

    // The list stays fresh while it is looked at
    Timer {
        interval: 15000
        repeat: true
        running: dialog.visible && !dialog.joining
        onTriggered: Chiaki.settings.coplayRefreshRooms()
    }

    // Whether the hosts of the sessions joined before are sharing now: asked while the list is seen
    Timer {
        interval: 15000
        repeat: true
        triggeredOnStart: true
        running: dialog.visible && !dialog.joining && recent.list.length > 0
        onTriggered: Chiaki.settings.coplayLookRecent()
    }

    // "Updated … min ago" moves on by itself
    Timer {
        interval: 20000
        repeat: true
        running: dialog.visible && (dialog.roomsUpdatedAt > 0 || recent.list.length > 0)
        onTriggered: dialog.now = Date.now()
    }

    // The sessions joined before
    QtObject {
        id: recent

        readonly property var list: Chiaki.settings.coplayRecent || []

        function joinedText(ms) {
            if (!(ms > 0))
                return qsTr("Joined before");
            const minutes = Math.floor((dialog.now - ms) / 60000);
            if (minutes < 1)
                return qsTr("Joined just now");
            if (minutes < 60)
                return qsTr("Joined %1 min ago").arg(minutes);
            const hours = Math.floor(minutes / 60);
            if (hours < 24)
                return qsTr("Joined %1 h ago").arg(hours);
            const days = Math.floor(hours / 24);
            if (days === 1)
                return qsTr("Joined yesterday");
            if (days < 30)
                return qsTr("Joined %1 days ago").arg(days);
            return qsTr("Joined on %1").arg(Qt.formatDate(new Date(ms), Locale.ShortFormat));
        }
    }

    // What the list shows: every filter and the order
    QtObject {
        id: rooms

        readonly property var all: Chiaki.settings.coplayRooms || []
        readonly property int state: Chiaki.settings.coplayRoomsState
        /** There is a list to show: read, or being read again. */
        readonly property bool listed: state === 2 || (state === 1 && dialog.roomsUpdatedAt > 0)
        property string search: ""
        property string language: ""
        /** Only rooms whose game runs on this ("ps5", "ps4", "pc", "xbox"); empty for all. */
        property string system: ""
        property bool hideFull: false
        /** Only rooms one gets into without waiting for the host. */
        property bool openOnly: false
        /** Only rooms whose host is live on Twitch right now. */
        property bool twitchOnly: false
        /** 0 most players, 1 most free places, 2 newest, 3 open longest */
        property int sort: 0
        readonly property var shown: {
            // Every word has to be found in the title, the game, the host or the language
            const words = search.toLowerCase().split(/\s+/).filter((w) => w.length > 0);
            let list = all.filter((r) => {
                if (language && r.lang && r.lang !== language)
                    return false;
                if (system && r.console !== system)
                    return false;
                if (hideFull && r.full)
                    return false;
                if (openOnly && r.policy === "ask")
                    return false;
                if (twitchOnly && !(r.twitch && r.twitch.length))
                    return false;
                const text = (r.title + " " + r.game + " " + r.host + " " + languageName(r.lang) + " " + consoleName(r.console)).toLowerCase();
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

        /** What a room's game runs on, as its badge says; empty from an older host. */
        function consoleName(kind) {
            return ({ ps5: "PS5", ps4: "PS4", pc: qsTr("PC"), xbox: "Xbox" })[kind] || "";
        }

        function languageName(code) {
            const list = Chiaki.settings.coplayLanguages;
            for (let i = 0; i < list.length; ++i) {
                if (list[i][0] === code)
                    return list[i][1];
            }
            return code ? code.toUpperCase() : qsTr("Any language");
        }
    }

    /** One of the orders of the list: a row with a round mark. */
    component Choice: AbstractButton {
        id: choice
        property bool chosen: false
        property bool firstInFocusChain: false
        property bool lastInFocusChain: false

        Layout.fillWidth: true
        implicitHeight: 34
        leftPadding: 10
        rightPadding: 10
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        font.pixelSize: 13
        font.weight: Font.Bold
        Accessible.role: Accessible.RadioButton
        Accessible.checked: chosen
        Accessible.name: text

        Keys.onPressed: (event) => C.FocusChain.handle(choice, event)

        background: Rectangle {
            radius: 10
            color: choice.chosen ? Qt.rgba(140 / 255, 82 / 255, 1, 0.16) : Qt.rgba(1, 1, 1, choice.hovered ? 0.05 : 0)
            border.width: choice.visualFocus ? 2 : 0
            border.color: C.Theme.focus
        }

        contentItem: RowLayout {
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                radius: 8
                color: "transparent"
                border.width: 2
                border.color: choice.chosen ? "#a77bff" : Qt.rgba(1, 1, 1, 0.3)

                Rectangle {
                    anchors.centerIn: parent
                    width: 8
                    height: 8
                    radius: 4
                    color: "#a77bff"
                    visible: choice.chosen
                }
            }

            Text {
                Layout.fillWidth: true
                text: choice.text
                elide: Text.ElideRight
                font: choice.font
                color: C.Theme.text
            }
        }
    }

    /** A filter that is on or off, with a label that wraps in a narrow column. */
    component FilterCheck: C.CheckBox {
        id: check
        Layout.fillWidth: true
        topPadding: 4
        bottomPadding: 4
        font.pixelSize: 13
        font.weight: Font.DemiBold
        Material.accent: C.Theme.button

        // Keys and controller: the cyan ring every control of the new look shows
        background: Rectangle {
            radius: 10
            color: "transparent"
            border.width: check.visualFocus ? 2 : 0
            border.color: C.Theme.focus
        }

        contentItem: Text {
            leftPadding: check.indicator.width + check.spacing
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            text: check.text
            font: check.font
            color: "#e7e4fa"
        }
    }

    // ------------------------------------------------------------------ ground

    Rectangle {
        anchors.fill: parent
        color: C.Theme.ground
    }

    // A soft light from above
    Shape {
        id: glow
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 420

        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: glow.width / 2
                centerY: -200
                focalX: centerX
                focalY: centerY
                centerRadius: 600
                GradientStop { position: 0.0; color: Qt.rgba(124 / 255, 134 / 255, 1, 0.16) }
                GradientStop { position: 0.7; color: Qt.rgba(124 / 255, 134 / 255, 1, 0) }
            }
            startX: 0
            startY: 0
            PathLine { x: glow.width; y: 0 }
            PathLine { x: glow.width; y: glow.height }
            PathLine { x: 0; y: glow.height }
            PathLine { x: 0; y: 0 }
        }
    }

    // ------------------------------------------------------------------ title bar: the invite

    Item {
        id: titleBar
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: headerColumn.implicitHeight + 24
        z: 1

        ColumnLayout {
            id: headerColumn
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
                leftMargin: dialog.pad
                rightMargin: dialog.pad
                topMargin: 12
            }
            spacing: 10

            GridLayout {
                Layout.fillWidth: true
                columns: dialog.narrowHeader ? 2 : 3
                columnSpacing: 14
                rowSpacing: 10

                C.IconButton {
                    iconName: "back"
                    tip: qsTr("Back")
                    focusPolicy: Qt.NoFocus
                    onClicked: dialog.close()
                }

                Text {
                    Layout.fillWidth: dialog.narrowHeader
                    Layout.rightMargin: 10
                    text: dialog.title
                    elide: Text.ElideRight
                    font.pixelSize: 22
                    font.weight: Font.ExtraBold
                    color: C.Theme.text
                }

                // Like an address bar: paste it and join
                Rectangle {
                    id: addressBar
                    Layout.columnSpan: dialog.narrowHeader ? 2 : 1
                    Layout.fillWidth: true
                    implicitHeight: 52
                    radius: 16
                    color: C.Theme.card
                    border.width: inviteField.activeFocus ? 2 : 1
                    border.color: inviteField.activeFocus ? C.Theme.focus : Qt.rgba(124 / 255, 134 / 255, 1, 0.4)

                    HoverHandler {
                        id: addressHover
                    }

                    Rectangle {
                        z: -1
                        anchors {
                            fill: parent
                            margins: -4
                        }
                        radius: 20
                        color: "transparent"
                        border.width: 4
                        border.color: Qt.rgba(124 / 255, 134 / 255, 1, 0.08)
                    }

                    RowLayout {
                        anchors {
                            fill: parent
                            leftMargin: 16
                            rightMargin: 6
                        }
                        spacing: 10

                        C.Icon {
                            iconName: "link"
                            size: 18
                            color: C.Theme.headingSoft
                        }

                        C.TextField {
                            id: inviteField
                            Layout.fillWidth: true
                            implicitHeight: 40
                            firstInFocusChain: true
                            leftPadding: 0
                            rightPadding: 0
                            topPadding: 0
                            bottomPadding: 0
                            verticalAlignment: TextInput.AlignVCenter
                            font.pixelSize: 15
                            font.weight: Font.DemiBold
                            color: C.Theme.text
                            Accessible.name: inviteHint.text
                            Accessible.description: inviteAbout.text
                            // Once there is a list of recent sessions, the last one is there
                            text: recent.list.length > 0 ? "" : Chiaki.settings.coplayLastInvite
                            echoMode: Chiaki.settings.streamerMode ? TextInput.Password : TextInput.Normal
                            onAccepted: if (dialog.canJoinInvite) dialog.join(text, "")
                            ToolTip.visible: addressHover.hovered && inviteField.length === 0 && !inviteField.activeFocus
                            ToolTip.delay: 600
                            ToolTip.text: inviteAbout.text

                            background: Item {
                                Text {
                                    id: inviteHint
                                    anchors {
                                        left: parent.left
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                    }
                                    visible: inviteField.length === 0 && inviteField.preeditText.length === 0
                                    elide: Text.ElideRight
                                    text: qsTr("Paste the invite a friend sent you: psrpt://…")
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    color: C.Theme.textDim
                                }

                                Text {
                                    id: inviteAbout
                                    visible: false
                                    text: qsTr("Paste the invite your friend sent you. You get their picture and sound, and your controller plays along on their console.")
                                }
                            }
                        }

                        C.PillButton {
                            id: inviteJoin
                            kind: "primary"
                            implicitHeight: 40
                            text: dialog.joining && dialog.joiningRoom.length === 0 ? qsTr("Connecting…") : qsTr("Join")
                            enabled: dialog.canJoinInvite
                            onClicked: dialog.join(inviteField.text, "")
                        }
                    }
                }
            }

            // Joined with an invite (or the room is not in the list right now): what happens, and a way out
            Rectangle {
                id: inviteWaiting
                Layout.fillWidth: true
                visible: dialog.joining && !dialog.joiningShown
                // A hidden Cancel would keep the focus out of sight
                onVisibleChanged: if (!visible) Qt.callLater(dialog.recoverFocus)
                implicitHeight: inviteWaitRow.implicitHeight + 20
                radius: 14
                color: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.1)
                border.width: 1
                border.color: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.45)

                RowLayout {
                    id: inviteWaitRow
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: 14
                        rightMargin: 10
                    }
                    spacing: 12

                    BusyIndicator {
                        Layout.preferredWidth: 22
                        Layout.preferredHeight: 22
                        padding: 0
                        running: inviteWaiting.visible
                        Material.accent: C.Theme.wait
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                        font.pixelSize: 14
                        font.weight: Font.ExtraBold
                        color: "#fde4a8"
                        text: dialog.waiting
                              ? (dialog.joiningTitle ? qsTr("Waiting for the host of \"%1\" to let you in…").arg(dialog.joiningTitle)
                                                     : qsTr("Waiting for the host to let you in…"))
                              : qsTr("Looking for the host…")
                    }

                    C.PillButton {
                        id: inviteCancel
                        kind: "ghost"
                        small: true
                        text: qsTr("Cancel")
                        onClicked: Chiaki.coplayCancelJoin()
                    }
                }
            }

            // Why joining did not work, from the invite or from a room
            Rectangle {
                Layout.fillWidth: true
                visible: errorLabel.text.length > 0
                implicitHeight: errorRow.implicitHeight + 20
                radius: 14
                color: Qt.rgba(0.94, 0.27, 0.43, 0.12)
                border.width: 1
                border.color: Qt.rgba(0.94, 0.27, 0.43, 0.45)

                RowLayout {
                    id: errorRow
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: 14
                        rightMargin: 14
                    }
                    spacing: 10

                    Rectangle {
                        Layout.preferredWidth: 8
                        Layout.preferredHeight: 8
                        radius: 4
                        color: C.Theme.bad
                    }

                    Label {
                        id: errorLabel
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        textFormat: Text.PlainText
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: "#ffc2d1"
                    }
                }
            }
        }

        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            color: Qt.rgba(1, 1, 1, 0.06)
        }
    }

    // ------------------------------------------------------------------ the page

    Flickable {
        id: flick
        anchors {
            top: titleBar.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        contentWidth: width
        contentHeight: dialog.wide ? Math.max(aside.implicitHeight, roomsColumn.implicitHeight) + dialog.topPad + dialog.bottomPad
                                   : aside.implicitHeight + 24 + roomsColumn.implicitHeight + dialog.topPad + dialog.bottomPad
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            width: 12
        }

        // ---------------------------------------------------- finding a room, and how you join

        GridLayout {
            id: aside
            x: dialog.contentLeft
            y: dialog.stickyAside ? Math.max(dialog.topPad, Math.min(flick.contentY + dialog.topPad, flick.contentHeight - dialog.bottomPad - implicitHeight))
                                  : dialog.topPad
            width: dialog.wide ? dialog.asideWidth : dialog.innerWidth
            columns: !dialog.wide && dialog.innerWidth >= 600 ? 2 : 1
            columnSpacing: 14
            rowSpacing: 14

            C.Card {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                implicitHeight: findColumn.implicitHeight + 32

                ColumnLayout {
                    id: findColumn
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        margins: 16
                    }
                    spacing: 12

                    C.SectionLabel {
                        text: qsTr("Find a room")
                    }

                    TextField {
                        id: searchField
                        Layout.fillWidth: true
                        implicitHeight: 40
                        leftPadding: 36
                        rightPadding: 12
                        topPadding: 0
                        bottomPadding: 0
                        verticalAlignment: TextInput.AlignVCenter
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        color: C.Theme.text
                        selectByMouse: true
                        Accessible.name: searchHint.text
                        onTextChanged: rooms.search = text
                        Keys.onEscapePressed: (event) => {
                            if (text.length > 0) {
                                text = "";
                                event.accepted = true;
                            } else {
                                event.accepted = false;
                            }
                        }
                        Keys.onUpPressed: C.FocusChain.move(searchField, false)
                        Keys.onDownPressed: C.FocusChain.move(searchField, true)

                        background: Rectangle {
                            radius: C.Theme.radiusField
                            color: C.Theme.field
                            border.width: searchField.activeFocus ? 2 : 1
                            border.color: searchField.activeFocus ? C.Theme.focus : C.Theme.lineStrong

                            C.Icon {
                                x: 12
                                anchors.verticalCenter: parent.verticalCenter
                                iconName: "search"
                                size: 16
                                color: C.Theme.textSoft
                            }

                            Text {
                                id: searchHint
                                anchors {
                                    left: parent.left
                                    right: parent.right
                                    leftMargin: searchField.leftPadding
                                    rightMargin: searchField.rightPadding
                                    verticalCenter: parent.verticalCenter
                                }
                                visible: searchField.length === 0 && searchField.preeditText.length === 0
                                elide: Text.ElideRight
                                text: qsTr("Title, game or host")
                                font.pixelSize: 14
                                font.weight: Font.DemiBold
                                color: C.Theme.textDim
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 5

                        Text {
                            text: qsTr("Language")
                            font.pixelSize: 13
                            font.weight: Font.Bold
                            color: C.Theme.textSoft
                        }

                        C.ComboBox {
                            id: languageCombo
                            Layout.fillWidth: true
                            implicitHeight: 40
                            topInset: 0
                            bottomInset: 0
                            leftPadding: 12
                            rightPadding: 36
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            Material.background: C.Theme.popup
                            Material.foreground: C.Theme.text
                            Material.accent: "#a77bff"
                            Accessible.name: qsTr("Language")
                            readonly property var codes: [""].concat(Chiaki.settings.coplayLanguages.map((l) => l[0]))
                            model: [qsTr("All languages")].concat(Chiaki.settings.coplayLanguages.map((l) => l[1]))
                            onActivated: (index) => rooms.language = codes[index]

                            background: Rectangle {
                                radius: C.Theme.radiusField
                                color: C.Theme.field
                                border.width: languageCombo.visualFocus ? 2 : 1
                                border.color: languageCombo.visualFocus ? C.Theme.focus : C.Theme.lineStrong
                            }

                            contentItem: Text {
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                text: languageCombo.displayText
                                font: languageCombo.font
                                color: C.Theme.text
                            }

                            indicator: C.Icon {
                                x: languageCombo.width - width - 12
                                y: (languageCombo.height - height) / 2
                                iconName: "chevron-down"
                                size: 16
                                color: C.Theme.textSoft
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 5

                        Text {
                            text: qsTr("Console")
                            font.pixelSize: 13
                            font.weight: Font.Bold
                            color: C.Theme.textSoft
                        }

                        C.ComboBox {
                            id: consoleCombo
                            Layout.fillWidth: true
                            implicitHeight: 40
                            topInset: 0
                            bottomInset: 0
                            leftPadding: 12
                            rightPadding: 36
                            font.pixelSize: 14
                            font.weight: Font.DemiBold
                            Material.background: C.Theme.popup
                            Material.foreground: C.Theme.text
                            Material.accent: "#a77bff"
                            Accessible.name: qsTr("Console")
                            // What the game runs on: a PlayStation, or a game on the host's computer
                            readonly property var codes: ["", "ps5", "ps4", "pc", "xbox"]
                            model: [qsTr("All consoles"), "PS5", "PS4", qsTr("PC"), "Xbox"]
                            onActivated: (index) => rooms.system = codes[index]

                            background: Rectangle {
                                radius: C.Theme.radiusField
                                color: C.Theme.field
                                border.width: consoleCombo.visualFocus ? 2 : 1
                                border.color: consoleCombo.visualFocus ? C.Theme.focus : C.Theme.lineStrong
                            }

                            contentItem: Text {
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                text: consoleCombo.displayText
                                font: consoleCombo.font
                                color: C.Theme.text
                            }

                            indicator: C.Icon {
                                x: consoleCombo.width - width - 12
                                y: (consoleCombo.height - height) / 2
                                iconName: "chevron-down"
                                size: 16
                                color: C.Theme.textSoft
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        Text {
                            text: qsTr("Order")
                            font.pixelSize: 13
                            font.weight: Font.Bold
                            color: C.Theme.textSoft
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Repeater {
                                model: [qsTr("Most players"), qsTr("Most free places"), qsTr("Newest"), qsTr("Open longest")]

                                Choice {
                                    required property string modelData
                                    required property int index
                                    text: modelData
                                    chosen: rooms.sort === index
                                    onClicked: rooms.sort = index
                                }
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Qt.rgba(1, 1, 1, 0.07)
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        FilterCheck {
                            text: qsTr("Hide full rooms")
                            checked: rooms.hideFull
                            onClicked: rooms.hideFull = checked
                        }

                        FilterCheck {
                            text: qsTr("Hide rooms that ask to join")
                            checked: rooms.openOnly
                            onClicked: rooms.openOnly = checked
                        }

                        FilterCheck {
                            text: qsTr("Only rooms streaming on Twitch")
                            checked: rooms.twitchOnly
                            onClicked: rooms.twitchOnly = checked
                        }
                    }
                }
            }

            C.Card {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                implicitHeight: youColumn.implicitHeight + 32

                ColumnLayout {
                    id: youColumn
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        margins: 16
                    }
                    spacing: 12

                    C.SectionLabel {
                        text: qsTr("You join as")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        C.Avatar {
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 36
                            source: Chiaki.settings.coplayAvatarUrl
                            name: Chiaki.settings.coplayName || "?"
                        }

                        Text {
                            Layout.fillWidth: true
                            readonly property bool named: !!Chiaki.settings.coplayName
                            wrapMode: named ? Text.NoWrap : Text.WordWrap
                            elide: named ? Text.ElideRight : Text.ElideNone
                            textFormat: Text.PlainText
                            text: named ? Chiaki.settings.coplayName : qsTr("You have no name yet: the others will see \"Guest\".")
                            font.pixelSize: named ? 15 : 13
                            font.weight: named ? Font.ExtraBold : Font.Normal
                            color: named ? C.Theme.text : C.Theme.textSoft
                        }

                        C.PillButton {
                            kind: "ghost"
                            small: true
                            text: qsTr("Change…")
                            onClicked: root.showSettingsDialog("profile")
                        }
                    }

                    // A row of its own rather than C.ToggleRow: in this narrow column the
                    // title has to wrap instead of being cut short. A click on the words switches too.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            TapHandler {
                                onTapped: controllerOnlyCheck.toggle()
                            }

                            Text {
                                id: controllerOnlyTitle
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("With my controller only")
                                font.pixelSize: 13
                                font.weight: Font.ExtraBold
                                color: C.Theme.text
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: qsTr("No picture or sound: I sit in front of the same TV")
                                font.pixelSize: 12
                                color: C.Theme.textDim
                            }
                        }

                        C.Toggle {
                            id: controllerOnlyCheck
                            Accessible.name: controllerOnlyTitle.text
                            checked: Chiaki.settings.coplayControllerOnly
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------- public rooms

        ColumnLayout {
            id: roomsColumn
            x: dialog.wide ? dialog.contentLeft + dialog.asideWidth + 24 : dialog.contentLeft
            y: dialog.wide ? dialog.topPad : aside.y + aside.implicitHeight + 24
            width: dialog.wide ? dialog.innerWidth - dialog.asideWidth - 24 : dialog.innerWidth
            spacing: 10

            // ------------------------------------------------ sessions joined before

            ColumnLayout {
                id: recentColumn
                Layout.fillWidth: true
                Layout.bottomMargin: 18
                visible: recent.list.length > 0
                spacing: 10

                Text {
                    text: qsTr("Recent sessions")
                    font.pixelSize: 22
                    font.weight: Font.ExtraBold
                    color: C.Theme.text
                }

                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 13
                    color: C.Theme.textDim
                    text: qsTr("The sessions you joined. An invite stays the same until its host makes a new one, so you can join again here. “Open now” means the host is sharing it right now.")
                }

                Repeater {
                    model: recent.list

                    Rectangle {
                        id: recentCard
                        required property var modelData
                        readonly property bool joiningHere: dialog.joining && dialog.joiningRecent === modelData.key
                        readonly property string name: modelData.title || modelData.host || qsTr("A friend's session")
                        readonly property bool narrow: width < 540

                        Layout.fillWidth: true
                        implicitHeight: recentRow.implicitHeight + 24
                        radius: 18
                        color: joiningHere ? "#1b1631" : (recentHover.hovered ? C.Theme.cardHigh : C.Theme.card)
                        border.width: 1
                        border.color: joiningHere ? Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.7) : C.Theme.line

                        HoverHandler {
                            id: recentHover
                        }

                        GridLayout {
                            id: recentRow
                            anchors {
                                left: parent.left
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 12
                                rightMargin: 12
                            }
                            columns: recentCard.narrow ? 2 : 4
                            columnSpacing: 14
                            rowSpacing: 10

                            // The game's cover where it was a public room with one, otherwise an initial
                            Item {
                                Layout.preferredWidth: 56
                                Layout.preferredHeight: 56
                                Layout.alignment: Qt.AlignVCenter

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 14
                                    gradient: Gradient {
                                        GradientStop { position: 0.0; color: C.Theme.brandFrom }
                                        GradientStop { position: 1.0; color: C.Theme.brandTo }
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        visible: recentCover.status !== Image.Ready
                                        textFormat: Text.PlainText
                                        font.pixelSize: 24
                                        font.weight: Font.ExtraBold
                                        color: "#ffffff"
                                        text: (recentCard.modelData.game || recentCard.name).trim().charAt(0).toUpperCase()
                                    }
                                }

                                Image {
                                    id: recentCover
                                    anchors.fill: parent
                                    visible: false
                                    layer.enabled: true
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    sourceSize.width: 112
                                    sourceSize.height: 112
                                    source: recentCard.modelData.cover ? recentCard.modelData.cover + "?w=192" : ""
                                }

                                Rectangle {
                                    id: recentMask
                                    anchors.fill: parent
                                    radius: 14
                                    visible: false
                                    layer.enabled: true
                                }

                                MultiEffect {
                                    anchors.fill: parent
                                    source: recentCover
                                    maskEnabled: true
                                    maskSource: recentMask
                                    visible: recentCover.status === Image.Ready
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 4

                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                    font.pixelSize: 16
                                    font.weight: Font.ExtraBold
                                    color: C.Theme.text
                                    text: recentCard.name
                                }

                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                    font.pixelSize: 13
                                    color: C.Theme.textSoft
                                    text: {
                                        const r = recentCard.modelData;
                                        const parts = [];
                                        if (r.host && r.game)
                                            parts.push(qsTr("%1 plays %2").arg(r.host).arg(r.game));
                                        else if (r.host && r.title)
                                            parts.push(qsTr("Hosted by %1").arg(r.host));
                                        parts.push(recent.joinedText(r.joined));
                                        return parts.join(" · ");
                                    }
                                }

                                Flow {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 3
                                    spacing: 5

                                    C.Badge {
                                        small: true
                                        dot: true
                                        visible: recentCard.modelData.status > 0
                                        tone: recentCard.modelData.status === 2 ? "good" : recentCard.modelData.status === 1 ? "wait" : "neutral"
                                        text: recentCard.modelData.status === 2 ? qsTr("Open now")
                                            : recentCard.modelData.status === 1 ? qsTr("Checking…") : qsTr("Not open right now")
                                    }
                                    C.Badge {
                                        small: true
                                        tone: "system"
                                        visible: text !== ""
                                        text: rooms.consoleName(recentCard.modelData.console)
                                    }
                                }
                            }

                            C.PillButton {
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                Layout.minimumWidth: 104
                                implicitHeight: 42
                                // Also when nobody is seen: a host without a meeting point is only found by trying
                                kind: recentCard.modelData.status === 3 ? "ghost" : "primary"
                                text: recentCard.joiningHere ? (dialog.waiting ? qsTr("Knocking…") : qsTr("Connecting…")) : qsTr("Join")
                                enabled: !dialog.joining
                                Accessible.description: recentCard.name
                                onClicked: dialog.joinRecent(recentCard.modelData)
                            }

                            C.IconButton {
                                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                small: true
                                iconName: "close"
                                tip: qsTr("Remove from this list")
                                enabled: !recentCard.joiningHere
                                onClicked: Chiaki.settings.coplayForgetRecent(recentCard.modelData.key)
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Public rooms")
                    font.pixelSize: 22
                    font.weight: Font.ExtraBold
                    color: C.Theme.text
                }

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    visible: rooms.listed
                    text: rooms.shown.length === rooms.all.length ? (rooms.all.length === 1 ? qsTr("1 room") : qsTr("%1 rooms").arg(rooms.all.length))
                                                                  : qsTr("%1 of %2").arg(rooms.shown.length).arg(rooms.all.length)
                    font.pixelSize: 13
                    color: C.Theme.textSoft
                }

                Text {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideRight
                    text: rooms.state === 4 ? ""
                        : dialog.roomsUpdatedAt > 0 ? dialog.updatedText()
                        : rooms.state === 1 ? qsTr("Loading…") : ""
                    font.pixelSize: 12
                    color: C.Theme.textDim
                }

                // Never switched off while it reads, so that the focus stays on it
                C.IconButton {
                    id: refreshButton
                    small: true
                    iconName: "refresh"
                    tip: qsTr("Refresh")
                    opacity: rooms.state === 1 ? 0.5 : 1
                    onClicked: {
                        if (rooms.state !== 1)
                            Chiaki.settings.coplayRefreshRooms();
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: rooms.state !== 4
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                color: C.Theme.textDim
                text: qsTr("Sessions their hosts want to be found. Join one like with an invite: you connect to the host directly. Some rooms ask to join first; in others you join as a viewer or join and play.")
            }

            // Switched off, not readable, nothing there, nothing that matches
            Rectangle {
                id: stateBox
                readonly property bool problem: rooms.state === 3 || rooms.state === 4
                Layout.fillWidth: true
                Layout.topMargin: 4
                visible: stateText.text.length > 0
                implicitHeight: stateText.implicitHeight + (problem ? 28 : 80)
                radius: 18
                color: problem ? Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.08) : "transparent"
                border.width: 1
                border.color: problem ? Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.35) : C.Theme.lineStrong

                Text {
                    id: stateText
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: 20
                        rightMargin: 20
                    }
                    horizontalAlignment: stateBox.problem ? Text.AlignLeft : Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    textFormat: Text.PlainText
                    font.pixelSize: stateBox.problem ? 14 : 15
                    color: stateBox.problem ? "#fbd98c" : C.Theme.textSoft
                    text: rooms.state === 4 ? qsTr("Public rooms are switched off in the Play Together settings (Network).")
                        : rooms.state === 3 ? qsTr("The list of public rooms could not be read: %1").arg(Chiaki.settings.coplayRoomsError || "")
                        : rooms.listed && rooms.shown.length === 0
                          ? (rooms.all.length === 0 ? qsTr("No public rooms right now. Host one yourself: choose Public on the Host page.")
                                                    : qsTr("No room matches. Try other words, another language, or show all rooms."))
                        : ""
                }
            }

            Repeater {
                id: roomRepeater
                model: rooms.shown

                Rectangle {
                    id: roomCard
                    required property var modelData
                    readonly property bool joinable: !modelData.full && modelData.compatible
                    /** This room is being joined: its card says what happens, with a way out. */
                    readonly property bool joiningHere: dialog.joining && dialog.joiningRoom.length > 0 && dialog.joiningRoom === modelData.invite
                    readonly property bool narrow: width < 540

                    function takeFocus(reason) {
                        const target = cancelButton.visible ? cancelButton : joinButton;
                        if (!target.visible || !target.enabled)
                            return false;
                        target.forceActiveFocus(reason);
                        return target.activeFocus;
                    }

                    Layout.fillWidth: true
                    implicitHeight: cardColumn.implicitHeight + 24
                    radius: 18
                    color: joiningHere ? "#1b1631" : (cardHover.hovered ? C.Theme.cardHigh : C.Theme.card)
                    border.width: 1
                    border.color: joiningHere ? Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.7) : C.Theme.line

                    HoverHandler {
                        id: cardHover
                    }

                    // A soft amber ring around the room we wait for
                    Rectangle {
                        z: -1
                        anchors {
                            fill: parent
                            margins: -4
                        }
                        radius: parent.radius + 4
                        visible: roomCard.joiningHere
                        color: "transparent"
                        border.width: 4
                        border.color: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.1)
                    }

                    ColumnLayout {
                        id: cardColumn
                        anchors {
                            left: parent.left
                            right: parent.right
                            top: parent.top
                            leftMargin: 12
                            rightMargin: 14
                            topMargin: 12
                        }
                        spacing: 10

                        // Wide: picture, what it is, players, Join in one row; narrow: players and Join below
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 4
                            columnSpacing: 14
                            rowSpacing: 10

                            // The game as it is right now, where the host sends pictures; otherwise the
                            // game's cover where the host's app found one, otherwise its initial
                            Item {
                                id: tile
                                readonly property bool live: livePicture.status === Image.Ready
                                Layout.row: 0
                                Layout.column: 0
                                Layout.preferredWidth: live ? 121 : 68
                                Layout.preferredHeight: 68
                                Layout.alignment: roomCard.narrow ? Qt.AlignTop : Qt.AlignVCenter

                                Rectangle {
                                    anchors.fill: parent
                                    visible: !tile.live
                                    radius: 14
                                    gradient: Gradient {
                                        GradientStop { position: 0.0; color: C.Theme.brandFrom }
                                        GradientStop { position: 1.0; color: C.Theme.brandTo }
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        visible: cover.status !== Image.Ready
                                        textFormat: Text.PlainText
                                        font.pixelSize: 28
                                        font.weight: Font.ExtraBold
                                        color: "#ffffff"
                                        text: (roomCard.modelData.game || roomCard.modelData.title || "?").trim().charAt(0).toUpperCase()
                                    }
                                }

                                Image {
                                    id: cover
                                    anchors.fill: parent
                                    visible: false
                                    layer.enabled: true
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    sourceSize.width: 144
                                    sourceSize.height: 144
                                    // Sony's image server makes a small one
                                    source: roomCard.modelData.cover ? roomCard.modelData.cover + "?w=192" : ""
                                }

                                // From the list itself; its address changes with every new picture
                                Image {
                                    id: livePicture
                                    anchors.fill: parent
                                    visible: false
                                    layer.enabled: true
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    source: roomCard.modelData.picture || ""
                                }

                                Rectangle {
                                    id: coverMask
                                    anchors.fill: parent
                                    radius: 14
                                    visible: false
                                    layer.enabled: true
                                }

                                MultiEffect {
                                    anchors.fill: parent
                                    source: tile.live ? livePicture : cover
                                    maskEnabled: true
                                    maskSource: coverMask
                                    visible: tile.live || cover.status === Image.Ready
                                }

                                // Over the picture, the game's cover small in the corner: which game it is
                                Rectangle {
                                    id: coverBadge
                                    visible: tile.live && cover.status === Image.Ready
                                    anchors {
                                        top: parent.top
                                        right: parent.right
                                        margins: 6
                                    }
                                    width: 26
                                    height: 26
                                    radius: 8
                                    color: Qt.rgba(1, 1, 1, 0.75)

                                    Rectangle {
                                        id: coverBadgeMask
                                        anchors.fill: parent
                                        anchors.margins: 1.5
                                        radius: 7
                                        visible: false
                                        layer.enabled: true
                                    }

                                    MultiEffect {
                                        anchors.fill: coverBadgeMask
                                        source: cover
                                        maskEnabled: true
                                        maskSource: coverBadgeMask
                                    }
                                }

                                // The picture is the game as it is now
                                Rectangle {
                                    visible: tile.live
                                    anchors {
                                        left: parent.left
                                        bottom: parent.bottom
                                        margins: 6
                                    }
                                    width: liveRow.implicitWidth + 12
                                    height: 18
                                    radius: 9
                                    color: Qt.rgba(0, 0, 0, 0.62)

                                    Row {
                                        id: liveRow
                                        anchors.centerIn: parent
                                        spacing: 4

                                        Rectangle {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 6
                                            height: 6
                                            radius: 3
                                            color: C.Theme.bad
                                        }

                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: qsTr("Live")
                                            color: "#ffffff"
                                            font.pixelSize: 10
                                            font.weight: Font.ExtraBold
                                        }
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.row: 0
                                Layout.column: 1
                                Layout.columnSpan: roomCard.narrow ? 3 : 1
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 4

                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                    font.pixelSize: 16
                                    font.weight: Font.ExtraBold
                                    color: C.Theme.text
                                    text: roomCard.modelData.title
                                }

                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    textFormat: Text.PlainText
                                    font.pixelSize: 13
                                    color: C.Theme.textSoft
                                    text: {
                                        const r = roomCard.modelData;
                                        const who = r.game ? qsTr("%1 plays %2").arg(r.host).arg(r.game)
                                                           : qsTr("Hosted by %1").arg(r.host);
                                        return r.since > 0 ? who + " · " + dialog.openFor(r.since) : who;
                                    }
                                }

                                Flow {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 3
                                    spacing: 5

                                    // The host is live on Twitch: a different thing than the "Live" picture of the game
                                    Rectangle {
                                        visible: !!(roomCard.modelData.twitch && roomCard.modelData.twitch.length)
                                        width: twitchRow.implicitWidth + 14
                                        height: 20
                                        radius: 10
                                        color: "#7c3aed"

                                        Row {
                                            id: twitchRow
                                            anchors.centerIn: parent
                                            spacing: 5

                                            Rectangle {
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: 6
                                                height: 6
                                                radius: 3
                                                color: "#ffffff"
                                            }

                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: roomCard.modelData.twitch && roomCard.modelData.twitch.length > 1
                                                      ? qsTr("%1 on Twitch").arg(roomCard.modelData.twitch.length) : qsTr("On Twitch")
                                                color: "#ffffff"
                                                font.pixelSize: 11
                                                font.weight: Font.ExtraBold
                                            }
                                        }
                                    }

                                    C.Badge {
                                        small: true
                                        tone: "system"
                                        visible: text !== ""
                                        text: rooms.consoleName(roomCard.modelData.console)
                                    }
                                    C.Badge {
                                        small: true
                                        text: rooms.languageName(roomCard.modelData.lang)
                                    }
                                    C.Badge {
                                        small: true
                                        tone: "play"
                                        text: roomCard.modelData.control === "turns" ? qsTr("One plays at a time")
                                            : roomCard.modelData.control === "host" ? qsTr("Only the host plays")
                                            : roomCard.modelData.control === "each" ? qsTr("Each their own controller")
                                            : qsTr("Everybody plays together")
                                    }
                                    C.Badge {
                                        small: true
                                        tone: "door"
                                        text: roomCard.modelData.policy === "ask" ? qsTr("Ask to join")
                                            : roomCard.modelData.policy === "watch" ? qsTr("Join as viewer") : qsTr("Join and play")
                                    }
                                    C.Badge {
                                        small: true
                                        tone: "web"
                                        visible: !!roomCard.modelData.web
                                        text: qsTr("Browsers welcome")
                                    }
                                }
                            }

                            // Who plays (their pictures, or their initials where they have none) and how many, of how many
                            ColumnLayout {
                                id: playersColumn
                                /** Initials, one for each player, the host first; empty from an older host. */
                                readonly property var initials: roomCard.modelData.initials || []
                                /** The players' pictures in the same order, "" where a player has none. */
                                readonly property var faces: roomCard.modelData.faces || []
                                /** Up to five circles; with more, four and "+N". */
                                readonly property var shown: initials.length > 5 ? initials.slice(0, 4).concat(["+" + (initials.length - 4)]) : initials
                                Layout.row: roomCard.narrow ? 1 : 0
                                Layout.column: roomCard.narrow ? 0 : 2
                                Layout.columnSpan: roomCard.narrow ? 3 : 1
                                Layout.fillWidth: roomCard.narrow
                                Layout.preferredWidth: roomCard.narrow ? -1 : 104
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 5

                                Row {
                                    Layout.alignment: roomCard.narrow ? Qt.AlignLeft : Qt.AlignRight
                                    visible: playersColumn.shown.length > 0
                                    spacing: -7

                                    Repeater {
                                        model: playersColumn.shown

                                        Rectangle {
                                            id: face
                                            required property var modelData
                                            required property int index
                                            readonly property var colours: ["#a77bff", "#f472b6", "#34d399", "#fbbf24", "#60a5fa", "#fb923c"]
                                            readonly property bool more: String(modelData).charAt(0) === "+"
                                            readonly property string picture: more ? "" : (playersColumn.faces[index] || "")
                                            readonly property bool pictured: facePicture.status === Image.Ready
                                            width: 24
                                            height: 24
                                            radius: 12
                                            color: more ? "#3a3458" : colours[index % colours.length]
                                            border.width: 2
                                            border.color: C.Theme.card

                                            Text {
                                                anchors.centerIn: parent
                                                visible: !face.pictured
                                                textFormat: Text.PlainText
                                                text: face.modelData
                                                font.pixelSize: String(face.modelData).length > 1 ? 9 : 11
                                                font.weight: Font.ExtraBold
                                                color: face.more ? C.Theme.text : "#120d26"
                                            }

                                            // The player's own picture, from the list, where they have one
                                            Image {
                                                id: facePicture
                                                anchors.fill: parent
                                                anchors.margins: 2
                                                visible: false
                                                layer.enabled: true
                                                asynchronous: true
                                                fillMode: Image.PreserveAspectCrop
                                                sourceSize.width: 64
                                                sourceSize.height: 64
                                                source: face.picture
                                            }

                                            Rectangle {
                                                id: faceMask
                                                anchors.fill: facePicture
                                                radius: width / 2
                                                visible: false
                                                layer.enabled: true
                                            }

                                            MultiEffect {
                                                anchors.fill: facePicture
                                                visible: face.pictured
                                                source: facePicture
                                                maskEnabled: true
                                                maskSource: faceMask
                                            }
                                        }
                                    }
                                }

                                Row {
                                    Layout.alignment: roomCard.narrow ? Qt.AlignLeft : Qt.AlignRight
                                    spacing: 4

                                    // Without initials (an older host), the people icon says what the numbers are
                                    C.Icon {
                                        visible: playersColumn.shown.length === 0
                                        anchors.verticalCenter: parent.verticalCenter
                                        iconName: "people"
                                        size: 15
                                        color: C.Theme.textDim
                                    }

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "%1 / %2".arg(roomCard.modelData.players).arg(roomCard.modelData.max)
                                        font.pixelSize: 13
                                        font.weight: Font.ExtraBold
                                        color: roomCard.modelData.full ? "#ff9db5" : C.Theme.text
                                    }

                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: qsTr("players")
                                        font.pixelSize: 12
                                        font.weight: Font.DemiBold
                                        color: C.Theme.textDim
                                    }
                                }
                            }

                            ColumnLayout {
                                Layout.row: roomCard.narrow ? 1 : 0
                                Layout.column: 3
                                Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                                spacing: 4

                                // The other rooms' Join buttons rest while one is being joined
                                C.PillButton {
                                    id: joinButton
                                    readonly property string roomKey: roomCard.modelData.invite
                                    Layout.alignment: Qt.AlignRight
                                    Layout.minimumWidth: 104
                                    implicitHeight: 42
                                    kind: roomCard.joinable ? "primary" : "ghost"
                                    text: roomCard.modelData.full ? qsTr("Full")
                                        : roomCard.joiningHere ? (dialog.waiting ? qsTr("Knocking…") : qsTr("Connecting…"))
                                        : qsTr("Join")
                                    enabled: roomCard.joinable && !dialog.joining
                                    Accessible.description: roomCard.modelData.title
                                    onClicked: dialog.join(roomCard.modelData.invite, roomCard.modelData.title, true)
                                }

                                // Only channel names come from the list; the addresses are made here
                                Repeater {
                                    model: (roomCard.modelData.twitch || []).slice(0, 3)

                                    C.PillButton {
                                        required property string modelData
                                        Layout.alignment: Qt.AlignRight
                                        small: true
                                        kind: "ghost"
                                        text: (roomCard.modelData.twitch || []).length > 1 ? qsTr("Watch %1").arg(modelData) : qsTr("Watch on Twitch")
                                        Accessible.description: qsTr("%1 on Twitch").arg(modelData)
                                        onClicked: Qt.openUrlExternally("https://www.twitch.tv/" + modelData)
                                    }
                                }

                                Text {
                                    Layout.alignment: Qt.AlignRight
                                    Layout.maximumWidth: 160
                                    visible: !roomCard.modelData.compatible
                                    horizontalAlignment: Text.AlignRight
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 12
                                    color: C.Theme.textDim
                                    text: qsTr("Needs another version of the app")
                                }
                            }
                        }

                        // Knocking: who we wait for, and a way out
                        Rectangle {
                            Layout.fillWidth: true
                            visible: roomCard.joiningHere
                            onVisibleChanged: if (!visible) Qt.callLater(dialog.recoverFocus)
                            implicitHeight: waitRow.implicitHeight + 20
                            radius: 14
                            color: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.1)

                            RowLayout {
                                id: waitRow
                                anchors {
                                    left: parent.left
                                    right: parent.right
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 14
                                    rightMargin: 10
                                }
                                spacing: 12

                                BusyIndicator {
                                    Layout.preferredWidth: 22
                                    Layout.preferredHeight: 22
                                    padding: 0
                                    running: roomCard.joiningHere
                                    Material.accent: C.Theme.wait
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        textFormat: Text.PlainText
                                        font.pixelSize: 14
                                        font.weight: Font.ExtraBold
                                        color: "#fde4a8"
                                        text: !dialog.waiting ? qsTr("Looking for the host…")
                                            : roomCard.modelData.host ? qsTr("Waiting for %1 to let you in…").arg(roomCard.modelData.host)
                                            : qsTr("Waiting for the host of \"%1\" to let you in…").arg(roomCard.modelData.title)
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        visible: dialog.waiting && !!roomCard.modelData.host
                                        wrapMode: Text.WordWrap
                                        textFormat: Text.PlainText
                                        font.pixelSize: 12
                                        color: "#d8c9a3"
                                        text: qsTr("%1 sees your name. You can keep looking around meanwhile.").arg(roomCard.modelData.host)
                                    }
                                }

                                C.PillButton {
                                    id: cancelButton
                                    readonly property string roomKey: roomCard.modelData.invite
                                    kind: "ghost"
                                    small: true
                                    text: qsTr("Cancel")
                                    onClicked: Chiaki.coplayCancelJoin()
                                }
                            }
                        }
                    }
                }
            }

            // Rooms the search and the filters leave out, and a way to see them all
            C.PillButton {
                Layout.alignment: Qt.AlignHCenter
                visible: rooms.listed && rooms.all.length > rooms.shown.length
                onVisibleChanged: if (!visible) Qt.callLater(dialog.recoverFocus)
                kind: "ghost"
                small: true
                text: (rooms.all.length - rooms.shown.length === 1 ? qsTr("1 room hidden by your filters · show all")
                                                                       : qsTr("%1 rooms hidden by your filters · show all").arg(rooms.all.length - rooms.shown.length))
                onClicked: dialog.showAllRooms()
            }
        }
    }
}
