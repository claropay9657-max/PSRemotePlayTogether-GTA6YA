import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import "controls" as C

// One message, the same in a session's chat (StreamChatPanel) and in the friends' chats
// (FriendsPanel): own messages on the right in the brand's colour, the others' on the left
// with their picture and name at the start of a run, a link's preview, the reactions under
// it, and the pictures of whoever has read up to it (as Instagram shows them). On hover a small
// bar beside the message offers reply, react and (for one's own) delete; react opens a bar of
// quick reactions over it (a long press opens that bar, with reply and delete in it too); "+"
// there opens all emoji (moreReactions()). The message it answers shows above the text (`quote`;
// a click goes there), a message that mentions the player is marked, and one its writer deleted
// is a stand-in.
//
// A card (an invite, a plan) can stand instead of the text bubble: `card`.
Item {
    id: msg

    property bool mine: false
    property string name: ""
    property color nameColor: C.Theme.headingSoft
    property string avatar: ""
    /** The first message of a run by one writer: their picture and name show. */
    property bool first: true
    /** Pictures and names of others (in groups and a session's chat; not with one friend). */
    property bool showAuthor: true
    property bool host: false
    /** The text, as rich text made by the app (escaped, links as links). */
    property string html: ""
    /** Under the text: the time, or "Sending…", "Not sent". */
    property string status: ""
    property bool statusBad: false
    /** A link's preview: url, site, title, description, image (a data: URL). */
    property var embed: null
    /** Each: emoji, count, mine. */
    property var reactions: []
    /** Whoever read up to here and no further. Each: name, avatar. */
    property var readers: []
    property Component card: null
    property bool canReact: true
    property bool canReply: false
    property bool canDelete: false
    /** The writer deleted it for everybody: a stand-in, without actions. */
    property bool deleted: false
    /** It mentions this player ("@" and their name): marked. */
    property bool mentionsMe: false
    /** The message it answers: name, text, color, deleted; null if it answers none. */
    property var quote: null
    property real maxBubble: Math.min(460, width * 0.8)

    signal react(string emoji)
    signal moreReactions()
    signal linkActivated(string link)
    signal replyRequested()
    signal deleteRequested()
    /** The quote was clicked: the chat goes to the message it answers. */
    signal quoteClicked()

    /** Lights up for a moment: the message a quote led to. */
    function highlight() {
        flash.restart();
    }

    readonly property bool hasActions: !deleted && (canReact || canReply || canDelete)
    readonly property color accentSoft: "#b18cff"

    readonly property bool sideAvatar: showAuthor && !mine
    /** Where the bubble (or card) is, in this item: for the smiley beside it and the bar over it. */
    readonly property real bodyTop: column.y + (nameRow.visible ? nameRow.implicitHeight + column.spacing : 0)
    readonly property real bodyHeight: card ? cardLoader.height : bubble.height
    readonly property real bodyWidth: card ? cardLoader.width : bubble.width
    readonly property real bodyLeft: mine ? column.x + column.width - bodyWidth : column.x
    readonly property int avatarSize: 30
    readonly property var quick: ["❤️", "😂", "👍", "😮", "😢", "🔥"]
    property bool barOpen: false

    implicitHeight: column.implicitHeight + (first ? 8 : 3)

    HoverHandler {
        id: hover
        onHoveredChanged: barClose.restart()
    }

    // The writer's picture, beside the first message of a run
    C.Avatar {
        visible: msg.sideAvatar && msg.first
        x: 6
        y: column.y + (nameLabel.visible ? nameLabel.implicitHeight + 3 : 0)
        width: msg.avatarSize
        height: msg.avatarSize
        source: msg.avatar
        name: msg.name
    }

    Column {
        id: column
        anchors.right: msg.mine ? parent.right : undefined
        anchors.left: msg.mine ? undefined : parent.left
        anchors.rightMargin: 6
        anchors.leftMargin: msg.sideAvatar ? msg.avatarSize + 14 : 6
        y: msg.first ? 8 : 3
        spacing: 4

        Row {
            id: nameRow
            visible: msg.showAuthor && !msg.mine && msg.first
            spacing: 6

            Label {
                id: nameLabel
                textFormat: Text.PlainText
                font.pixelSize: 12
                font.bold: true
                color: msg.nameColor
                text: msg.name
            }

            Rectangle {
                visible: msg.host
                anchors.verticalCenter: nameLabel.verticalCenter
                radius: 7
                width: hostTag.implicitWidth + 10
                height: 14
                color: Qt.rgba(140 / 255, 82 / 255, 1, 0.8)

                Label {
                    id: hostTag
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    font.pixelSize: 8
                    font.bold: true
                    color: "white"
                    text: qsTr("HOST")
                }
            }
        }

        // ---- the text bubble
        Rectangle {
            id: bubble
            visible: !msg.card
            anchors.right: msg.mine ? parent.right : undefined
            width: Math.min(msg.maxBubble, Math.max(bubbleColumn.implicitWidth, 72) + 24)
            height: bubbleColumn.implicitHeight + 16
            radius: 16
            color: msg.deleted ? Qt.rgba(1, 1, 1, 0.03)
                   : msg.mine ? Qt.rgba(140 / 255, 82 / 255, 1, 0.30)
                   : msg.mentionsMe ? Qt.rgba(140 / 255, 82 / 255, 1, 0.16) : C.Theme.cardHigh
            border.width: msg.mentionsMe && !msg.deleted ? 1.5 : 1
            border.color: msg.deleted ? C.Theme.line
                          : msg.mentionsMe ? Qt.rgba(177 / 255, 140 / 255, 1, 0.85)
                          : msg.mine ? Qt.rgba(140 / 255, 82 / 255, 1, 0.45) : C.Theme.line

            TapHandler {
                enabled: msg.hasActions
                onLongPressed: msg.barOpen = !msg.barOpen
            }

            ColumnLayout {
                id: bubbleColumn
                x: 12
                y: 8
                width: Math.min(implicitWidth, msg.maxBubble - 24)
                spacing: 6

                // The message it answers: its writer and the start of it; a click goes there
                Rectangle {
                    id: quoteBox
                    visible: !!msg.quote && !msg.deleted
                    Layout.preferredWidth: Math.min(Math.max(quoteName.implicitWidth, quoteText.implicitWidth, 80) + 22, msg.maxBubble - 24)
                    implicitHeight: quoteColumn.implicitHeight + 12
                    radius: 8
                    color: Qt.rgba(1, 1, 1, quoteHover.hovered ? 0.10 : 0.06)

                    Rectangle {
                        anchors {
                            left: parent.left
                            top: parent.top
                            bottom: parent.bottom
                            topMargin: 5
                            bottomMargin: 5
                            leftMargin: 4
                        }
                        width: 3
                        radius: 1.5
                        color: msg.quote && msg.quote.color ? msg.quote.color : msg.accentSoft
                    }

                    HoverHandler {
                        id: quoteHover
                        cursorShape: Qt.PointingHandCursor
                    }

                    TapHandler {
                        onTapped: msg.quoteClicked()
                    }

                    ColumnLayout {
                        id: quoteColumn
                        anchors {
                            left: parent.left
                            right: parent.right
                            top: parent.top
                            margins: 6
                            leftMargin: 13
                        }
                        spacing: 1

                        Label {
                            id: quoteName
                            Layout.fillWidth: true
                            visible: text.length > 0
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            font.pixelSize: 11
                            font.bold: true
                            color: msg.quote && msg.quote.color ? msg.quote.color : msg.accentSoft
                            text: msg.quote ? (msg.quote.name || "") : ""
                        }

                        Label {
                            id: quoteText
                            Layout.fillWidth: true
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            maximumLineCount: 1
                            font.pixelSize: 12
                            font.italic: !!msg.quote && !!msg.quote.deleted
                            opacity: 0.75
                            color: C.Theme.text
                            text: msg.quote ? (msg.quote.text || "") : ""
                        }
                    }
                }

                // A message its writer deleted for everybody
                Label {
                    visible: msg.deleted
                    textFormat: Text.PlainText
                    font.pixelSize: 13
                    font.italic: true
                    color: C.Theme.textDim
                    text: qsTr("Message deleted")
                }

                Text {
                    Layout.maximumWidth: msg.maxBubble - 24
                    visible: msg.html.length > 0 && !msg.deleted
                    wrapMode: Text.Wrap
                    textFormat: Text.StyledText
                    color: C.Theme.text
                    linkColor: C.Theme.focus
                    font.pixelSize: 14
                    text: msg.html
                    onLinkActivated: (link) => msg.linkActivated(link)

                    HoverHandler {
                        cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.IBeamCursor
                    }
                }

                // The preview the writer's app made of the first link
                Rectangle {
                    id: preview
                    visible: !msg.deleted && !!msg.embed && !!(msg.embed.title || msg.embed.description || msg.embed.image)
                    Layout.preferredWidth: Math.min(300, msg.maxBubble - 24)
                    implicitHeight: previewColumn.implicitHeight + 20
                    radius: 10
                    color: Qt.rgba(1, 1, 1, previewHover.hovered ? 0.08 : 0.05)
                    border.color: Qt.rgba(1, 1, 1, 0.08)

                    Rectangle {
                        anchors {
                            left: parent.left
                            top: parent.top
                            bottom: parent.bottom
                            topMargin: 8
                            bottomMargin: 8
                        }
                        width: 3
                        radius: 1.5
                        color: C.Theme.brandFrom
                    }

                    HoverHandler {
                        id: previewHover
                        cursorShape: msg.embed && msg.embed.url ? Qt.PointingHandCursor : Qt.ArrowCursor
                    }

                    TapHandler {
                        onTapped: {
                            if (msg.embed && msg.embed.url)
                                msg.linkActivated(msg.embed.url);
                        }
                    }

                    ColumnLayout {
                        id: previewColumn
                        anchors {
                            left: parent.left
                            right: parent.right
                            top: parent.top
                            margins: 10
                            leftMargin: 14
                        }
                        spacing: 4

                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            opacity: 0.6
                            font.pixelSize: 11
                            text: msg.embed ? (msg.embed.site || "") : ""
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            color: C.Theme.focus
                            font.pixelSize: 14
                            font.bold: true
                            text: msg.embed ? (msg.embed.title || "") : ""
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            opacity: 0.75
                            font.pixelSize: 12
                            text: msg.embed ? (msg.embed.description || "") : ""
                        }

                        Image {
                            Layout.fillWidth: true
                            Layout.preferredHeight: visible ? Math.round(width * Math.max(0.3, Math.min(1.2, sourceSize.height / Math.max(1, sourceSize.width)))) : 0
                            visible: status === Image.Ready
                            fillMode: Image.PreserveAspectFit
                            source: msg.embed ? (msg.embed.image || "") : ""
                        }
                    }
                }

                Label {
                    Layout.alignment: msg.mine ? Qt.AlignRight : Qt.AlignLeft
                    visible: msg.status.length > 0
                    textFormat: Text.PlainText
                    font.pixelSize: 10
                    color: msg.statusBad ? C.Theme.bad : C.Theme.textDim
                    text: msg.status
                }
            }
        }

        // ---- or a card
        Loader {
            id: cardLoader
            active: !!msg.card
            visible: active
            anchors.right: msg.mine ? parent.right : undefined
            width: Math.min(360, msg.width - (msg.sideAvatar ? msg.avatarSize + 20 : 12))
            sourceComponent: msg.card
        }

        // ---- the reactions
        Flow {
            visible: msg.reactions.length > 0
            anchors.right: msg.mine ? parent.right : undefined
            // A width of its own, so that the chips stand side by side (from the right for own messages)
            width: msg.maxBubble
            spacing: 4
            layoutDirection: msg.mine ? Qt.RightToLeft : Qt.LeftToRight

            Repeater {
                model: msg.reactions

                delegate: AbstractButton {
                    id: chip
                    required property var modelData
                    implicitWidth: chipRow.implicitWidth + 14
                    implicitHeight: 24
                    hoverEnabled: true
                    Accessible.name: qsTr("%1, %2").arg(modelData.emoji).arg(modelData.count)
                    onClicked: msg.react(modelData.emoji)

                    background: Rectangle {
                        radius: 12
                        color: chip.modelData.mine ? Qt.rgba(140 / 255, 82 / 255, 1, chip.hovered ? 0.42 : 0.30)
                                                   : Qt.rgba(1, 1, 1, chip.hovered ? 0.12 : 0.07)
                        border.width: 1
                        border.color: chip.modelData.mine ? Qt.rgba(167 / 255, 123 / 255, 1, 0.8) : Qt.rgba(1, 1, 1, 0.10)
                    }

                    contentItem: Item {
                        Row {
                            id: chipRow
                            anchors.centerIn: parent
                            spacing: 4

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                textFormat: Text.PlainText
                                font.pixelSize: 13
                                text: chip.modelData.emoji
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: chip.modelData.count > 1
                                textFormat: Text.PlainText
                                font.pixelSize: 11
                                font.bold: true
                                color: chip.modelData.mine ? "#ffffff" : C.Theme.textSoft
                                text: chip.modelData.count
                            }
                        }
                    }
                }
            }
        }

        // ---- who read up to here: their pictures, small, on the right
        Row {
            visible: msg.readers.length > 0
            anchors.right: parent.right
            spacing: -4
            layoutDirection: Qt.RightToLeft

            Repeater {
                model: msg.readers.slice(0, 5)

                delegate: Rectangle {
                    required property var modelData
                    width: 18
                    height: 18
                    radius: 9
                    color: C.Theme.ground

                    C.Avatar {
                        anchors.fill: parent
                        anchors.margins: 1.5
                        source: parent.modelData.avatar || ""
                        name: parent.modelData.name || "?"
                    }

                    ToolTip.visible: readHover.hovered
                    ToolTip.delay: 400
                    ToolTip.text: qsTr("Read by %1").arg(modelData.name || qsTr("a player"))

                    HoverHandler {
                        id: readHover
                    }
                }
            }

            Rectangle {
                visible: msg.readers.length > 5
                width: moreLabel.implicitWidth + 10
                height: 18
                radius: 9
                color: C.Theme.cardHigh

                Label {
                    id: moreLabel
                    anchors.centerIn: parent
                    textFormat: Text.PlainText
                    font.pixelSize: 9
                    font.bold: true
                    color: C.Theme.textSoft
                    text: "+" + (msg.readers.length - 5)
                }
            }
        }
    }

    // Lights up for a moment when a quote led here
    Rectangle {
        id: flashBox
        z: 1
        x: msg.bodyLeft - 4
        y: msg.bodyTop - 4
        width: msg.bodyWidth + 8
        height: msg.bodyHeight + 8
        radius: 18
        color: "transparent"
        border.width: 2
        border.color: msg.accentSoft
        opacity: 0

        SequentialAnimation {
            id: flash
            NumberAnimation { target: flashBox; property: "opacity"; to: 1; duration: 160 }
            PauseAnimation { duration: 700 }
            NumberAnimation { target: flashBox; property: "opacity"; to: 0; duration: 600 }
        }
    }

    component ActionButton: AbstractButton {
        id: actionButton
        property string iconName: ""
        property string emoji: ""
        property color tint: C.Theme.textSoft
        width: 28
        height: 28
        hoverEnabled: true
        ToolTip.visible: hovered && Accessible.name.length > 0
        ToolTip.delay: 500
        ToolTip.text: Accessible.name

        background: Rectangle {
            radius: 14
            color: actionButton.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent"
        }

        contentItem: Item {
            C.Icon {
                anchors.centerIn: parent
                visible: actionButton.iconName.length > 0
                iconName: actionButton.iconName
                size: 15
                color: actionButton.tint
                opacity: actionButton.hovered ? 1 : 0.8
            }

            Text {
                anchors.centerIn: parent
                visible: actionButton.emoji.length > 0
                textFormat: Text.PlainText
                font.pixelSize: 15
                opacity: actionButton.hovered ? 1 : 0.75
                text: actionButton.emoji
            }
        }
    }

    // ---- on hover: reply, react and delete beside the message (react opens the quick reactions)
    Rectangle {
        id: actions
        visible: msg.hasActions && !msg.barOpen && (hover.hovered || actionsHover.hovered)
        z: 2
        width: actionRow.implicitWidth + 4
        height: 32
        radius: 16
        color: "#1d1838"
        border.width: 1
        border.color: C.Theme.lineStrong
        // Beside the message, kept inside the chat
        x: msg.mine ? Math.max(2, msg.bodyLeft - width - 6) : Math.min(msg.width - width - 2, msg.bodyLeft + msg.bodyWidth + 6)
        y: msg.bodyTop + Math.max(0, (Math.min(msg.bodyHeight, 60) - height) / 2)

        HoverHandler {
            id: actionsHover
        }

        Row {
            id: actionRow
            anchors.centerIn: parent
            layoutDirection: msg.mine ? Qt.RightToLeft : Qt.LeftToRight

            ActionButton {
                visible: msg.canReply
                iconName: "reply"
                Accessible.name: qsTr("Reply")
                onClicked: msg.replyRequested()
            }

            ActionButton {
                id: reactButton
                visible: msg.canReact
                emoji: "🙂"
                Accessible.name: qsTr("React")
                onClicked: msg.barOpen = true
            }

            ActionButton {
                visible: msg.canDelete
                iconName: "trash"
                tint: C.Theme.bad
                Accessible.name: qsTr("Delete")
                onClicked: msg.deleteRequested()
            }
        }
    }

    // Closes once the pointer has left the message and the bar for a moment
    Timer {
        id: barClose
        interval: 700
        onTriggered: {
            if (!hover.hovered && !barHover.hovered)
                msg.barOpen = false;
        }
    }

    // ---- quick reactions, over the message
    Rectangle {
        id: bar
        visible: msg.hasActions && msg.barOpen
        z: 3
        height: 34
        width: barRow.implicitWidth + 12
        radius: 17
        color: "#1d1838"
        border.width: 1
        border.color: C.Theme.lineStrong
        y: Math.max(0, msg.bodyTop - height + 10)
        x: msg.mine ? Math.max(0, msg.bodyLeft + msg.bodyWidth - width) : Math.min(msg.width - width, msg.bodyLeft)

        HoverHandler {
            id: barHover
            onHoveredChanged: barClose.restart()
        }

        Row {
            id: barRow
            anchors.centerIn: parent
            spacing: 0

            Repeater {
                model: msg.canReact ? msg.quick : []

                delegate: AbstractButton {
                    id: quickButton
                    required property string modelData
                    width: 30
                    height: 30
                    hoverEnabled: true
                    Accessible.name: qsTr("React with %1").arg(modelData)
                    onClicked: {
                        msg.barOpen = false;
                        msg.react(modelData);
                    }

                    background: Rectangle {
                        radius: 15
                        color: quickButton.hovered ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                    }

                    contentItem: Text {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        font.pixelSize: quickButton.hovered ? 20 : 17
                        text: quickButton.modelData
                    }
                }
            }

            AbstractButton {
                id: moreButton
                visible: msg.canReact
                width: 30
                height: 30
                hoverEnabled: true
                Accessible.name: qsTr("More reactions")
                onClicked: {
                    msg.barOpen = false;
                    msg.moreReactions();
                }

                background: Rectangle {
                    radius: 15
                    color: moreButton.hovered ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
                }

                contentItem: C.Icon {
                    iconName: "plus"
                    size: 16
                    color: C.Theme.textSoft
                }
            }

            // Reply and delete, for a touch screen (no hover there)
            Item {
                visible: msg.canReact && (msg.canReply || msg.canDelete)
                width: 9
                height: 30

                Rectangle {
                    anchors.centerIn: parent
                    width: 1
                    height: 18
                    color: C.Theme.line
                }
            }

            ActionButton {
                visible: msg.canReply
                width: 30
                height: 30
                iconName: "reply"
                Accessible.name: qsTr("Reply")
                onClicked: {
                    msg.barOpen = false;
                    msg.replyRequested();
                }
            }

            ActionButton {
                visible: msg.canDelete
                width: 30
                height: 30
                iconName: "trash"
                tint: C.Theme.bad
                Accessible.name: qsTr("Delete")
                onClicked: {
                    msg.barOpen = false;
                    msg.deleteRequested();
                }
            }
        }
    }
}
