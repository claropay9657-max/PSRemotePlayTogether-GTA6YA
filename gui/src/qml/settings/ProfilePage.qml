import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Your profile: the name and picture the other players see, whether you host or join,
// and a look at the places where they see them.
C.SettingsPage {
    id: page
    title: qsTr("Your profile")
    description: qsTr("How the other players see you, whether you host or join.")

    /** The name as it is typed; it is saved when the field is left. */
    readonly property string typedName: nameField.text.trim()
    /** Without a name the others see "Host" (or "Somebody" when you join). */
    readonly property string shownName: typedName || qsTr("Host")
    readonly property string picture: Chiaki.settings.coplayAvatarUrl || ""
    /** A game one of the consoles is running, for the line in the public rooms. */
    readonly property string game: {
        const hosts = Chiaki.hosts || [];
        for (let i = 0; i < hosts.length; ++i) {
            if (hosts[i] && hosts[i].app)
                return hosts[i].app;
        }
        return "";
    }
    /** Two cards side by side where there is room for both. */
    readonly property bool wide: width >= 740
    readonly property real columnWidth: wide ? Math.floor((width - 20) / 2) : width

    function escaped(text) {
        return String(text).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
    }

    /** The small grey line over each example. */
    component Caption: Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        color: C.Theme.textDim
        font.pixelSize: 12
        font.weight: Font.Bold
    }

    /** Three bars going up and down: somebody is talking (as in the players panel). */
    component TalkBars: Item {
        id: bars
        property real phase: 0
        implicitWidth: 13
        implicitHeight: 11

        NumberAnimation on phase {
            from: 0
            to: 1
            duration: 700
            loops: Animation.Infinite
            running: bars.visible
        }

        Repeater {
            model: 3

            Rectangle {
                id: bar
                required property int index
                x: bar.index * 5
                width: 3
                radius: 1.5
                height: Math.round(bars.height * (0.45 + 0.55 * (0.5 + 0.5 * Math.sin(2 * Math.PI * (bars.phase - bar.index * 0.29)))))
                y: bars.height - bar.height
                color: C.Theme.good
            }
        }
    }

    Item {
        Layout.fillWidth: true
        implicitHeight: page.wide ? Math.max(meGroup.implicitHeight, seenGroup.implicitHeight)
                                  : meGroup.implicitHeight + 20 + seenGroup.implicitHeight

        // ------------------------------------------------ name and picture

        C.SettingsGroup {
            id: meGroup
            width: page.columnWidth
            title: qsTr("Name and picture")

            Item {
                // Found by the search like a row
                readonly property bool isSettingRow: true
                readonly property string title: qsTr("Your picture")
                readonly property string subtitle: qsTr("A square picture works best. Without one, the others see your initial.")
                readonly property string keywords: qsTr("avatar photo image profile picture choose remove")

                Layout.fillWidth: true
                implicitHeight: pictureColumn.implicitHeight + 26 + 6

                ColumnLayout {
                    id: pictureColumn
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        topMargin: 26
                        leftMargin: 22
                        rightMargin: 22
                    }
                    spacing: 18

                    Item {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 8
                        implicitWidth: 128
                        implicitHeight: 128

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -8
                            radius: width / 2
                            color: "transparent"
                            border.width: 2
                            border.color: Qt.rgba(140 / 255, 82 / 255, 1, 0.55)
                        }

                        C.Avatar {
                            anchors.fill: parent
                            source: page.picture
                            name: page.shownName
                        }

                        // The same as "Choose picture…", for the mouse: the button below has the keys
                        QQC.AbstractButton {
                            id: cameraButton
                            x: parent.width - width - 2
                            y: parent.height - height - 2
                            width: 40
                            height: 40
                            focusPolicy: Qt.NoFocus
                            hoverEnabled: true
                            Accessible.name: qsTr("Choose a picture")
                            QQC.ToolTip.visible: hovered
                            QQC.ToolTip.text: qsTr("Choose a picture")
                            QQC.ToolTip.delay: 600
                            onClicked: Chiaki.settings.coplayChooseAvatar()

                            background: Rectangle {
                                radius: width / 2
                                color: cameraButton.down ? Qt.lighter(C.Theme.button, 1.15)
                                     : cameraButton.hovered ? Qt.lighter(C.Theme.button, 1.08) : C.Theme.button
                                border.width: 3
                                border.color: C.Theme.card
                            }

                            contentItem: Item {
                                C.Icon {
                                    anchors.centerIn: parent
                                    iconName: "camera"
                                    size: 17
                                    color: "#ffffff"
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8

                        C.PillButton {
                            id: chooseButton
                            kind: "primary"
                            text: qsTr("Choose picture…")
                            onClicked: Chiaki.settings.coplayChooseAvatar()
                        }

                        C.PillButton {
                            id: removeButton
                            visible: page.picture.length > 0
                            text: qsTr("Remove")
                            onClicked: {
                                const hadFocus = removeButton.activeFocus;
                                Chiaki.settings.coplayClearAvatar();
                                // The button goes away with the picture: the keys stay on the page
                                if (hadFocus && !removeButton.visible)
                                    chooseButton.forceActiveFocus(Qt.TabFocusReason);
                            }
                        }
                    }
                }
            }

            Item {
                // Found by the search like a row
                readonly property bool isSettingRow: true
                readonly property string title: qsTr("Your name")
                readonly property string subtitle: nameHint.text
                readonly property string keywords: qsTr("nickname player name gamertag")

                Layout.fillWidth: true
                implicitHeight: nameColumn.implicitHeight + 14 + 22

                ColumnLayout {
                    id: nameColumn
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        topMargin: 14
                        leftMargin: 22
                        rightMargin: 22
                    }
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Your name")
                        color: C.Theme.textSoft
                        font.pixelSize: 13
                        font.weight: Font.Bold
                    }

                    C.Field {
                        id: nameField
                        Layout.fillWidth: true
                        implicitHeight: 46
                        Accessible.name: qsTr("Your name")
                        font.pixelSize: 17
                        font.weight: Font.ExtraBold
                        // As long as the others get it (coplay::kMaxNameLength)
                        maximumLength: 32
                        hint: qsTr("Your name, for the other players")
                        text: Chiaki.settings.coplayName
                        onEditingFinished: Chiaki.settings.coplayName = text
                    }

                    Text {
                        id: nameHint
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("No name? The others see “Host” when you host, and “Somebody” when you join.")
                        color: C.Theme.textDim
                        font.pixelSize: 12
                    }
                }
            }
        }

        // ------------------------------------------------ how the others see you

        C.SettingsGroup {
            id: seenGroup
            x: page.wide ? page.columnWidth + 20 : 0
            y: page.wide ? 0 : meGroup.implicitHeight + 20
            width: page.columnWidth
            title: qsTr("How the others see you")

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 18
                spacing: 16

                // A tile of the players panel, talking
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Caption { text: qsTr("In the players panel") }

                    Item {
                        implicitWidth: 200
                        implicitHeight: 112

                        // Talking: a soft green glow and a green frame
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -5
                            radius: 17
                            color: "transparent"
                            border.width: 4
                            border.color: Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.22)
                        }

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: -2
                            radius: 14
                            color: C.Theme.good
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: 12
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: "#3b2a73" }
                                GradientStop { position: 1.0; color: "#1a1538" }
                            }
                        }

                        C.Avatar {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 16
                            width: 54
                            height: 54
                            source: page.picture
                            name: page.shownName
                        }

                        // The name along the bottom, rounded with the tile
                        Item {
                            anchors {
                                left: parent.left
                                right: parent.right
                                bottom: parent.bottom
                            }
                            height: 26
                            clip: true

                            Rectangle {
                                anchors {
                                    left: parent.left
                                    right: parent.right
                                    bottom: parent.bottom
                                }
                                height: parent.height + 12
                                radius: 12
                                color: Qt.rgba(0, 0, 0, 0.62)
                            }

                            RowLayout {
                                anchors {
                                    fill: parent
                                    leftMargin: 9
                                    rightMargin: 9
                                }
                                spacing: 6

                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    text: page.shownName
                                    color: C.Theme.text
                                    font.pixelSize: 13
                                    font.weight: Font.ExtraBold
                                }

                                TalkBars {
                                    Accessible.name: qsTr("Talking")
                                }
                            }
                        }
                    }
                }

                // A player on the game picture, and in the OBS overlay
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Caption { text: qsTr("On the game picture and in OBS") }

                    Rectangle {
                        Layout.maximumWidth: parent.width
                        implicitWidth: chipRow.implicitWidth + 16
                        implicitHeight: 36
                        radius: height / 2
                        color: Qt.rgba(0, 0, 0, 0.45)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.10)

                        RowLayout {
                            id: chipRow
                            anchors {
                                left: parent.left
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 3
                                rightMargin: 13
                            }
                            spacing: 8

                            C.Avatar {
                                implicitWidth: 30
                                implicitHeight: 30
                                source: page.picture
                                name: page.shownName
                                ringColor: C.Theme.good
                                ringWidth: 2
                            }

                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: page.shownName
                                color: C.Theme.text
                                font.pixelSize: 13
                                font.weight: Font.ExtraBold
                            }
                        }
                    }
                }

                // A room in the list, and a line of the chat
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Caption { text: qsTr("In public rooms and the chat") }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        textFormat: Text.StyledText
                        color: "#e7e4fa"
                        font.pixelSize: 13
                        text: page.game
                              ? qsTr("%1 plays %2").arg("<b>" + page.escaped(page.shownName) + "</b>").arg(page.escaped(page.game))
                              : qsTr("Hosted by %1").arg("<b>" + page.escaped(page.shownName) + "</b>")
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        textFormat: Text.StyledText
                        color: "#e7e4fa"
                        font.pixelSize: 13
                        // The host's colour in the chat
                        text: "<b><font color=\"#b18cff\">" + page.escaped(page.shownName) + "</font></b>  "
                              + page.escaped(qsTr("hi everyone, ready when you are"))
                    }
                }
            }
        }
    }
}
