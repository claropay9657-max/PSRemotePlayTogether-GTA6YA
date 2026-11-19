import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

// The start page: your consoles, and Play Together at hand. Everything works with
// the mouse, the keyboard and a controller: up/down picks a console, Cross plays,
// Triangle wakes, Square removes, L1 sets the console PIN, R1 refreshes PSN,
// L3 makes a Steam shortcut, R3 adds a console, Options opens the settings.
Pane {
    padding: 0
    id: consolePane
    background: C.NovaBackground { }

    readonly property color accent: "#8c52ff"
    readonly property color pink: "#f0469b"
    readonly property color cyan: "#00f2ff"
    readonly property color green: "#34d399"
    readonly property color amber: "#f5b942"
    readonly property color red: "#f0466e"
    /** Controller hints only make sense with a controller. */
    readonly property bool padConnected: Chiaki.controllers.length > 0
    readonly property bool wide: width >= 1100
    readonly property int shownConsoles: {
        let n = 0;
        for (let i = 0; i < Chiaki.hosts.length; ++i) {
            if (Chiaki.hosts[i].display)
                n++;
        }
        return n;
    }

    StackView.onActivated: {
        forceActiveFocus(Qt.TabFocusReason);
        if(!Chiaki.autoConnect && !root.initialAsk && !Chiaki.window.directStream)
        {
            root.initialAsk = true;
            if(Chiaki.settings.addSteamShortcutAsk && (typeof Chiaki.createSteamShortcut === "function"))
                root.showRemindDialog(qsTr("Official Steam artwork + controller layout"), qsTr("Would you like to either create a new non-Steam game for PSRemotePlayTogether\nor update an existing non-Steam game with the official artwork and controller layout?") + "\n\n" + qsTr("(Note: If you select no now and want to do this later, click the button or press L3 from the main menu.)"), false, () => root.showSteamShortcutDialog(true));
            else if(Chiaki.settings.remotePlayAsk)
            {
                if(!Chiaki.settings.psnRefreshToken || !Chiaki.settings.psnAuthToken || !Chiaki.settings.psnAuthTokenExpiry || !Chiaki.settings.psnAccountId)
                    root.showRemindDialog(qsTr("Remote Play via PSN"), qsTr("Would you like to connect to PSN?\nThis enables:\n- Automatic registration\n- Playing outside of your home network without port forwarding?") + "\n\n" + qsTr("(Note: If you select no now and want to do this later, go to the Config section of the settings.)"), true, () => root.showPSNTokenDialog(false));
                else
                    Chiaki.settings.remotePlayAsk = false;
            }
        }
    }
    Keys.onUpPressed: {
        if(hostsView.currentItem && hostsView.currentItem.visible)
        {
            hostsView.decrementCurrentIndex()
            while(!hostsView.currentItem.visible)
                hostsView.decrementCurrentIndex()
        }
    }
    Keys.onDownPressed: {
        if(hostsView.currentItem && hostsView.currentItem.visible)
        {
            hostsView.incrementCurrentIndex()
            while(!hostsView.currentItem.visible)
                 hostsView.incrementCurrentIndex()
        }
    }
    Keys.onMenuPressed: settingsButton.clicked()
    Keys.onReturnPressed: if (hostsView.currentItem) hostsView.currentItem.connectToHost()
    Keys.onYesPressed: if (hostsView.currentItem) hostsView.currentItem.wakeUpHost()
    Keys.onNoPressed: if (hostsView.currentItem) hostsView.currentItem.deleteHost()
    Keys.onEscapePressed: root.showConfirmDialog(qsTr("Quit"), qsTr("Are you sure you want to quit?"), () => Qt.quit())
    Keys.onPressed: (event) => {
        if (event.modifiers)
            return;
        switch (event.key) {
        case Qt.Key_PageUp:
            if (hostsView.currentItem) hostsView.currentItem.setConsolePin();
            event.accepted = true;
            break;
        case Qt.Key_PageDown:
            if (Chiaki.settings.psnAuthToken) Chiaki.refreshPsnToken();
            event.accepted = true;
            break;
        case Qt.Key_F1:
            if (typeof Chiaki.createSteamShortcut === "function") root.showSteamShortcutDialog(false);
            event.accepted = true;
            break;
        case Qt.Key_F2:
            root.showManualHostDialog();
            event.accepted = true;
            break;
        case Qt.Key_F3:
            root.showPlayTogetherDialog();
            event.accepted = true;
            break;
        }
    }

    // ------------------------------------------------------------ building blocks

    // A small labelled pill: the state of a console at a glance
    component Chip: Rectangle {
        id: chip
        property string text
        property color dot: "transparent"
        height: 24
        width: chipContent.implicitWidth + 18
        radius: 12
        color: Qt.rgba(1, 1, 1, 0.07)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.09)

        Row {
            id: chipContent
            anchors.centerIn: parent
            spacing: 6

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: chip.dot.a > 0
                width: 7
                height: 7
                radius: 3.5
                color: chip.dot
            }

            Label {
                font.pixelSize: 12
                font.bold: true
                text: chip.text
            }
        }
    }

    // A button of the page: a pill, filled for the main action, with an optional
    // controller button drawn in front of the text while a controller is connected
    component PageButton: Button {
        id: pb
        property bool primary: false
        property string hint: ""
        property bool showHint: consolePane.padConnected && hint.length > 0
        property color tint: "transparent"
        focusPolicy: Qt.NoFocus
        font.pixelSize: 15
        font.bold: true
        leftPadding: showHint ? 44 : 18
        rightPadding: 18
        topPadding: 10
        bottomPadding: 10
        flat: true
        contentItem: Label {
            text: pb.text
            font: pb.font
            color: "white"
            opacity: pb.enabled ? 1 : 0.45
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            implicitHeight: 42
            radius: height / 2
            color: pb.primary ? (pb.down ? Qt.darker(consolePane.accent, 1.2) : pb.hovered ? Qt.lighter(consolePane.accent, 1.12) : consolePane.accent)
                 : pb.tint.a > 0 ? pb.tint
                 : Qt.rgba(1, 1, 1, pb.down ? 0.2 : pb.hovered ? 0.13 : 0.07)
            border.width: pb.primary ? 0 : 1
            border.color: Qt.rgba(1, 1, 1, 0.12)
            Behavior on color { ColorAnimation { duration: 120 } }

            Image {
                visible: pb.showHint
                anchors {
                    left: parent.left
                    leftMargin: 10
                    verticalCenter: parent.verticalCenter
                }
                width: 26
                height: 26
                sourceSize: Qt.size(width, height)
                source: pb.hint
            }
        }
    }

    component Card: Rectangle {
        radius: 20
        color: Qt.rgba(16 / 255, 14 / 255, 32 / 255, 0.78)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.08)
    }

    // ------------------------------------------------------------ header

    Item {
        id: header
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 84

        RowLayout {
            anchors {
                fill: parent
                leftMargin: 28
                rightMargin: 20
            }
            spacing: 10

            ColumnLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: 0

                C.GradientText {
                    text: "PSRemotePlayTogether"
                    font.pixelSize: consolePane.width < 760 ? 20 : 26
                    font.bold: true
                }

                Label {
                    visible: consolePane.width >= 760
                    opacity: 0.55
                    font.pixelSize: 13
                    text: qsTr("One console. One controller. Everyone plays.")
                }
            }

            Item { Layout.fillWidth: true }

            PageButton {
                visible: !consolePane.wide
                primary: true
                text: qsTr("Play Together")
                onClicked: root.showPlayTogetherDialog()
            }

            PageButton {
                text: consolePane.width < 760 ? "+" : qsTr("Add console")
                hint: "qrc:/icons/r3.svg"
                onClicked: root.showManualHostDialog()
            }

            PageButton {
                id: moreButton
                text: "⋯"
                font.pixelSize: 20
                visible: Chiaki.settings.psnAuthToken || typeof Chiaki.createSteamShortcut === "function"
                onClicked: moreMenu.popup(moreButton, 0, moreButton.height + 6)

                Menu {
                    id: moreMenu

                    MenuItem {
                        visible: !!Chiaki.settings.psnAuthToken
                        height: visible ? implicitHeight : 0
                        text: consolePane.padConnected ? qsTr("Refresh PSN consoles  (R1)") : qsTr("Refresh PSN consoles")
                        onTriggered: Chiaki.refreshPsnToken()
                    }
                    MenuItem {
                        visible: typeof Chiaki.createSteamShortcut === "function"
                        height: visible ? implicitHeight : 0
                        text: consolePane.padConnected ? qsTr("Create Steam shortcut  (L3)") : qsTr("Create Steam shortcut")
                        onTriggered: root.showSteamShortcutDialog(false)
                    }
                }
            }

            RoundButton {
                id: settingsButton
                focusPolicy: Qt.NoFocus
                icon.source: "qrc:/icons/settings-20px.svg"
                icon.width: 26
                icon.height: 26
                flat: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Settings")
                onClicked: root.showSettingsDialog()
            }

            RoundButton {
                focusPolicy: Qt.NoFocus
                text: "×"
                font.pixelSize: 24
                flat: true
                opacity: 0.7
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Quit")
                onClicked: Qt.quit()
            }
        }

        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "transparent" }
                GradientStop { position: 0.25; color: Qt.rgba(140 / 255, 82 / 255, 1, 0.45) }
                GradientStop { position: 0.75; color: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.45) }
                GradientStop { position: 1.0; color: "transparent" }
            }
        }
    }

    // ------------------------------------------------------------ consoles

    Item {
        id: main
        anchors {
            top: header.bottom
            left: parent.left
            right: consolePane.wide ? side.left : parent.right
            bottom: footer.top
            topMargin: 18
            leftMargin: 28
            rightMargin: consolePane.wide ? 20 : 28
        }

        RowLayout {
            id: sectionHeader
            anchors {
                top: parent.top
                left: parent.left
                right: parent.right
            }
            spacing: 10

            Label {
                text: qsTr("Your consoles")
                font.pixelSize: 20
                font.bold: true
            }

            Rectangle {
                visible: consolePane.shownConsoles > 0
                implicitWidth: countLabel.implicitWidth + 14
                implicitHeight: 22
                radius: 11
                color: Qt.rgba(1, 1, 1, 0.08)
                Label {
                    id: countLabel
                    anchors.centerIn: parent
                    font.pixelSize: 12
                    font.bold: true
                    text: consolePane.shownConsoles
                }
            }

            Item { Layout.fillWidth: true }

            // Looking for consoles on this network: on or off
            Rectangle {
                id: discoveryPill
                implicitWidth: discoveryRow.implicitWidth + 24
                implicitHeight: 34
                radius: 17
                color: discoveryArea.containsMouse ? Qt.rgba(1, 1, 1, 0.12) : Qt.rgba(1, 1, 1, 0.06)
                border.color: Chiaki.discoveryEnabled ? Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.5) : Qt.rgba(1, 1, 1, 0.12)

                Row {
                    id: discoveryRow
                    anchors.centerIn: parent
                    spacing: 8

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: Chiaki.discoveryEnabled ? consolePane.green : Qt.rgba(1, 1, 1, 0.35)

                        SequentialAnimation on opacity {
                            running: Chiaki.discoveryEnabled
                            loops: Animation.Infinite
                            alwaysRunToEnd: true
                            NumberAnimation { to: 0.25; duration: 900 }
                            NumberAnimation { to: 1.0; duration: 900 }
                        }
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 13
                        font.bold: true
                        text: Chiaki.discoveryEnabled ? qsTr("Searching this network") : qsTr("Search is off")
                    }
                }

                MouseArea {
                    id: discoveryArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Chiaki.discoveryEnabled = !Chiaki.discoveryEnabled
                }
            }
        }

        ListView {
            id: hostsView
            keyNavigationWraps: true
            anchors {
                top: sectionHeader.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                topMargin: 12
            }
            spacing: 12
            clip: true
            model: Chiaki.hosts
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { }
            onCountChanged: {
                if(!hostsView.currentItem)
                    hostsView.incrementCurrentIndex();
                if(!hostsView.currentItem)
                    return;
                if(!hostsView.currentItem.visible)
                {
                    for(var i = 0; i < hostsView.count; i++)
                    {
                        hostsView.incrementCurrentIndex()
                        if(hostsView.currentItem.visible)
                        {
                            break;
                        }
                    }
                }
            }
            delegate: ItemDelegate {
                visible: modelData.display
                id: delegate
                width: hostsView.width - 12
                height: modelData.display ? 148 : 0
                highlighted: ListView.isCurrentItem
                padding: 0
                readonly property bool on: modelData.state === "ready"
                readonly property bool resting: modelData.state === "standby"
                readonly property bool psn: !!modelData.duid
                // A glass card; the selected one glows
                background: Rectangle {
                    radius: 20
                    color: delegate.hovered || delegate.highlighted ? Qt.rgba(36 / 255, 26 / 255, 74 / 255, 0.82) : Qt.rgba(16 / 255, 14 / 255, 32 / 255, 0.78)
                    border.width: delegate.highlighted ? 2 : 1
                    border.color: delegate.highlighted ? consolePane.accent : Qt.rgba(1, 1, 1, 0.08)
                    Behavior on color { ColorAnimation { duration: 150 } }
                }
                onClicked: connectToHost()

                function connectToHost() {
                    if(modelData.discovered)
                        Chiaki.connectToHost(index, modelData.name);
                    else
                        Chiaki.connectToHost(index);
                }

                function wakeUpHost() {
                    if(!modelData.discovered && !modelData.duid)
                        Chiaki.wakeUpHost(index);
                }

                function deleteHost() {
                    if (modelData.manual)
                        root.showConfirmDialog(qsTr("Delete Console"), qsTr("Are you sure you want to delete this console?"), () => {Chiaki.deleteHost(index)});
                    else if (modelData.discovered && !modelData.registered)
                        root.showConfirmDialog(qsTr("Hide Console"), qsTr("Are you sure you want to hide this console?") + "\n\n" + qsTr("Note: You can unhide from the Consoles section of the Settings under Hidden Consoles"), () => Chiaki.hideHost(modelData.mac, modelData.name));
                }

                function setConsolePin() {
                    root.showConsolePinDialog(index);
                }

                contentItem: RowLayout {
                    spacing: 22

                    // The console, on a soft glow that lights up when it is on
                    Item {
                        Layout.leftMargin: 20
                        Layout.preferredWidth: 150
                        Layout.fillHeight: true

                        Rectangle {
                            anchors.centerIn: parent
                            width: 118
                            height: 118
                            radius: 59
                            opacity: delegate.on ? 0.55 : 0.22
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: delegate.on ? consolePane.accent : Qt.rgba(1, 1, 1, 0.25) }
                                GradientStop { position: 1.0; color: "transparent" }
                            }
                        }

                        Image {
                            anchors.fill: parent
                            anchors.margins: 14
                            fillMode: Image.PreserveAspectFit
                            source: "image://svg/console-ps" + (modelData.ps5 ? "5" : "4") + (modelData.state == "standby" ? "#light_standby" : "#light_on")
                            sourceSize: Qt.size(width, height)
                        }
                    }

                    // Name, state at a glance, and the details underneath
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            font.pixelSize: 22
                            font.bold: true
                            text: modelData.name || (modelData.ps5 ? qsTr("PlayStation 5") : qsTr("PlayStation 4"))
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 6

                            // On, asleep, or reached through PSN
                            Chip {
                                visible: !delegate.psn
                                text: delegate.on ? qsTr("On") : (delegate.resting ? qsTr("Rest mode") : (modelData.state || qsTr("Unknown")))
                                dot: delegate.on ? consolePane.green : (delegate.resting ? consolePane.amber : "#9494a8")
                            }

                            Chip {
                                visible: delegate.psn
                                text: modelData.discovered ? qsTr("Automatic registration available") : qsTr("Through PSN")
                                dot: consolePane.cyan
                            }

                            Chip {
                                visible: !delegate.psn
                                text: modelData.registered ? qsTr("Registered") : qsTr("Not registered yet")
                                dot: modelData.registered ? consolePane.accent : consolePane.red
                            }

                            Chip {
                                visible: !delegate.psn && !!modelData.app
                                text: modelData.app || ""
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            opacity: 0.5
                            font.pixelSize: 13
                            text: {
                                let parts = [];
                                if (modelData.address)
                                    parts.push(Chiaki.settings.streamerMode ? qsTr("Address hidden") : modelData.address);
                                if (!delegate.psn)
                                    parts.push(modelData.discovered ? (modelData.manual ? qsTr("on this network, also added by hand") : qsTr("on this network")) : qsTr("added by hand"));
                                if (modelData.mac && !Chiaki.settings.streamerMode)
                                    parts.push(qsTr("ID %1").arg(modelData.mac));
                                if (modelData.titleId)
                                    parts.push(modelData.titleId);
                                return parts.join("  ·  ");
                            }
                        }
                    }

                    // What to do with it: one clear main action, the rest small
                    ColumnLayout {
                        Layout.rightMargin: 20
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 8

                        PageButton {
                            Layout.alignment: Qt.AlignRight
                            Layout.preferredWidth: 190
                            primary: true
                            hint: delegate.highlighted ? root.controllerButton("cross") : ""
                            text: {
                                if (delegate.psn)
                                    return modelData.discovered ? qsTr("Register") : qsTr("Play via PSN");
                                if (!modelData.registered)
                                    return qsTr("Register");
                                return delegate.resting ? qsTr("Wake up & play") : qsTr("Play");
                            }
                            onClicked: delegate.connectToHost()
                        }

                        Row {
                            Layout.alignment: Qt.AlignRight
                            spacing: 6

                            PageButton {
                                visible: modelData.registered && !delegate.psn && !modelData.discovered
                                font.pixelSize: 13
                                topPadding: 6
                                bottomPadding: 6
                                hint: delegate.highlighted ? root.controllerButton("pyramid") : ""
                                text: qsTr("Wake")
                                onClicked: delegate.wakeUpHost()
                            }

                            PageButton {
                                visible: modelData.registered
                                font.pixelSize: 13
                                topPadding: 6
                                bottomPadding: 6
                                hint: delegate.highlighted ? "qrc:/icons/l1.svg" : ""
                                text: qsTr("PIN")
                                onClicked: delegate.setConsolePin()
                            }

                            PageButton {
                                visible: modelData.manual || (modelData.discovered && !modelData.registered)
                                font.pixelSize: 13
                                topPadding: 6
                                bottomPadding: 6
                                hint: delegate.highlighted ? root.controllerButton("box") : ""
                                tint: hovered ? Qt.rgba(0.94, 0.27, 0.43, 0.45) : "transparent"
                                text: modelData.manual ? qsTr("Delete") : qsTr("Hide")
                                onClicked: delegate.deleteHost()
                            }
                        }
                    }
                }
            }
        }

        // Shown while there is no console in the list yet
        Card {
            anchors {
                top: sectionHeader.bottom
                left: parent.left
                right: parent.right
                topMargin: 12
            }
            height: emptyColumn.implicitHeight + 56
            visible: consolePane.shownConsoles === 0

            ColumnLayout {
                id: emptyColumn
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    margins: 28
                }
                spacing: 10

                RowLayout {
                    spacing: 12
                    BusyIndicator {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        running: Chiaki.discoveryEnabled
                        visible: Chiaki.discoveryEnabled
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 20
                        font.bold: true
                        text: Chiaki.discoveryEnabled ? qsTr("Looking for your PlayStation…") : qsTr("No consoles yet")
                    }
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                    font.pixelSize: 14
                    text: qsTr("Switch your PS5 or PS4 on (rest mode is fine) and make sure it is on the same network as this computer. It shows up here by itself. Somewhere else? Add it by its address.")
                }

                Row {
                    Layout.topMargin: 6
                    spacing: 10

                    PageButton {
                        primary: true
                        text: qsTr("Add a console by address")
                        hint: "qrc:/icons/r3.svg"
                        onClicked: root.showManualHostDialog()
                    }

                    PageButton {
                        visible: !Chiaki.discoveryEnabled
                        text: qsTr("Search this network")
                        onClicked: Chiaki.discoveryEnabled = true
                    }
                }

                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    wrapMode: Text.WordWrap
                    opacity: 0.55
                    font.pixelSize: 13
                    text: consolePane.wide ? qsTr("No console of your own? Join a friend's with the invite they sent you, on the right.")
                                           : qsTr("No console of your own? Join a friend's under Play Together.")
                }
            }
        }
    }

    // ------------------------------------------------------------ Play Together

    Flickable {
        id: side
        visible: consolePane.wide
        anchors {
            top: header.bottom
            right: parent.right
            bottom: footer.top
            topMargin: 18
            rightMargin: 28
        }
        width: 360
        contentHeight: sideColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: sideColumn
            width: side.width
            spacing: 14

            Card {
                Layout.fillWidth: true
                implicitHeight: joinColumn.implicitHeight + 40

                ColumnLayout {
                    id: joinColumn
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        margins: 20
                    }
                    spacing: 10

                    RowLayout {
                        spacing: 12

                        C.Avatar {
                            Layout.preferredWidth: 44
                            Layout.preferredHeight: 44
                            source: Chiaki.settings.coplayAvatarUrl
                            name: Chiaki.settings.coplayName || "?"
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0

                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.pixelSize: 17
                                font.bold: true
                                text: qsTr("Play Together")
                            }
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                opacity: 0.6
                                font.pixelSize: 13
                                text: Chiaki.settings.coplayName ? qsTr("You play as %1").arg(Chiaki.settings.coplayName)
                                                                 : qsTr("Set your name and picture")
                            }
                        }
                    }

                    Label {
                        Layout.topMargin: 6
                        font.pixelSize: 14
                        font.bold: true
                        text: qsTr("Join a friend")
                    }

                    TextField {
                        id: inviteField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Paste the invite: psrpt://…")
                        text: Chiaki.settings.coplayLastInvite
                        selectByMouse: true
                        font.pixelSize: 14
                        onAccepted: if (joinButton.enabled) joinButton.clicked()
                        Keys.onEscapePressed: consolePane.forceActiveFocus()
                    }

                    PageButton {
                        id: joinButton
                        Layout.fillWidth: true
                        primary: true
                        enabled: !Chiaki.coplayJoining && inviteField.text.trim().length > 0
                        text: Chiaki.coplayJoining ? qsTr("Connecting…") : qsTr("Join")
                        onClicked: {
                            joinError.text = "";
                            Chiaki.joinCoPlay(inviteField.text, Chiaki.settings.coplayName, Chiaki.settings.coplayControllerOnly);
                        }
                    }

                    Label {
                        id: joinError
                        Layout.fillWidth: true
                        visible: text.length > 0
                        wrapMode: Text.WordWrap
                        color: consolePane.red
                        font.pixelSize: 13
                    }

                    Connections {
                        target: Chiaki
                        function onCoplayJoinFinished(ok, message) {
                            if (!ok)
                                joinError.text = message;
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        Layout.preferredHeight: 1
                        color: Qt.rgba(1, 1, 1, 0.08)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                font.pixelSize: 14
                                font.bold: true
                                text: qsTr("Share your console")
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                opacity: 0.6
                                font.pixelSize: 12
                                text: Chiaki.settings.coplayHostEnabled
                                      ? qsTr("Friends with your invite can join while you play.")
                                      : qsTr("Off: your sessions stay yours alone.")
                            }
                        }

                        Switch {
                            focusPolicy: Qt.NoFocus
                            checked: Chiaki.settings.coplayHostEnabled
                            onToggled: Chiaki.settings.coplayHostEnabled = checked
                        }
                    }

                    PageButton {
                        Layout.fillWidth: true
                        visible: Chiaki.settings.coplayHostEnabled
                        text: copiedTimer.running ? qsTr("Invite copied") : qsTr("Copy my invite")
                        onClicked: {
                            Chiaki.settings.coplayCopyInvite();
                            copiedTimer.restart();
                        }
                        Timer {
                            id: copiedTimer
                            interval: 1600
                        }
                    }

                    PageButton {
                        Layout.fillWidth: true
                        text: qsTr("Profile, voice, webcam & overlays…")
                        font.pixelSize: 13
                        onClicked: root.showPlayTogetherDialog()
                    }
                }
            }

            Card {
                Layout.fillWidth: true
                implicitHeight: tipsColumn.implicitHeight + 36

                ColumnLayout {
                    id: tipsColumn
                    anchors {
                        left: parent.left
                        right: parent.right
                        top: parent.top
                        margins: 18
                    }
                    spacing: 6

                    Label {
                        font.pixelSize: 14
                        font.bold: true
                        text: qsTr("While playing")
                    }
                    Repeater {
                        model: [
                            qsTr("Menu: %1").arg(consolePane.padConnected ? Chiaki.settings.stringForStreamMenuShortcut() : (Qt.platform.os === "osx" ? "⌘O" : "Ctrl+O")),
                            qsTr("Chat: %1").arg(Qt.platform.os === "osx" ? "⌘T" : "Ctrl+T"),
                            qsTr("Full screen: F11 or double-click")
                        ]
                        delegate: Label {
                            required property string modelData
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            opacity: 0.65
                            font.pixelSize: 13
                            text: "•  " + modelData
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ footer

    Item {
        id: footer
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        height: 52

        Label {
            anchors {
                left: parent.left
                leftMargin: 28
                verticalCenter: parent.verticalCenter
            }
            textFormat: Text.StyledText
            linkColor: consolePane.cyan
            opacity: 0.8
            text: qsTr("powered by %1").arg("<a href=\"https://gamernova.net/\">GamerNova.net</a>")
            onLinkActivated: (link) => Qt.openUrlExternally(link)

            HoverHandler {
                cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
            }
        }

        Label {
            anchors {
                right: parent.right
                rightMargin: 28
                verticalCenter: parent.verticalCenter
            }
            opacity: 0.45
            text: qsTr("Version %1").arg(Qt.application.version)
        }
    }
}
