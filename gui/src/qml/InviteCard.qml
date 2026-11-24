import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Effects

import "controls" as C

// An invite to a friend's session, as a chat shows it: the game's key art as a soft band
// with the cover on top, the session's title and game, how it is played and how one gets
// in (the same badges as the public rooms), the places taken and free, whether it is open
// right now, and Join.
//
// `card` is what FriendsClient lets through (CleanRoom in friends.cpp): invite, web, title,
// host, game, cover (Sony's image server only), console, policy, control, players, max, talk.
Item {
    id: invite

    property var card: ({})
    /** Sent by this computer's player. */
    property bool mine: false
    /** Who sent it, or to whom it went. */
    property string person: ""
    /** "open" (the session is open now), "old" (sent long ago, likely over), or "" (not known). */
    property string live: ""
    /** When it was sent, as text. */
    property string time: ""
    signal join()

    readonly property bool hasCover: coverImage.status === Image.Ready
    readonly property string initial: ((card.game || card.title || "?") + "").trim().charAt(0).toUpperCase()
    readonly property int places: Math.max(0, Math.min(16, card.max || 0))
    readonly property int taken: Math.max(0, Math.min(places, card.players || 0))

    function consoleName(kind) {
        return ({ ps5: "PS5", ps4: "PS4", pc: qsTr("PC"), xbox: "Xbox" })[kind] || "";
    }

    implicitWidth: 340
    implicitHeight: shape.height

    // The card: a rounded shape, everything in it cut to its corners
    Item {
        id: shape
        width: invite.width
        height: body.y + body.height
        layer.enabled: true
        layer.smooth: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: cardMask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        Rectangle {
            anchors.fill: parent
            color: "#161231"
        }

        // ---- the band: the key art blurred, or the brand's colours
        Item {
            id: band
            width: parent.width
            height: 86
            clip: true

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#5b2fd6" }
                    GradientStop { position: 1.0; color: "#b8327f" }
                }
            }

            // Large and faint behind it: what the session is about
            C.Icon {
                visible: !invite.hasCover
                x: parent.width - 96
                y: -14
                iconName: invite.card.talk ? "mic" : "gamepad"
                size: 120
                color: Qt.rgba(1, 1, 1, 0.10)
            }

            Image {
                id: bandImage
                anchors.fill: parent
                visible: false
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                sourceSize.width: 384
                source: coverImage.source
            }

            MultiEffect {
                anchors.fill: parent
                visible: invite.hasCover
                source: bandImage
                blurEnabled: true
                blurMax: 48
                blur: 1.0
                saturation: 0.15
                brightness: -0.18
            }

            // Darker towards the bottom, where the text starts
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(0.05, 0.04, 0.12, invite.hasCover ? 0.25 : 0.0) }
                    GradientStop { position: 1.0; color: Qt.rgba(0.05, 0.04, 0.12, 0.70) }
                }
            }

            RowLayout {
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                    margins: 12
                }
                spacing: 8

                // INVITE, in the brand's colours
                Rectangle {
                    implicitWidth: inviteWord.implicitWidth + 16
                    implicitHeight: 20
                    radius: 10
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: C.Theme.brandFrom }
                        GradientStop { position: 1.0; color: C.Theme.brandTo }
                    }

                    Text {
                        textFormat: Text.PlainText
                        id: inviteWord
                        anchors.centerIn: parent
                        text: qsTr("INVITE")
                        color: "#ffffff"
                        font.pixelSize: 10
                        font.weight: Font.ExtraBold
                        font.letterSpacing: 1.2
                    }
                }

                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    color: Qt.rgba(1, 1, 1, 0.92)
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    text: invite.mine ? qsTr("You invited %1").arg(invite.person) : qsTr("%1 invites you").arg(invite.person)
                }

                // Whether it is open now
                Rectangle {
                    visible: invite.live.length > 0
                    implicitWidth: liveRow.implicitWidth + 14
                    implicitHeight: 20
                    radius: 10
                    color: invite.live === "open" ? Qt.rgba(0.05, 0.25, 0.17, 0.85) : Qt.rgba(0, 0, 0, 0.45)
                    border.width: 1
                    border.color: invite.live === "open" ? Qt.rgba(0.20, 0.83, 0.60, 0.6) : Qt.rgba(1, 1, 1, 0.15)

                    Row {
                        id: liveRow
                        anchors.centerIn: parent
                        spacing: 5

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 6
                            height: 6
                            radius: 3
                            color: invite.live === "open" ? C.Theme.good : C.Theme.textDim

                            SequentialAnimation on opacity {
                                running: invite.live === "open" && invite.visible
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.35; duration: 900; easing.type: Easing.InOutSine }
                                NumberAnimation { to: 1.0; duration: 900; easing.type: Easing.InOutSine }
                            }
                        }

                        Text {
                            textFormat: Text.PlainText
                            anchors.verticalCenter: parent.verticalCenter
                            color: invite.live === "open" ? "#bff5dd" : C.Theme.textSoft
                            font.pixelSize: 10
                            font.weight: Font.ExtraBold
                            text: invite.live === "open" ? qsTr("Open now") : qsTr("Probably over")
                        }
                    }
                }
            }
        }

        // ---- the body: cover, title, game, badges, places, Join
        Item {
            id: body
            y: band.height
            width: parent.width
            height: bodyColumn.implicitHeight + 16

            ColumnLayout {
                id: bodyColumn
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                    leftMargin: 14
                    rightMargin: 14
                }
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    // The cover, standing up into the band
                    Item {
                        Layout.preferredWidth: 72
                        Layout.preferredHeight: 72
                        Layout.topMargin: -34
                        Layout.alignment: Qt.AlignTop

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -3
                            radius: 17
                            color: "#161231"
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: 14
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: C.Theme.brandFrom }
                                GradientStop { position: 1.0; color: C.Theme.brandTo }
                            }

                            Text {
                                anchors.centerIn: parent
                                visible: !invite.hasCover && !invite.card.talk
                                textFormat: Text.PlainText
                                font.pixelSize: 30
                                font.weight: Font.ExtraBold
                                color: "#ffffff"
                                text: invite.initial
                            }

                            C.Icon {
                                anchors.centerIn: parent
                                visible: !invite.hasCover && !!invite.card.talk
                                iconName: "mic"
                                size: 30
                                color: "#ffffff"
                            }
                        }

                        Image {
                            id: coverImage
                            anchors.fill: parent
                            visible: false
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.width: 144
                            sourceSize.height: 144
                            source: invite.card.cover ? invite.card.cover + "?w=192" : ""
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
                            visible: invite.hasCover
                            source: coverImage
                            maskEnabled: true
                            maskSource: coverMask
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 8
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            color: C.Theme.text
                            font.pixelSize: 17
                            font.weight: Font.ExtraBold
                            text: invite.card.title || qsTr("A session")
                        }

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            textFormat: Text.PlainText
                            color: C.Theme.textSoft
                            font.pixelSize: 13
                            text: invite.card.talk ? qsTr("Just talking, no game")
                                  : invite.card.game ? invite.card.game
                                  : invite.card.host ? qsTr("Hosted by %1").arg(invite.card.host) : ""
                            visible: text.length > 0
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 5

                    C.Badge {
                        small: true
                        tone: "system"
                        visible: text !== ""
                        text: invite.consoleName(invite.card.console)
                    }
                    C.Badge {
                        small: true
                        tone: "play"
                        visible: !invite.card.talk && !!invite.card.control
                        text: invite.card.control === "turns" ? qsTr("One plays at a time")
                            : invite.card.control === "host" ? qsTr("Only the host plays")
                            : invite.card.control === "each" ? qsTr("Each their own controller")
                            : qsTr("Everybody plays together")
                    }
                    C.Badge {
                        small: true
                        tone: "door"
                        visible: !!invite.card.policy
                        text: invite.card.policy === "ask" ? qsTr("Ask to join")
                            : invite.card.talk ? qsTr("Join and talk")
                            : invite.card.policy === "watch" ? qsTr("Join as viewer") : qsTr("Join and play")
                    }
                    C.Badge {
                        small: true
                        tone: "web"
                        visible: !!invite.card.web
                        text: qsTr("Browsers welcome")
                    }
                }

                // The places: taken ones filled, free ones open
                RowLayout {
                    Layout.fillWidth: true
                    visible: invite.places > 0
                    spacing: 8

                    Row {
                        spacing: 4

                        Repeater {
                            model: invite.places

                            delegate: Rectangle {
                                required property int index
                                width: 12
                                height: 12
                                radius: 6
                                color: index < invite.taken ? "transparent" : Qt.rgba(1, 1, 1, 0.06)
                                border.width: index < invite.taken ? 0 : 1
                                border.color: Qt.rgba(1, 1, 1, 0.22)
                                gradient: index < invite.taken ? takenGradient : null

                                Gradient {
                                    id: takenGradient
                                    GradientStop { position: 0.0; color: C.Theme.brandFrom }
                                    GradientStop { position: 1.0; color: C.Theme.brandTo }
                                }
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        color: C.Theme.textSoft
                        font.pixelSize: 12
                        text: invite.taken >= invite.places ? qsTr("Full: %1 / %2").arg(invite.taken).arg(invite.places)
                              : qsTr("%1 / %2 · %3 free").arg(invite.taken).arg(invite.places).arg(invite.places - invite.taken)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    C.PillButton {
                        visible: !invite.mine
                        Layout.fillWidth: true
                        implicitHeight: 40
                        kind: invite.live === "old" ? "ghost" : "success"
                        iconName: "enter"
                        text: invite.live === "old" ? qsTr("Try to join") : qsTr("Join")
                        Accessible.description: invite.card.title || ""
                        onClicked: invite.join()
                    }

                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: invite.mine
                        horizontalAlignment: invite.mine ? Text.AlignRight : Text.AlignLeft
                        color: C.Theme.textDim
                        font.pixelSize: 10
                        text: invite.time
                    }
                }
            }
        }
    }

    Rectangle {
        id: cardMask
        width: shape.width
        height: shape.height
        radius: 18
        visible: false
        layer.enabled: true
    }

    // A thin edge in the brand's colour over the cut shape
    Rectangle {
        width: shape.width
        height: shape.height
        radius: 18
        color: "transparent"
        border.width: 1
        border.color: invite.live === "open" ? Qt.rgba(0.20, 0.83, 0.60, 0.45) : Qt.rgba(167 / 255, 123 / 255, 1, 0.38)
    }
}
