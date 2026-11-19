import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material

import org.streetpea.chiaking

import "controls" as C

Pane {
    padding: 0
    id: consolePane
    background: C.NovaBackground { }
    StackView.onActivated: {
        forceActiveFocus(Qt.TabFocusReason);
        if(!Chiaki.autoConnect && !root.initialAsk && !Chiaki.window.directStream)
        {
            root.initialAsk = true;
            if(Chiaki.settings.addSteamShortcutAsk && (typeof Chiaki.createSteamShortcut === "function"))
                root.showRemindDialog(qsTr("Official Steam artwork + controller layout"), qsTr("Would you like to either create a new non-Steam game for PSRemotePlayTogether\nor update an existing non-Steam game with the official artwork and controller layout?") + "\n\n" + qsTr("(Note: If you select no now and want to do this later, click the button or press R3 from the main menu.)"), false, () => root.showSteamShortcutDialog(true));
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

    // A small labelled pill: the state of a console at a glance
    component Chip: Rectangle {
        id: chip
        property string text
        property color dot: "transparent"
        height: 26
        width: chipContent.implicitWidth + 20
        radius: 13
        color: Qt.rgba(1, 1, 1, 0.08)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.10)

        Row {
            id: chipContent
            anchors.centerIn: parent
            spacing: 6

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                visible: chip.dot.a > 0
                width: 8
                height: 8
                radius: 4
                color: chip.dot
            }

            Label {
                font.pixelSize: 13
                font.bold: true
                text: chip.text
            }
        }
    }

    ToolBar {
        id: toolBar
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 80
        background: C.GlassBar { }

        RowLayout {
            id: toolRow
            anchors {
                fill: parent
                leftMargin: 10
                rightMargin: 10
            }

            // What the buttons need at their full size. The name only shows when there is
            // room left for it, and in a narrow window the buttons shrink instead of
            // pushing the last one out of the window.
            readonly property real buttonsWidth: {
                let w = 0;
                for (let i = 0; i < children.length; i++) {
                    const c = children[i];
                    if (c === wordmark || c === toolSpacer || !c.visible)
                        continue;
                    w += (c.Layout.maximumWidth < 10000 ? c.Layout.maximumWidth : c.Layout.preferredWidth)
                        + c.Layout.leftMargin + c.Layout.rightMargin + spacing;
                }
                return w;
            }

            Button {
                Layout.fillHeight: true
                Layout.preferredWidth: 80
                flat: true
                text: "×"
                font.pixelSize: 44
                focusPolicy: Qt.NoFocus
                onClicked: Qt.quit()
                Material.roundedScale: Material.SmallScale
            }

            C.GradientText {
                id: wordmark
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: 6
                // Makes room for the buttons in a narrow window
                visible: toolRow.width >= toolRow.buttonsWidth + implicitWidth + 24
                text: "PSRemotePlayTogether"
                font.pixelSize: 26
                font.bold: true
            }

            Item {
                id: toolSpacer
                Layout.fillWidth: true
            }

            Button {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.minimumWidth: 110
                Layout.preferredWidth: 260
                Layout.maximumWidth: 260
                flat: true
                font.pixelSize: 17
                // Keeps the text clear of the controller button drawn at the right edge
                rightPadding: 46
                text: "Create Steam Shortcut"
                focusPolicy: Qt.NoFocus
                onClicked: root.showSteamShortcutDialog(false)
                Material.roundedScale: Material.SmallScale
                visible: typeof Chiaki.createSteamShortcut === "function"
                Image {
                    anchors {
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: 12
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l3.svg"
                }
            }

            Button {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.minimumWidth: 110
                Layout.preferredWidth: 220
                Layout.maximumWidth: 220
                flat: true
                font.pixelSize: 17
                text: "Refresh PSN Hosts"
                icon.source: "qrc:/icons/r1.svg"
                focusPolicy: Qt.NoFocus
                onClicked: Chiaki.refreshPsnToken();
                Material.roundedScale: Material.SmallScale
                visible: Chiaki.settings.psnAuthToken
            }

            Button {
                Layout.fillHeight: true
                Layout.preferredWidth: 200
                Layout.topMargin: 14
                Layout.bottomMargin: 14
                Layout.leftMargin: 8
                Layout.rightMargin: 8
                text: qsTr("Play Together")
                font.pixelSize: 17
                font.bold: true
                focusPolicy: Qt.NoFocus
                onClicked: root.showPlayTogetherDialog()
                Material.roundedScale: Material.FullScale
                highlighted: true
            }

            Button {
                Layout.fillHeight: true
                Layout.fillWidth: true
                Layout.minimumWidth: 110
                Layout.preferredWidth: 220
                Layout.maximumWidth: 220
                flat: true
                font.pixelSize: 17
                // Keeps the text clear of the controller button drawn at the left edge
                leftPadding: 46
                text: "Add Manual Host"
                focusPolicy: Qt.NoFocus
                onClicked: root.showManualHostDialog()
                Material.roundedScale: Material.SmallScale
                Image {
                    anchors {
                        left: parent.left
                        verticalCenter: parent.verticalCenter
                        leftMargin: 12
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r3.svg"
                }
            }

            Button {
                id: settingsButton
                Layout.fillHeight: true
                Layout.preferredWidth: 80
                flat: true
                icon.source: "qrc:/icons/settings-20px.svg";
                icon.width: 50
                icon.height: 50
                focusPolicy: Qt.NoFocus
                onClicked: root.showSettingsDialog()
                Material.roundedScale: Material.SmallScale
            }
        }
    }

    ListView {
        id: hostsView
        keyNavigationWraps: true
        anchors {
            top: toolBar.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
            bottomMargin: 50
        }
        clip: true
        model: Chiaki.hosts
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
            width: parent ? parent.width - 40 : 0
            x: 20
            height: modelData.display ? 180 : 0
            highlighted: ListView.isCurrentItem
            // A glass card; the selected one glows
            background: Rectangle {
                anchors.fill: parent
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                radius: 18
                color: delegate.hovered || delegate.highlighted ? Qt.rgba(40 / 255, 30 / 255, 80 / 255, 0.72) : Qt.rgba(16 / 255, 16 / 255, 24 / 255, 0.72)
                border.width: delegate.highlighted ? 2 : 1
                border.color: delegate.highlighted ? "#8c52ff" : Qt.rgba(1, 1, 1, 0.09)
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

            RowLayout {
                anchors {
                    fill: parent
                    leftMargin: 30
                    rightMargin: 10
                    topMargin: 10
                    bottomMargin: 10
                }
                spacing: 50

                Image {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 150
                    fillMode: Image.PreserveAspectFit
                    source: "image://svg/console-ps" + (modelData.ps5 ? "5" : "4") + (modelData.state == "standby" ? "#light_standby" : "#light_on")
                    sourceSize: Qt.size(width, height)
                }

                // Name, state at a glance, and the details underneath
                ColumnLayout {
                    Layout.alignment: Qt.AlignLeft | Qt.AlignVCenter
                    spacing: 8

                    Label {
                        font.pixelSize: 24
                        font.bold: true
                        text: modelData.name || (modelData.ps5 ? qsTr("PlayStation 5") : qsTr("PlayStation 4"))
                    }

                    Row {
                        spacing: 8

                        // On, asleep, or reached through PSN
                        Chip {
                            visible: !modelData.duid
                            text: modelData.state === "ready" ? qsTr("On") : (modelData.state === "standby" ? qsTr("In rest mode") : modelData.state)
                            dot: modelData.state === "ready" ? "#34d399" : (modelData.state === "standby" ? "#f5b942" : "#9494a8")
                        }

                        Chip {
                            visible: !!modelData.duid
                            text: modelData.discovered ? qsTr("Automatic registration available") : qsTr("Remote connection via PSN")
                            dot: "#00f2ff"
                        }

                        Chip {
                            visible: !modelData.duid
                            text: modelData.registered ? qsTr("Registered") : qsTr("Not registered yet")
                            dot: modelData.registered ? "#8c52ff" : "#f0466e"
                        }

                        Chip {
                            visible: !modelData.duid && !!modelData.app
                            text: modelData.app || ""
                        }
                    }

                    Label {
                        opacity: 0.6
                        font.pixelSize: 14
                        text: {
                            let parts = [];
                            if (modelData.address)
                                parts.push(Chiaki.settings.streamerMode ? qsTr("Address hidden") : modelData.address);
                            if (modelData.mac && !Chiaki.settings.streamerMode)
                                parts.push(qsTr("ID %1").arg(modelData.mac));
                            if (!modelData.duid)
                                parts.push(modelData.discovered ? (modelData.manual ? qsTr("found on this network, also added by hand") : qsTr("found on this network")) : qsTr("added by hand"));
                            if (modelData.titleId)
                                parts.push(modelData.titleId);
                            return parts.join("  ·  ");
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                ColumnLayout {
                    Layout.fillHeight: true
                    spacing: 0

                    Button {
                        Layout.alignment: Qt.AlignCenter
                        text: modelData.manual ? qsTr("Delete") : qsTr("Hide")
                        flat: true
                        padding: 20
                        leftPadding: delegate.highlighted ? 50 : undefined
                        focusPolicy: Qt.NoFocus
                        visible: modelData.manual || (modelData.discovered && !modelData.registered)
                        onClicked: delegate.deleteHost()
                        Material.roundedScale: Material.SmallScale

                        Image {
                            anchors {
                                left: parent.left
                                verticalCenter: parent.verticalCenter
                                leftMargin: 12
                            }
                            width: 28
                            height: 28
                            sourceSize: Qt.size(width, height)
                            source: root.controllerButton("box")
                            visible: delegate.highlighted
                        }
                    }

                    Button {
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Wake Up")
                        flat: true
                        padding: 20
                        leftPadding: delegate.highlighted ? 50 : undefined
                        visible: modelData.registered && !modelData.duid && !modelData.discovered
                        focusPolicy: Qt.NoFocus
                        onClicked: delegate.wakeUpHost()
                        Material.roundedScale: Material.SmallScale

                        Image {
                            anchors {
                                left: parent.left
                                verticalCenter: parent.verticalCenter
                                leftMargin: 12
                            }
                            width: 28
                            height: 28
                            sourceSize: Qt.size(width, height)
                            source: root.controllerButton("pyramid")
                            visible: delegate.highlighted
                        }
                    }

                    Button {
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Update Console Pin")
                        flat: true
                        padding: 20
                        leftPadding: delegate.highlighted ? 50 : undefined
                        visible: modelData.registered
                        focusPolicy: Qt.NoFocus
                        onClicked: delegate.setConsolePin()
                        Material.roundedScale: Material.SmallScale

                        Image {
                            anchors {
                                left: parent.left
                                verticalCenter: parent.verticalCenter
                                leftMargin: 12
                            }
                            width: 28
                            height: 28
                            sourceSize: Qt.size(width, height)
                            source: "qrc:/icons/l1.svg"
                            visible: delegate.highlighted
                        }
                    }
                } 
            } 
        }
    }     

    RoundButton {
        anchors {
            left: parent.left
            bottom: parent.bottom
            margins: 20
        }
        icon.source: "qrc:/icons/discover-" + (checked ? "" : "off-") + "24px.svg"
        icon.width: 50
        icon.height: 50
        padding: 20
        focusPolicy: Qt.NoFocus
        checkable: true
        checked: Chiaki.discoveryEnabled
        onToggled: Chiaki.discoveryEnabled = !Chiaki.discoveryEnabled
        Material.background: Material.accent
    }

    Label {
        anchors {
            right: parent.right
            bottom: parent.bottom
            margins: 20
        }
        opacity: 0.6
        text: Qt.application.version
    }

    // Shown while there is no console in the list yet
    ColumnLayout {
        id: hero
        anchors.centerIn: parent
        spacing: 10
        visible: {
            for (let i = 0; i < Chiaki.hosts.length; ++i) {
                if (Chiaki.hosts[i].display)
                    return false;
            }
            return true;
        }

        C.GradientText {
            Layout.alignment: Qt.AlignHCenter
            text: "PSRemotePlayTogether"
            font.pixelSize: 54
            font.bold: true
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            opacity: 0.75
            font.pixelSize: 22
            text: qsTr("One console. One controller. Everyone plays.")
        }

        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 16
            opacity: 0.55
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Looking for your PlayStation on this network…\nA friend sent you an invite? Open Play Together.")
        }
    }

    Label {
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            margins: 22
        }
        textFormat: Text.StyledText
        linkColor: "#00f2ff"
        opacity: 0.85
        text: qsTr("powered by %1").arg("<a href=\"https://gamernova.net/\">GamerNova.net</a>")
        onLinkActivated: (link) => Qt.openUrlExternally(link)

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }
}
