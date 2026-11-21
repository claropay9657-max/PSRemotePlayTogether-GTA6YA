import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Effects
import QtQuick.Shapes

import org.streetpea.chiaking

import "controls" as C

// Host: everything about a session friends join, set up before it starts. Private:
// only friends with the invite get in. Public: the room is listed for everybody,
// with a title, a language and the game, and the host decides who gets in.
// "Start" connects to the console with all of this; "Play" on the start page plays
// without anybody.
//
// The page draws its own title bar and, at the bottom, what is about to start with
// Cancel and Start. DialogView underneath still does the rest: Escape goes back,
// Menu (Options on a controller) starts, and the focus comes back where it was.
DialogView {
    id: dialog
    title: qsTr("Host a session")

    /** Registered consoles that can be played: index into Chiaki.hosts, name, state. */
    readonly property var consoles: {
        let list = [];
        for (let i = 0; i < Chiaki.hosts.length; ++i) {
            const h = Chiaki.hosts[i];
            if (h.display && h.registered)
                list.push({ index: i, name: h.name, discovered: h.discovered, state: h.state, app: h.app || "", titleId: h.titleId || "", psn: !!h.duid });
        }
        return list;
    }
    property int consoleChoice: 0
    readonly property var chosen: consoles.length > 0 ? consoles[Math.min(consoleChoice, consoles.length - 1)] : null
    readonly property bool resting: !!chosen && chosen.state === "standby"
    readonly property bool publicRoom: !!Chiaki.settings.coplayHostPublic
    readonly property bool roomsOn: !!Chiaki.settings.coplayRoomsOn && !!Chiaki.settings.coplayMeetOn

    // The room as Join shows it to everybody else (StreamSession::CoPlayStartRoom
    // describes it the same way when the session starts)
    readonly property string hostName: (Chiaki.settings.coplayName || "").trim() || qsTr("Host")
    readonly property string defaultTitle: qsTr("%1's room").arg(hostName)
    readonly property string roomTitle: titleField.text.replace(/\s+/g, " ").trim() || defaultTitle
    /** The game the console runs, where the console was found in the network. */
    readonly property string roomGame: chosen && chosen.discovered ? chosen.app : ""
    /** The game's picture, only where the app already has one: a room in the list it read plays the same game. */
    readonly property string roomCover: {
        const id = chosen && chosen.discovered ? chosen.titleId : "";
        if (!id)
            return "";
        const rooms = Chiaki.settings.coplayRooms || [];
        for (let i = 0; i < rooms.length; ++i) {
            if (rooms[i].titleId === id && rooms[i].cover)
                return rooms[i].cover;
        }
        return "";
    }
    readonly property bool roomWeb: !!Chiaki.settings.coplayWebSupported && !!Chiaki.settings.coplayWebGuests
    /** How the room is played, by Chiaki.settings.coplayMixMode, in the words of the room list. */
    readonly property var controlNames: [qsTr("Everybody plays together"), qsTr("One plays at a time"), qsTr("Only the host plays")]
    /** How one gets in, by Chiaki.settings.coplayJoinPolicy, in the words of the room list. */
    readonly property var doorNames: [qsTr("Ask to join"), qsTr("Join as viewer"), qsTr("Join and play")]
    /** What each way of controlling means, under the choice. */
    readonly property var controlHints: [qsTr("Everybody's buttons count at the same time."), qsTr("Whoever moves takes over; the others rest."), qsTr("Your friends watch.")]
    readonly property string controlName: controlNames[Chiaki.settings.coplayMixMode] || controlNames[0]
    readonly property string guestsText: Chiaki.settings.coplayMaxGuests === 1 ? qsTr("1 friend") : qsTr("Up to %1 friends").arg(Chiaki.settings.coplayMaxGuests)

    /** One line about what Start is about to start. */
    readonly property string summary: {
        if (!chosen)
            return "";
        let parts = [];
        if (publicRoom) {
            parts.push(qsTr("Public room “%1”").arg(roomTitle));
            if (Chiaki.settings.coplayRoomLanguage)
                parts.push(languageName(Chiaki.settings.coplayRoomLanguage));
        } else {
            parts.push(qsTr("Private session"));
        }
        parts.push(guestsText);
        parts.push(controlName);
        return parts.join("  ·  ");
    }

    /** What a console card says about its console. */
    function consoleLine(c) {
        if (!c)
            return "";
        if (c.state === "standby")
            return qsTr("Rest mode · woken up when you start");
        if (c.app)
            return qsTr("On · running %1").arg(c.app);
        if (c.psn && !c.discovered)
            return qsTr("Over the internet (PSN).");
        return c.discovered ? qsTr("Ready.") : "";
    }

    function languageName(code) {
        if (!code)
            return qsTr("Any language");
        const list = Chiaki.settings.coplayLanguages;
        for (let i = 0; i < list.length; ++i) {
            if (list[i][0] === code)
                return list[i][1];
        }
        return code.toUpperCase();
    }

    function back() {
        dialog.rejected();
        dialog.close();
    }

    buttonText: resting ? qsTr("Wake up & start") : qsTr("Start")
    buttonEnabled: !!chosen && !(publicRoom && !roomsOn)
    // Start is in the bar at the bottom of the page (it shows buttonText and buttonEnabled)
    buttonVisible: false
    onAccepted: {
        if (!chosen)
            return;
        // Whatever was typed last counts, also without pressing Enter
        Chiaki.settings.coplayRoomTitle = titleField.text;
        Chiaki.hostConsole(chosen.index, chosen.discovered ? chosen.name : "");
    }

    // ------------------------------------------------------------ pieces of this page

    /** A choice that is one card: a console, private or public, who gets in. */
    component ChoiceCard: AbstractButton {
        id: card
        property bool chosen: false
        property string title
        property string subtitle
        property color subtitleColor: C.Theme.textSoft
        /** The round tick on the right, for choices that are things (consoles). */
        property bool tick: false
        /** Smaller, for the choices inside a group. */
        property bool compact: false
        property bool firstInFocusChain: false
        property bool lastInFocusChain: false
        /** What stands in front: an icon, a console. */
        default property alias leading: leadingSlot.data

        Layout.fillWidth: true
        Layout.preferredWidth: 100
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        implicitHeight: Math.max(compact ? 48 : 60, cardRow.implicitHeight + topPadding + bottomPadding)
        leftPadding: compact ? 12 : 14
        rightPadding: compact ? 12 : 14
        topPadding: 8
        bottomPadding: 8
        Accessible.role: Accessible.RadioButton
        Accessible.name: title
        Accessible.description: subtitle
        Accessible.checked: chosen

        Keys.onPressed: (event) => C.FocusChain.handle(card, event)

        background: Item {
            Rectangle {
                anchors.fill: parent
                radius: card.compact ? 14 : 16
                color: card.chosen ? Qt.rgba(140 / 255, 82 / 255, 1, card.compact ? 0.16 : 0.14)
                                   : Qt.rgba(1, 1, 1, card.down ? 0.09 : card.hovered ? 0.065 : 0.04)
                border.width: card.chosen || !card.compact ? 2 : 1
                border.color: card.chosen ? C.Theme.brandFrom : Qt.rgba(1, 1, 1, card.hovered ? 0.18 : 0.1)
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: (card.compact ? 14 : 16) + 4
                color: "transparent"
                border.width: 2
                border.color: C.Theme.focus
                visible: card.visualFocus
            }
        }

        contentItem: RowLayout {
            id: cardRow
            spacing: card.compact ? 10 : 12

            Row {
                id: leadingSlot
                Layout.alignment: Qt.AlignVCenter
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    text: card.title
                    color: card.compact && !card.chosen ? "#e7e4fa" : C.Theme.text
                    font.pixelSize: card.compact ? 13 : 15
                    font.weight: card.compact ? Font.Bold : Font.ExtraBold
                    wrapMode: card.compact ? Text.WordWrap : Text.NoWrap
                    elide: card.compact ? Text.ElideNone : Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: card.subtitle
                    color: card.subtitleColor
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                visible: card.tick
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: 22
                implicitHeight: 22
                radius: 11
                color: card.chosen ? C.Theme.brandFrom : "transparent"
                border.width: 2
                border.color: card.chosen ? C.Theme.brandFrom : Qt.rgba(1, 1, 1, 0.28)

                C.Icon {
                    anchors.centerIn: parent
                    visible: card.chosen
                    iconName: "check"
                    size: 12
                    color: "#ffffff"
                }
            }
        }
    }

    /** A line icon on a small tinted square. */
    component IconTile: Rectangle {
        id: tile
        property string iconName
        property color iconColor: "#d9d5f2"
        implicitWidth: 34
        implicitHeight: 34
        radius: 10
        color: Qt.rgba(1, 1, 1, 0.07)

        C.Icon {
            anchors.centerIn: parent
            iconName: tile.iconName
            size: 18
            color: tile.iconColor
        }
    }

    /** A console standing upright, with its light: long when on, short in rest mode. */
    component ConsoleGlyph: Item {
        id: glyph
        property bool resting: false
        property color light: C.Theme.good
        implicitWidth: 26
        implicitHeight: 34

        Rectangle {
            x: 6
            y: 1
            width: 14
            height: 32
            radius: 5
            color: glyph.resting ? "#221d3c" : "#2a2448"
            border.width: 1
            border.color: glyph.resting ? "#3a3360" : "#4a4173"

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: glyph.resting ? 13 : 5
                width: 2
                height: glyph.resting ? 5 : 22
                radius: 1
                color: glyph.light
            }
        }
    }

    /** An invite row in the look of this page: the same row, copying the same way. */
    component Invite: C.InviteRow {
        id: inviteRow
        /** The one most friends need: its Copy is filled. */
        property bool emphasised: false

        radius: 14
        color: Qt.rgba(1, 1, 1, 0.03)
        border.color: C.Theme.line

        // Up and Down go on from Copy and "Try again", as everywhere else on the page
        Keys.onPressed: (event) => {
            if (event.key !== Qt.Key_Up && event.key !== Qt.Key_Down)
                return;
            const window = inviteRow.Window.window;
            const item = window ? window.activeFocusItem : null;
            const next = item ? item.nextItemInFocusChain(event.key === Qt.Key_Down) : null;
            if (next)
                next.forceActiveFocus(Qt.TabFocusReason);
            event.accepted = true;
        }

        copyButton.implicitHeight: 34
        copyButton.topInset: 0
        copyButton.bottomInset: 0
        copyButton.leftInset: 0
        copyButton.rightInset: 0
        copyButton.topPadding: 0
        copyButton.bottomPadding: 0
        copyButton.leftPadding: 14
        copyButton.rightPadding: 14
        copyButton.font.pixelSize: 13
        copyButton.font.weight: Font.ExtraBold
        copyButton.background: Rectangle {
            implicitWidth: 64
            implicitHeight: 34
            radius: height / 2
            opacity: inviteRow.copyButton.enabled ? 1 : 0.45
            color: inviteRow.emphasised
                   ? Qt.lighter(C.Theme.button, inviteRow.copyButton.down ? 0.9 : inviteRow.copyButton.hovered ? 1.12 : 1)
                   : Qt.rgba(1, 1, 1, inviteRow.copyButton.down ? 0.16 : inviteRow.copyButton.hovered ? 0.12 : 0.08)
            border.width: inviteRow.copyButton.visualFocus ? 2 : inviteRow.emphasised ? 0 : 1
            border.color: inviteRow.copyButton.visualFocus ? C.Theme.focus : Qt.rgba(1, 1, 1, 0.12)
        }
        copyButton.contentItem: Text {
            text: inviteRow.copyButton.text
            font: inviteRow.copyButton.font
            color: C.Theme.text
            opacity: inviteRow.copyButton.enabled ? 1 : 0.7
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // ------------------------------------------------------------ the page

    Item {
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
                const top = item.mapToItem(flick.contentItem, 0, 0).y - 24;
                const bottom = top + item.height + 48;
                const max = Math.max(0, flick.contentHeight - flick.height);
                if (top < flick.contentY)
                    flick.contentY = Math.max(0, top);
                else if (bottom > flick.contentY + flick.height)
                    flick.contentY = Math.min(max, bottom - flick.height);
            }
        }

        // The whole window, over DialogView's own bar
        Item {
            id: page
            parent: dialog
            anchors.fill: parent
            z: 10

            /** Two columns side by side; below about 900 px they stack. */
            readonly property bool wide: width >= 900
            readonly property int gutter: width < 600 ? 16 : 32
            readonly property real innerWidth: Math.max(0, Math.min(width - 2 * gutter, 1336))
            readonly property real sideWidth: wide ? Math.min(440, Math.max(360, Math.round(innerWidth * 0.3))) : innerWidth
            readonly property real mainWidth: wide ? innerWidth - sideWidth - 24 : innerWidth

            // Nothing under the page is pressed by mistake
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
            }

            Rectangle {
                anchors.fill: parent
                color: C.Theme.ground
            }

            // A soft purple glow from the top left corner
            Shape {
                width: parent.width
                height: 900
                transform: Scale { yScale: 400 / 900 }

                ShapePath {
                    strokeWidth: 0
                    strokeColor: "transparent"
                    fillGradient: RadialGradient {
                        centerX: 0
                        centerY: -90
                        focalX: 0
                        focalY: -90
                        centerRadius: 900
                        GradientStop { position: 0.0; color: Qt.rgba(140 / 255, 82 / 255, 1, 0.16) }
                        GradientStop { position: 0.7; color: Qt.rgba(140 / 255, 82 / 255, 1, 0) }
                    }
                    startX: 0
                    startY: 0
                    PathLine { x: page.width; y: 0 }
                    PathLine { x: page.width; y: 900 }
                    PathLine { x: 0; y: 900 }
                    PathLine { x: 0; y: 0 }
                }
            }

            Flickable {
                id: flick
                anchors {
                    top: titleBar.bottom
                    left: parent.left
                    right: parent.right
                    bottom: actionBar.top
                }
                contentWidth: width
                contentHeight: pageColumns.implicitHeight + 20 + 28
                clip: true
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {
                    policy: flick.contentHeight > flick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    width: 12
                }

                GridLayout {
                    id: pageColumns
                    x: Math.round((flick.width - width) / 2)
                    y: 20
                    width: page.innerWidth
                    columns: page.wide ? 2 : 1
                    columnSpacing: 24
                    rowSpacing: 18

                    // ==================================================== settings, top to bottom

                    ColumnLayout {
                        id: column
                        Layout.preferredWidth: page.mainWidth
                        Layout.maximumWidth: page.mainWidth
                        // Only as high as what is in it, whatever the other column needs
                        Layout.fillHeight: false
                        Layout.alignment: Qt.AlignTop
                        spacing: 18

                        // ------------------------------------------------ console

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            C.SectionLabel {
                                text: qsTr("Console")
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: dialog.consoles.length === 0
                                wrapMode: Text.WordWrap
                                color: "#fbd98c"
                                font.pixelSize: 14
                                text: qsTr("No registered console yet. Add yours on the start page first (\"Add console\"), then come back here to host.")
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                visible: dialog.consoles.length > 0
                                columns: Math.max(1, Math.min(dialog.consoles.length, Math.floor((page.mainWidth + 10) / 270)))
                                columnSpacing: 10
                                rowSpacing: 10

                                Repeater {
                                    model: dialog.consoles

                                    ChoiceCard {
                                        id: consoleCard
                                        required property var modelData
                                        required property int index
                                        readonly property bool awake: modelData.discovered && modelData.state === "ready"

                                        chosen: index === Math.min(dialog.consoleChoice, dialog.consoles.length - 1)
                                        tick: true
                                        title: modelData.name
                                        subtitle: dialog.consoleLine(modelData)
                                        subtitleColor: modelData.state === "standby" ? "#fbd98c"
                                                     : consoleCard.awake ? "#9ff0cc" : C.Theme.textSoft
                                        onClicked: dialog.consoleChoice = index

                                        ConsoleGlyph {
                                            resting: consoleCard.modelData.state === "standby"
                                            light: consoleCard.modelData.state === "standby" ? C.Theme.wait
                                                 : consoleCard.awake ? C.Theme.good : C.Theme.textDim
                                        }
                                    }
                                }
                            }
                        }

                        // ------------------------------------------------ who can join

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            C.SectionLabel {
                                text: qsTr("Who can join")
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: page.mainWidth >= 540 ? 2 : 1
                                columnSpacing: 10
                                rowSpacing: 10

                                ChoiceCard {
                                    chosen: !dialog.publicRoom
                                    title: qsTr("Private")
                                    subtitle: qsTr("Only friends I send my invite to")
                                    onClicked: Chiaki.settings.coplayHostPublic = false

                                    IconTile {
                                        iconName: "lock"
                                    }
                                }

                                ChoiceCard {
                                    chosen: dialog.publicRoom
                                    title: qsTr("Public room")
                                    subtitle: qsTr("Listed for everybody under Join")
                                    onClicked: Chiaki.settings.coplayHostPublic = true

                                    IconTile {
                                        iconName: "globe"
                                        color: Qt.rgba(124 / 255, 134 / 255, 1, 0.18)
                                        iconColor: "#b9bfff"
                                    }
                                }
                            }

                            // What a public room is called, and who gets in
                            C.Card {
                                Layout.fillWidth: true
                                visible: dialog.publicRoom
                                radius: 18
                                implicitHeight: publicColumn.implicitHeight + 28

                                ColumnLayout {
                                    id: publicColumn
                                    anchors {
                                        top: parent.top
                                        left: parent.left
                                        right: parent.right
                                        topMargin: 14
                                        leftMargin: 16
                                        rightMargin: 16
                                    }
                                    spacing: 12

                                    Rectangle {
                                        Layout.fillWidth: true
                                        visible: dialog.publicRoom && !dialog.roomsOn
                                        implicitHeight: roomsOffLabel.implicitHeight + 20
                                        radius: 12
                                        color: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.12)
                                        border.width: 1
                                        border.color: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.4)

                                        Label {
                                            id: roomsOffLabel
                                            anchors {
                                                left: parent.left
                                                right: parent.right
                                                verticalCenter: parent.verticalCenter
                                                leftMargin: 12
                                                rightMargin: 12
                                            }
                                            wrapMode: Text.WordWrap
                                            color: "#fbd98c"
                                            font.pixelSize: 13
                                            text: !Chiaki.settings.coplayRoomsOn
                                                  ? qsTr("Public rooms are switched off in the Play Together settings (Network).")
                                                  : qsTr("A public room needs the meeting point, which is switched off in the Play Together settings (Network).")
                                        }
                                    }

                                    GridLayout {
                                        Layout.fillWidth: true
                                        columns: page.mainWidth >= 560 ? 2 : 1
                                        columnSpacing: 12
                                        rowSpacing: 12

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.preferredWidth: 300
                                            spacing: 5

                                            Label {
                                                text: qsTr("Title")
                                                color: C.Theme.textSoft
                                                font.pixelSize: 13
                                                font.weight: Font.Bold
                                            }

                                            C.TextField {
                                                id: titleField
                                                Layout.fillWidth: true
                                                maximumLength: 60
                                                text: Chiaki.settings.coplayRoomTitle
                                                onEditingFinished: Chiaki.settings.coplayRoomTitle = text
                                                Accessible.name: qsTr("Title")
                                                Accessible.description: dialog.defaultTitle

                                                leftPadding: 12
                                                rightPadding: 12
                                                topPadding: 0
                                                bottomPadding: 0
                                                color: C.Theme.text
                                                font.pixelSize: 14
                                                font.weight: Font.DemiBold
                                                // The field's own placeholder would float onto its border
                                                background: Rectangle {
                                                    implicitWidth: 120
                                                    implicitHeight: 40
                                                    radius: C.Theme.radiusField
                                                    color: C.Theme.field
                                                    border.width: titleField.activeFocus ? 2 : 1
                                                    border.color: !titleField.activeFocus ? Qt.rgba(1, 1, 1, 0.12)
                                                                : titleField.readOnly ? C.Theme.focus : C.Theme.brandFrom

                                                    Text {
                                                        anchors {
                                                            left: parent.left
                                                            right: parent.right
                                                            verticalCenter: parent.verticalCenter
                                                            leftMargin: titleField.leftPadding
                                                            rightMargin: titleField.rightPadding
                                                        }
                                                        visible: titleField.length === 0 && titleField.preeditText.length === 0
                                                        elide: Text.ElideRight
                                                        text: dialog.defaultTitle
                                                        color: C.Theme.textDim
                                                        font: titleField.font
                                                    }
                                                }
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            Layout.preferredWidth: 170
                                            spacing: 5

                                            Label {
                                                text: qsTr("Language")
                                                color: C.Theme.textSoft
                                                font.pixelSize: 13
                                                font.weight: Font.Bold
                                            }

                                            C.ComboBox {
                                                id: languageCombo
                                                Layout.fillWidth: true
                                                // The first entry is "any": the room does not say
                                                readonly property var codes: [""].concat(Chiaki.settings.coplayLanguages.map((l) => l[0]))
                                                model: [qsTr("Any language")].concat(Chiaki.settings.coplayLanguages.map((l) => l[1]))
                                                currentIndex: Math.max(0, codes.indexOf(Chiaki.settings.coplayRoomLanguage))
                                                onActivated: (index) => Chiaki.settings.coplayRoomLanguage = codes[index]
                                                Accessible.name: qsTr("Language")

                                                implicitHeight: 40
                                                font.pixelSize: 14
                                                font.weight: Font.DemiBold
                                                leftPadding: 0
                                                rightPadding: 36
                                                background: Rectangle {
                                                    implicitWidth: 120
                                                    implicitHeight: 40
                                                    radius: C.Theme.radiusField
                                                    color: C.Theme.field
                                                    border.width: languageCombo.visualFocus ? 2 : 1
                                                    border.color: languageCombo.visualFocus ? C.Theme.focus
                                                                : Qt.rgba(1, 1, 1, languageCombo.hovered ? 0.2 : 0.12)
                                                }
                                                indicator: C.Icon {
                                                    x: languageCombo.width - width - 12
                                                    y: (languageCombo.height - height) / 2
                                                    iconName: "chevron-down"
                                                    size: 16
                                                    color: C.Theme.textSoft
                                                }
                                                popup.background: Rectangle {
                                                    radius: 10
                                                    color: C.Theme.popup
                                                    border.width: 1
                                                    border.color: Qt.rgba(1, 1, 1, 0.16)
                                                }
                                            }
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            text: qsTr("Who gets in")
                                            color: C.Theme.textSoft
                                            font.pixelSize: 13
                                            font.weight: Font.Bold
                                        }

                                        GridLayout {
                                            Layout.fillWidth: true
                                            columns: page.mainWidth >= 600 ? 3 : 1
                                            columnSpacing: 8
                                            rowSpacing: 8

                                            Repeater {
                                                model: [
                                                    { icon: "door", text: qsTr("Ask to join"), sub: qsTr("You approve each player first") },
                                                    { icon: "eye", text: qsTr("Join as viewers"), sub: qsTr("They play once you allow it") },
                                                    { icon: "people", text: qsTr("Join and play"), sub: qsTr("No approval, they play right away") },
                                                ]

                                                ChoiceCard {
                                                    id: doorChoice
                                                    required property var modelData
                                                    required property int index

                                                    compact: true
                                                    chosen: Chiaki.settings.coplayJoinPolicy === index
                                                    title: modelData.text
                                                    subtitle: modelData.sub
                                                    onClicked: Chiaki.settings.coplayJoinPolicy = index

                                                    C.Icon {
                                                        iconName: doorChoice.modelData.icon
                                                        size: 18
                                                        color: doorChoice.chosen ? "#cdb6ff" : C.Theme.textSoft
                                                    }
                                                }
                                            }
                                        }

                                        // "Only me" leaves nobody else at the controller, whatever the door says
                                        Label {
                                            Layout.fillWidth: true
                                            visible: Chiaki.settings.coplayMixMode === 2
                                            wrapMode: Text.WordWrap
                                            color: C.Theme.textDim
                                            font.pixelSize: 12
                                            text: qsTr("While only you control (Who controls: Only me), everybody watches either way.")
                                        }
                                    }

                                    // A picture of the game now and then, taken from the stream on this
                                    // computer (StreamSession, CoPlayRoomAnnouncer); none in streamer mode
                                    C.ToggleRow {
                                        visible: dialog.publicRoom && dialog.roomsOn
                                        iconName: "monitor"
                                        title: qsTr("Live picture in the list")
                                        subtitle: Chiaki.settings.streamerMode ? qsTr("Off while streamer mode is on.")
                                                : (Chiaki.settings.audioVideoDisabled & 0x02) !== 0 ? qsTr("Needs the picture on this computer: “I play on the TV” is on.")
                                                : qsTr("A small picture of your game every 45 seconds, so that people see what you play.")
                                        enabled: !Chiaki.settings.streamerMode
                                        checked: !!Chiaki.settings.coplayRoomPreview && !Chiaki.settings.streamerMode
                                        onToggled: Chiaki.settings.coplayRoomPreview = checked
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        visible: dialog.publicRoom && dialog.roomsOn
                                        spacing: 8

                                        C.Icon {
                                            Layout.alignment: Qt.AlignTop
                                            Layout.topMargin: 1
                                            iconName: "shield"
                                            size: 16
                                            color: C.Theme.headingSoft
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            color: C.Theme.textSoft
                                            font.pixelSize: 13
                                            lineHeight: 1.15
                                            text: qsTr("Players connect to your computer directly and so learn its internet address; in a public room, anybody who reads the list. Your private invites stay private.")
                                        }
                                    }
                                }
                            }
                        }

                        // ------------------------------------------------ players

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            C.SectionLabel {
                                text: qsTr("Players")
                            }

                            GridLayout {
                                id: playersGrid
                                readonly property bool sideBySide: page.mainWidth >= 600
                                Layout.fillWidth: true
                                columns: sideBySide ? 2 : 1
                                columnSpacing: 12
                                rowSpacing: 12

                                C.Card {
                                    Layout.fillWidth: !playersGrid.sideBySide
                                    Layout.preferredWidth: playersGrid.sideBySide ? 230 : -1
                                    Layout.fillHeight: true
                                    radius: 16
                                    implicitWidth: friendsRow.implicitWidth + 24
                                    implicitHeight: friendsRow.implicitHeight + 16

                                    RowLayout {
                                        id: friendsRow
                                        anchors {
                                            left: parent.left
                                            right: parent.right
                                            verticalCenter: parent.verticalCenter
                                            leftMargin: 14
                                            rightMargin: 10
                                        }
                                        spacing: 10

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 1

                                            Label {
                                                text: qsTr("Friends")
                                                color: C.Theme.text
                                                font.pixelSize: 14
                                                font.weight: Font.ExtraBold
                                            }

                                            Label {
                                                text: qsTr("%1 to %2").arg(1).arg(8)
                                                color: C.Theme.textDim
                                                font.pixelSize: 12
                                            }
                                        }

                                        C.Stepper {
                                            value: Chiaki.settings.coplayMaxGuests
                                            from: 1
                                            to: 8
                                            lessText: qsTr("Fewer friends")
                                            moreText: qsTr("More friends")
                                            onMoved: (n) => Chiaki.settings.coplayMaxGuests = n
                                        }
                                    }
                                }

                                C.Card {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: 16
                                    implicitHeight: controlColumn.implicitHeight + 18

                                    ColumnLayout {
                                        id: controlColumn
                                        anchors {
                                            left: parent.left
                                            right: parent.right
                                            verticalCenter: parent.verticalCenter
                                            leftMargin: 14
                                            rightMargin: 10
                                        }
                                        spacing: 6

                                        Label {
                                            text: qsTr("Who controls")
                                            color: C.Theme.text
                                            font.pixelSize: 14
                                            font.weight: Font.ExtraBold
                                        }

                                        C.Segmented {
                                            Layout.fillWidth: true
                                            model: [qsTr("Together"), qsTr("One at a time"), qsTr("Only me")]
                                            currentIndex: Chiaki.settings.coplayMixMode
                                            onActivated: (index) => Chiaki.settings.coplayMixMode = index
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            wrapMode: Text.WordWrap
                                            color: C.Theme.textDim
                                            font.pixelSize: 12
                                            text: dialog.controlHints[Chiaki.settings.coplayMixMode] || dialog.controlHints[0]
                                        }
                                    }
                                }
                            }

                            GridLayout {
                                Layout.fillWidth: true
                                columns: Math.max(1, Math.min(Chiaki.settings.coplayWebSupported ? 3 : 2, Math.floor((page.mainWidth + 10) / 270)))
                                columnSpacing: 10
                                rowSpacing: 10

                                C.ToggleRow {
                                    visible: !!Chiaki.settings.coplayWebSupported
                                    iconName: "browser"
                                    title: qsTr("Browsers allowed")
                                    subtitle: qsTr("Friends without the app")
                                    checked: !!Chiaki.settings.coplayWebGuests
                                    onToggled: Chiaki.settings.coplayWebGuests = checked
                                }

                                C.ToggleRow {
                                    iconName: "moon"
                                    title: qsTr("Guests may sleep it")
                                    subtitle: qsTr("Console rests when they leave")
                                    checked: !!Chiaki.settings.coplayGuestsMaySleep
                                    onToggled: Chiaki.settings.coplayGuestsMaySleep = checked
                                }

                                // The same setting as "On this computer" (Settings › General), where picture and
                                // sound can also be switched off one by one
                                C.ToggleRow {
                                    iconName: "tv"
                                    title: qsTr("I play on the TV")
                                    subtitle: qsTr("No picture or sound here")
                                    checked: (Chiaki.settings.audioVideoDisabled & 0x02) !== 0
                                    onToggled: Chiaki.settings.audioVideoDisabled = checked ? 3 : 0
                                }
                            }
                        }
                    }

                    // ==================================================== what others see, and the invites

                    ColumnLayout {
                        Layout.preferredWidth: page.sideWidth
                        Layout.maximumWidth: page.sideWidth
                        Layout.fillHeight: false
                        Layout.alignment: Qt.AlignTop
                        spacing: 16

                        // ------------------------------------------------ the room as the list shows it

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: dialog.publicRoom
                            spacing: 8

                            C.SectionLabel {
                                text: qsTr("Your room in the list")
                            }

                            Item {
                                id: preview
                                Layout.fillWidth: true
                                implicitHeight: previewRow.implicitHeight + 24
                                // Not listed while public rooms or the meeting point are off
                                opacity: dialog.roomsOn ? 1 : 0.5

                                Shape {
                                    anchors.fill: parent
                                    layer.enabled: true
                                    layer.samples: 4

                                    ShapePath {
                                        fillColor: C.Theme.card
                                        strokeColor: Qt.rgba(124 / 255, 134 / 255, 1, 0.45)
                                        strokeWidth: 1
                                        strokeStyle: ShapePath.DashLine
                                        dashPattern: [4, 3]
                                        startX: 18.5
                                        startY: 0.5
                                        PathLine { x: preview.width - 0.5 - 18; y: 0.5 }
                                        PathArc { x: preview.width - 0.5; y: 18.5; radiusX: 18; radiusY: 18 }
                                        PathLine { x: preview.width - 0.5; y: preview.height - 0.5 - 18 }
                                        PathArc { x: preview.width - 0.5 - 18; y: preview.height - 0.5; radiusX: 18; radiusY: 18 }
                                        PathLine { x: 18.5; y: preview.height - 0.5 }
                                        PathArc { x: 0.5; y: preview.height - 0.5 - 18; radiusX: 18; radiusY: 18 }
                                        PathLine { x: 0.5; y: 18.5 }
                                        PathArc { x: 18.5; y: 0.5; radiusX: 18; radiusY: 18 }
                                    }
                                }

                                RowLayout {
                                    id: previewRow
                                    anchors {
                                        left: parent.left
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                        margins: 12
                                    }
                                    spacing: 12

                                    // The game's picture where the app has one, otherwise its initial, as in the list
                                    Item {
                                        Layout.preferredWidth: 64
                                        Layout.preferredHeight: 64
                                        Layout.alignment: Qt.AlignTop

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: 14
                                            gradient: Gradient {
                                                GradientStop { position: 0.0; color: C.Theme.brandFrom }
                                                GradientStop { position: 1.0; color: C.Theme.brandTo }
                                            }

                                            Label {
                                                anchors.centerIn: parent
                                                visible: cover.status !== Image.Ready
                                                color: "#ffffff"
                                                font.pixelSize: 28
                                                font.weight: Font.ExtraBold
                                                text: (dialog.roomGame || dialog.roomTitle || "?").trim().charAt(0).toUpperCase()
                                            }
                                        }

                                        Image {
                                            id: cover
                                            anchors.fill: parent
                                            visible: false
                                            layer.enabled: true
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                            sourceSize.width: 128
                                            sourceSize.height: 128
                                            // Sony's image server makes a small one
                                            source: dialog.roomCover ? dialog.roomCover + "?w=192" : ""
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
                                            visible: cover.status === Image.Ready
                                            source: cover
                                            maskEnabled: true
                                            maskSource: coverMask
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 4

                                        Label {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            color: C.Theme.text
                                            font.pixelSize: 15
                                            font.weight: Font.ExtraBold
                                            text: dialog.roomTitle
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            color: C.Theme.textSoft
                                            font.pixelSize: 12
                                            text: dialog.roomGame
                                                  ? qsTr("%1 plays %2").arg(dialog.hostName).arg(dialog.roomGame)
                                                  : qsTr("Hosted by %1").arg(dialog.hostName)
                                        }

                                        Flow {
                                            Layout.fillWidth: true
                                            Layout.topMargin: 2
                                            spacing: 5

                                            C.Badge {
                                                small: true
                                                text: qsTr("%1 / %2 players").arg(1).arg(Chiaki.settings.coplayMaxGuests + 1)
                                            }
                                            C.Badge {
                                                small: true
                                                text: dialog.languageName(Chiaki.settings.coplayRoomLanguage)
                                            }
                                            C.Badge {
                                                small: true
                                                tone: "play"
                                                text: dialog.controlName
                                            }
                                            C.Badge {
                                                small: true
                                                tone: "door"
                                                text: dialog.doorNames[Chiaki.settings.coplayJoinPolicy] || dialog.doorNames[0]
                                            }
                                            C.Badge {
                                                small: true
                                                visible: dialog.roomWeb
                                                tone: "web"
                                                text: qsTr("Browsers welcome")
                                            }
                                        }
                                    }
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: C.Theme.textDim
                                font.pixelSize: 12
                                lineHeight: 1.15
                                text: qsTr("The list shows the title, the language, the game your console runs and how many play, and with the live picture on a small picture of your game. It names no address: players find you through the meeting point. A public room has an invite of its own, which changes when the room closes, so that strangers cannot come back later; your private invite does not lead into it. While playing, the menu shows who knocks, and lets you close the room to newcomers and remove and block a player.")
                            }
                        }

                        // ------------------------------------------------ invites

                        C.Card {
                            Layout.fillWidth: true
                            implicitHeight: inviteColumn.implicitHeight + 28

                            ColumnLayout {
                                id: inviteColumn
                                anchors {
                                    top: parent.top
                                    left: parent.left
                                    right: parent.right
                                    topMargin: 14
                                    leftMargin: 16
                                    rightMargin: 16
                                }
                                spacing: 10

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    C.SectionLabel {
                                        text: qsTr("Invites")
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        color: C.Theme.textSoft
                                        font.pixelSize: 13
                                        lineHeight: 1.15
                                        text: dialog.publicRoom
                                              ? qsTr("Players find your room in the list. To send friends straight to it, copy its invite. It is new for every public session.")
                                              : qsTr("Send one of these to your friends. They work every time you host privately, until you make a new one in the Play Together settings.")
                                    }
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

                                    Invite {
                                        emphasised: true
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

                                    Invite {
                                        title: qsTr("Same network")
                                        dot: "#00f2ff"
                                        secret: Chiaki.settings.streamerMode
                                        invite: Chiaki.settings.coplayInviteLan || Chiaki.settings.coplayInvite
                                        note: qsTr("For friends in your home network or in the same VPN. Works right away.")
                                    }

                                    Invite {
                                        visible: invites.ipv6Invite.length > 0
                                        title: qsTr("Internet, IPv6")
                                        dot: "#f0469b"
                                        secret: Chiaki.settings.streamerMode
                                        invite: invites.ipv6Invite
                                        note: qsTr("Try this one if the other internet invite does not work: many connections share their IPv4 address with others and can only be reached over IPv6. Your friend needs IPv6 too, and your router has to let UDP port %1 through.").arg(invites.port)
                                    }

                                    Invite {
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

                                // The meeting point
                                RowLayout {
                                    Layout.fillWidth: true
                                    visible: !!Chiaki.settings.coplayMeetOn
                                    spacing: 8

                                    C.Icon {
                                        Layout.alignment: Qt.AlignTop
                                        Layout.topMargin: 1
                                        iconName: "check"
                                        size: 15
                                        color: "#9ff0cc"
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WordWrap
                                        color: "#9ff0cc"
                                        font.pixelSize: 12
                                        text: qsTr("Friends get in through the meeting point: with most routers, no port to open.")
                                    }
                                }
                            }
                        }

                        C.PillButton {
                            Layout.alignment: Qt.AlignLeft
                            kind: "ghost"
                            small: true
                            iconName: "sliders"
                            text: qsTr("Network settings…")
                            onClicked: root.showSettingsDialog("network")
                        }
                    }
                }
            }

            // ------------------------------------------------ what is about to start, and Start

            Rectangle {
                id: actionBar
                readonly property bool narrow: page.width < 640
                anchors {
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                height: barLayout.implicitHeight + 28
                color: Qt.rgba(12 / 255, 10 / 255, 29 / 255, 0.92)

                Rectangle {
                    anchors {
                        top: parent.top
                        left: parent.left
                        right: parent.right
                    }
                    height: 1
                    color: C.Theme.line
                }

                GridLayout {
                    id: barLayout
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: page.gutter
                        rightMargin: page.gutter
                    }
                    // Narrow: the line above the buttons
                    columns: actionBar.narrow ? 1 : 2
                    columnSpacing: 14
                    rowSpacing: 10

                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0 || !actionBar.narrow
                        elide: Text.ElideRight
                        maximumLineCount: 1
                        color: C.Theme.textSoft
                        font.pixelSize: 14
                        text: dialog.summary
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        spacing: 14

                        C.PillButton {
                            kind: "ghost"
                            text: qsTr("Cancel")
                            onClicked: dialog.back()
                        }

                        C.PillButton {
                            kind: "brand"
                            iconName: dialog.resting ? "power" : "play"
                            text: dialog.buttonText
                            enabled: dialog.buttonEnabled
                            onClicked: dialog.accepted()
                        }
                    }
                }
            }

            // ------------------------------------------------ title bar

            Item {
                id: titleBar
                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                }
                height: 64

                Rectangle {
                    anchors {
                        left: parent.left
                        right: parent.right
                        bottom: parent.bottom
                    }
                    height: 1
                    color: Qt.rgba(1, 1, 1, 0.06)
                }

                RowLayout {
                    anchors {
                        fill: parent
                        leftMargin: page.gutter
                        rightMargin: page.gutter
                    }
                    spacing: 14

                    // As DialogView's own: Escape and Cancel do the same, so it stays out of the focus chain
                    C.IconButton {
                        iconName: "back"
                        tip: qsTr("Back")
                        focusPolicy: Qt.NoFocus
                        onClicked: dialog.back()
                    }

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        color: C.Theme.text
                        font.pixelSize: 22
                        font.weight: Font.ExtraBold
                        text: dialog.title
                    }

                    C.Avatar {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        source: Chiaki.settings.coplayAvatarUrl
                        name: dialog.hostName
                    }

                    Label {
                        Layout.maximumWidth: Math.max(80, page.width * 0.3)
                        visible: page.width >= 520
                        elide: Text.ElideRight
                        color: C.Theme.textSoft
                        font.pixelSize: 13
                        textFormat: Text.StyledText
                        text: qsTr("Hosting as %1").arg("<b><font color=\"#f3f1ff\">" + dialog.hostName.replace(/&/g, "&amp;").replace(/</g, "&lt;") + "</font></b>")
                    }
                }
            }
        }
    }
}
