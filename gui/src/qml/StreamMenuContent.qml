import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

// The stream menu (⌘O / Ctrl+O, or the controller combo): the connection, Play Together,
// sound, picture and quality, on one floating sheet. Shown inside the stream window, or
// with Vulkan in a window of its own (StreamMenuWindow). Every control can be reached
// with the arrow keys / D-pad; Escape goes back to the game.
FocusScope {
    id: menu

    /** Gets the focus when the menu opens. */
    readonly property Item initialFocusItem: resumeButton

    signal closeRequested()
    signal displaySettingsRequested()
    signal placeboSettingsRequested()
    signal displayPanelRequested()
    signal mainViewRequested()
    signal chatRequested()
    /** Drag the things on the game picture into place. */
    signal arrangeRequested()

    readonly property color accent: "#8c52ff"
    readonly property color pink: "#f0469b"
    readonly property color cyan: "#00f2ff"
    readonly property color green: "#34d399"
    readonly property color amber: "#fbbf24"
    readonly property color red: "#f0466e"

    readonly property var session: Chiaki.session
    readonly property bool connected: !!session && session.connected
    readonly property bool guest: !!session && session.coplayGuest
    readonly property bool together: !!session && session.coplayActive
    /** Where the up key goes from the bottom row. */
    readonly property Item upFromControls: menu.together ? coplayBar.firstItem : resumeButton
    /**
     * How tall the menu wants to be. The controls at the bottom take a second row when the
     * window is too narrow for one, and the menu grows with them instead of cutting them off.
     */
    readonly property int wantedHeight: Math.ceil(sheetColumn.implicitHeight)
        + sheet.anchors.topMargin + sheet.anchors.bottomMargin
        + sheetColumn.anchors.topMargin + sheetColumn.anchors.bottomMargin

    Keys.onEscapePressed: closeRequested()
    Keys.onMenuPressed: closeRequested()

    function setVolume(v) {
        Chiaki.settings.audioVolume = Math.max(0, Math.min(128, Math.round(v)));
    }

    function setZoom(v) {
        const z = Math.max(-1, Math.min(4, Math.round(v * 100) / 100));
        Chiaki.window.ZoomFactor = z;
        Chiaki.settings.sZoomFactor = z;
    }

    function leave() {
        if (Chiaki.session)
            Chiaki.window.close(); // asks first, depending on the settings
        else
            mainViewRequested();
    }

    // ------------------------------------------------------------ building blocks

    component MenuButton: ToolButton {
        id: mb
        /** A colour that stands for a state: muted, live, leaving. */
        property color tint: "transparent"
        /** One option of a group that is the chosen one. */
        property bool selected: false
        padding: 8
        leftPadding: 14
        rightPadding: 14
        font.pixelSize: 14
        font.bold: true
        focusPolicy: Qt.StrongFocus
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        contentItem: Label {
            text: mb.text
            font: mb.font
            color: "white"
            opacity: mb.enabled ? 1 : 0.4
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            implicitHeight: 36
            implicitWidth: 36
            radius: 10
            color: mb.selected ? menu.accent
                 : mb.tint.a > 0 ? mb.tint
                 : Qt.rgba(1, 1, 1, mb.down ? 0.24 : (mb.hovered || mb.activeFocus) ? 0.16 : 0.08)
            border.width: mb.activeFocus ? 2 : 1
            border.color: mb.activeFocus ? menu.cyan : (mb.selected ? Qt.lighter(menu.accent, 1.35) : Qt.rgba(1, 1, 1, 0.12))
        }
    }

    component Card: Rectangle {
        id: card
        default property alias content: body.data
        property string title
        radius: 14
        color: Qt.rgba(1, 1, 1, 0.045)
        border.color: Qt.rgba(1, 1, 1, 0.09)
        implicitWidth: Math.max(body.implicitWidth, cardTitle.implicitWidth) + 28
        implicitHeight: cardTitle.implicitHeight + body.implicitHeight + 28

        Label {
            id: cardTitle
            x: 14
            y: 10
            text: card.title
            font.pixelSize: 11
            font.bold: true
            font.letterSpacing: 1.4
            opacity: 0.55
        }

        RowLayout {
            id: body
            x: 14
            anchors.top: cardTitle.bottom
            anchors.topMargin: 8
            spacing: 8
        }
    }

    component Chip: Rectangle {
        id: chip
        property alias text: chipLabel.text
        property color tone: Qt.rgba(1, 1, 1, 0.8)
        radius: height / 2
        implicitHeight: 24
        implicitWidth: chipLabel.implicitWidth + 18
        color: Qt.rgba(1, 1, 1, 0.07)
        border.color: Qt.rgba(1, 1, 1, 0.1)

        Label {
            id: chipLabel
            anchors.centerIn: parent
            font.pixelSize: 12
            font.bold: true
            color: chip.tone
        }
    }

    component Bar: Slider {
        id: bs
        // Mouse only: the buttons beside it are for the keys and the controller
        focusPolicy: Qt.NoFocus
        Layout.preferredWidth: 140
        background: Rectangle {
            x: bs.leftPadding
            y: bs.topPadding + bs.availableHeight / 2 - height / 2
            width: bs.availableWidth
            height: 6
            radius: 3
            color: Qt.rgba(1, 1, 1, 0.14)

            Rectangle {
                width: bs.visualPosition * parent.width
                height: parent.height
                radius: 3
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: menu.accent }
                    GradientStop { position: 1.0; color: menu.pink }
                }
            }
        }
        handle: Rectangle {
            x: bs.leftPadding + bs.visualPosition * (bs.availableWidth - width)
            y: bs.topPadding + bs.availableHeight / 2 - height / 2
            width: 16
            height: 16
            radius: 8
            color: "white"
            border.color: menu.accent
            border.width: bs.pressed ? 4 : 2
        }
    }

    // ------------------------------------------------------------ the sheet

    Rectangle {
        id: sheet
        anchors {
            fill: parent
            leftMargin: 16
            rightMargin: 16
            topMargin: 6
            bottomMargin: 14
        }
        radius: 22
        color: Qt.rgba(13 / 255, 11 / 255, 31 / 255, 0.97)
        border.color: Qt.rgba(1, 1, 1, 0.12)

        // A soft accent line along the top edge
        Rectangle {
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
                leftMargin: 22
                rightMargin: 22
            }
            height: 2
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.3; color: Qt.rgba(140 / 255, 82 / 255, 1, 0.8) }
                GradientStop { position: 0.7; color: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.8) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }

        ColumnLayout {
            id: sheetColumn
            anchors {
                fill: parent
                leftMargin: 18
                rightMargin: 18
                topMargin: 14
                bottomMargin: 16
            }
            spacing: 12

            // ---------------------------------------- connection and leaving
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.preferredWidth: 10
                    Layout.preferredHeight: 10
                    radius: 5
                    color: Chiaki.reconnecting || !menu.connected ? menu.amber : menu.green

                    SequentialAnimation on opacity {
                        running: Chiaki.reconnecting || (!!menu.session && !menu.connected)
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { to: 0.3; duration: 600 }
                        NumberAnimation { to: 1.0; duration: 600 }
                    }
                }

                Label {
                    Layout.maximumWidth: 420
                    elide: Text.ElideRight
                    font.pixelSize: 16
                    font.bold: true
                    textFormat: Text.StyledText
                    text: {
                        if (!menu.session)
                            return qsTr("Not connected");
                        if (Chiaki.reconnecting)
                            return Chiaki.reconnectStatus;
                        const where = Chiaki.settings.streamerMode ? qsTr("hidden") : menu.session.host;
                        if (menu.guest)
                            return menu.connected ? qsTr("Playing at %1").arg(where) : qsTr("Joining %1…").arg(where);
                        return menu.connected ? qsTr("Connected to %1").arg(where) : qsTr("Connecting to %1…").arg(where);
                    }
                }

                Chip {
                    visible: menu.connected
                    tone: menu.cyan
                    text: qsTr("%1 Mbps").arg(menu.connected ? menu.session.measuredBitrate.toFixed(1) : "0")
                }

                Chip {
                    readonly property real loss: menu.connected && isFinite(menu.session.averagePacketLoss) ? menu.session.averagePacketLoss * 100 : 0
                    visible: menu.connected
                    tone: loss >= 1 ? menu.red : Qt.rgba(1, 1, 1, 0.8)
                    text: qsTr("%1 % loss").arg(loss.toFixed(1))
                }

                Chip {
                    visible: menu.connected && Chiaki.window.droppedFrames > 0
                    tone: menu.red
                    text: qsTr("%1 dropped").arg(Chiaki.window.droppedFrames)
                }

                Item { Layout.fillWidth: true }

                MenuButton {
                    id: chatButton
                    visible: !!menu.session && menu.session.coplayActive
                    readonly property int unread: menu.session ? menu.session.coplayChatUnread : 0
                    text: qsTr("Chat")
                    onClicked: menu.chatRequested()
                    KeyNavigation.right: resumeButton
                    KeyNavigation.down: menu.upFromControls === resumeButton ? volDown : menu.upFromControls

                    Rectangle {
                        visible: chatButton.unread > 0
                        anchors {
                            right: parent.right
                            top: parent.top
                            rightMargin: -6
                            topMargin: -6
                        }
                        width: Math.max(20, unreadLabel.implicitWidth + 10)
                        height: 20
                        radius: 10
                        color: menu.pink
                        Label {
                            id: unreadLabel
                            anchors.centerIn: parent
                            font.pixelSize: 11
                            font.bold: true
                            text: chatButton.unread > 99 ? "99+" : chatButton.unread
                        }
                    }
                }

                MenuButton {
                    id: resumeButton
                    text: qsTr("Back to game")
                    onClicked: menu.closeRequested()
                    KeyNavigation.left: chatButton.visible ? chatButton : null
                    KeyNavigation.right: leaveButton
                    KeyNavigation.down: menu.upFromControls === resumeButton ? volDown : menu.upFromControls
                }

                MenuButton {
                    id: leaveButton
                    text: menu.session ? (menu.guest ? qsTr("Leave") : qsTr("End session")) : qsTr("Main screen")
                    tint: Qt.rgba(0.94, 0.27, 0.43, 0.55)
                    onClicked: menu.leave()
                    KeyNavigation.left: resumeButton
                    KeyNavigation.down: menu.upFromControls === resumeButton ? volDown : menu.upFromControls
                }
            }

            // ---------------------------------------- Play Together
            Rectangle {
                Layout.fillWidth: true
                // Without Play Together in this session there is only a line of text in it
                Layout.preferredHeight: menu.together ? 166 : 84
                // Not coplayBar.visible: that is false while this is hidden, and it would never show again
                visible: !!menu.session
                radius: 14
                color: Qt.rgba(1, 1, 1, 0.045)
                border.color: Qt.rgba(1, 1, 1, 0.09)

                Label {
                    id: togetherTitle
                    x: 14
                    y: 10
                    // Says whose microphone this is: the one below, under Sound, talks to the console
                    text: menu.together ? qsTr("PLAY TOGETHER  ·  VOICE AND WEBCAM WITH YOUR FRIENDS") : qsTr("PLAY TOGETHER")
                    font.pixelSize: 11
                    font.bold: true
                    font.letterSpacing: 1.4
                    opacity: 0.55
                }

                PlayTogetherBar {
                    id: coplayBar
                    anchors {
                        fill: parent
                        margins: 10
                        topMargin: 32
                    }
                    upItem: resumeButton
                    downItem: volDown
                    onCloseRequested: menu.closeRequested()
                }
            }

            // ---------------------------------------- sound, picture, quality, overlays
            // One row in a wide window, two in a narrow one: see wantedHeight
            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: controls.implicitHeight
                implicitHeight: controls.implicitHeight

                Flow {
                    id: controls
                    width: parent.width
                    spacing: 12

                    Card {
                        title: qsTr("SOUND")

                        MenuButton {
                            id: volDown
                            text: "−"
                            onClicked: menu.setVolume(Chiaki.settings.audioVolume - 8)
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.right: volUp
                        }

                        Bar {
                            Layout.preferredWidth: 110
                            from: 0
                            to: 128
                            stepSize: 1
                            value: Chiaki.settings.audioVolume
                            onMoved: Chiaki.settings.audioVolume = value
                        }

                        MenuButton {
                            id: volUp
                            text: "+"
                            onClicked: menu.setVolume(Chiaki.settings.audioVolume + 8)
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: volDown
                            KeyNavigation.right: muteButton.visible ? muteButton : fitButton
                        }

                        Label {
                            Layout.preferredWidth: 44
                            horizontalAlignment: Text.AlignRight
                            font.pixelSize: 14
                            font.bold: true
                            text: Math.round(Chiaki.settings.audioVolume / 128 * 100) + " %"
                        }

                        MenuButton {
                            id: muteButton
                            // The console's own microphone; voice chat has its own switch above
                            visible: !menu.guest
                            enabled: menu.connected
                            text: menu.session && !menu.session.muted ? qsTr("Console mic on") : qsTr("Console mic off")
                            selected: menu.connected && !menu.session.muted
                            onClicked: menu.session.muted = !menu.session.muted
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: volUp
                            KeyNavigation.right: fitButton
                        }
                    }

                    Card {
                        title: qsTr("PICTURE")

                        MenuButton {
                            id: fitButton
                            text: qsTr("Fit")
                            selected: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Normal
                            onClicked: Chiaki.window.videoMode = ChiakiWindow.VideoMode.Normal
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: muteButton.visible ? muteButton : volUp
                            KeyNavigation.right: zoomButton
                        }

                        MenuButton {
                            id: zoomButton
                            text: qsTr("Zoom")
                            selected: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom
                            onClicked: Chiaki.window.videoMode = ChiakiWindow.VideoMode.Zoom
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: fitButton
                            KeyNavigation.right: stretchButton
                        }

                        MenuButton {
                            id: stretchButton
                            text: qsTr("Stretch")
                            selected: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Stretch
                            onClicked: Chiaki.window.videoMode = ChiakiWindow.VideoMode.Stretch
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: zoomButton
                            KeyNavigation.right: zoomOut.visible ? zoomOut : qualityCombo
                        }

                        Rectangle {
                            visible: zoomOut.visible
                            Layout.leftMargin: 4
                            Layout.rightMargin: 4
                            Layout.preferredWidth: 1
                            Layout.preferredHeight: 24
                            color: Qt.rgba(1, 1, 1, 0.15)
                        }

                        MenuButton {
                            id: zoomOut
                            visible: Chiaki.window.videoMode == ChiakiWindow.VideoMode.Zoom
                            text: "−"
                            onClicked: menu.setZoom(Chiaki.window.ZoomFactor - 0.1)
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: stretchButton
                            KeyNavigation.right: zoomIn
                        }

                        Bar {
                            visible: zoomOut.visible
                            Layout.preferredWidth: 110
                            from: -1
                            to: 4
                            stepSize: 0.01
                            value: Chiaki.window.ZoomFactor
                            onMoved: menu.setZoom(value)
                        }

                        MenuButton {
                            id: zoomIn
                            visible: zoomOut.visible
                            text: "+"
                            onClicked: menu.setZoom(Chiaki.window.ZoomFactor + 0.1)
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: zoomOut
                            KeyNavigation.right: qualityCombo
                        }

                        Label {
                            visible: zoomOut.visible
                            Layout.preferredWidth: 96
                            font.pixelSize: 13
                            font.bold: true
                            text: {
                                const z = Chiaki.window.ZoomFactor;
                                if (z === -1)
                                    return qsTr("No black bars");
                                return (z >= 0 ? z + 1 : z).toFixed(2) + " ×";
                            }
                        }
                    }

                    Card {
                        title: qsTr("QUALITY")

                        ComboBox {
                            // The opened list is never cut off: as wide as its longest entry, and inside the window
                            implicitContentWidthPolicy: ComboBox.WidestText
                            popup.width: Math.max(width, implicitContentWidth + leftPadding + rightPadding + 24)
                            popup.margins: 8
                            id: qualityCombo
                            Layout.preferredWidth: 190
                            Layout.preferredHeight: 40
                            font.pixelSize: 14
                            // VideoPreset: Fast (not offered), Default, HighQuality, HighQualitySpatial,
                            // HighQualityAdvancedSpatial, Custom
                            model: [qsTr("Default"), qsTr("High quality"), qsTr("HQ + spatial"),
                                    qsTr("HQ + advanced spatial"), qsTr("Custom")]
                            currentIndex: Math.max(0, Chiaki.window.videoPreset - 1)
                            onActivated: (index) => {
                                Chiaki.window.videoPreset = index + 1;
                                Chiaki.settings.videoPreset = index + 1;
                            }
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: zoomIn.visible ? zoomIn : stretchButton
                            KeyNavigation.right: displaySettingsButton
                        }

                        MenuButton {
                            id: displaySettingsButton
                            text: qsTr("Display…")
                            onClicked: menu.displaySettingsRequested()
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: qualityCombo
                            KeyNavigation.right: placeboButton.visible ? placeboButton : hudPresetCombo
                        }

                        MenuButton {
                            id: placeboButton
                            visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                            text: qsTr("Placebo…")
                            onClicked: menu.placeboSettingsRequested()
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: displaySettingsButton
                            KeyNavigation.right: hudPresetCombo
                        }
                    }

                    Card {
                        title: qsTr("OVERLAYS")

                        // What is shown on the game picture: switch between the saved arrangements
                        ComboBox {
                            // The opened list is never cut off: as wide as its longest entry, and inside the window
                            implicitContentWidthPolicy: ComboBox.WidestText
                            popup.width: Math.max(width, implicitContentWidth + leftPadding + rightPadding + 24)
                            popup.margins: 8
                            id: hudPresetCombo
                            Layout.preferredWidth: 230
                            Layout.preferredHeight: 40
                            font.pixelSize: 14
                            model: Chiaki.hud ? Chiaki.hud.presets : []
                            currentIndex: Chiaki.hud ? Chiaki.hud.active : 0
                            onActivated: (index) => Chiaki.hud.active = index
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: placeboButton.visible ? placeboButton : displaySettingsButton
                            KeyNavigation.right: arrangeButton
                        }

                        MenuButton {
                            id: arrangeButton
                            text: qsTr("Arrange")
                            onClicked: menu.arrangeRequested()
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: hudPresetCombo
                            KeyNavigation.right: overlayMoreButton
                        }

                        MenuButton {
                            id: overlayMoreButton
                            // What is shown at all, on the game picture and in OBS
                            text: qsTr("Options…")
                            onClicked: menu.displayPanelRequested()
                            KeyNavigation.up: menu.upFromControls
                            KeyNavigation.left: arrangeButton
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true }
        }
    }
}
