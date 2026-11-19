import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs

import org.streetpea.chiaking

import "controls" as C

DialogView {
    enum Console {
        PS4,
        PS5
    }
    property int selectedConsole: SettingsDialog.Console.PS5
    property bool quitControllerMapping: true
    id: dialog
    title: qsTr("Settings")
    header: qsTr("Changes are saved at once")
    buttonVisible: false
    function flickContainsItem(flick, item) {
        let current = item;
        while (current) {
            if (current === flick || current === flick.contentItem)
                return true;
            current = current.parent;
        }
        return false;
    }
    function flickVisibilityTarget(flick, item) {
        // A setting's whole row, so that its name shows with the control
        for (let row = item; row && row !== flick && row !== flick.contentItem; row = row.parent) {
            if (row.isSettingRow === true)
                return row;
        }
        let current = item;
        while (current && current !== flick && current !== flick.contentItem) {
            if (current.parent === flick.contentItem)
                return current;
            if (current.parent && current.parent.parent === flick.contentItem)
                return current;
            current = current.parent;
        }
        return item;
    }
    function ensureItemVisibleInFlick(flick, item) {
        if (!flick || !item || !flickContainsItem(flick, item))
            return;
        const target = flickVisibilityTarget(flick, item);
        const top = target.mapToItem(flick.contentItem, 0, 0).y;
        const bottom = top + target.height;
        if (top < flick.contentY)
            flick.contentY = top;
        else if (bottom > flick.contentY + flick.height)
            flick.contentY = bottom - flick.height;
    }
    function itemIsInsideNestedScrollable(flick, item) {
        let current = item ? item.parent : null;
        while (current && current !== flick && current !== flick.contentItem) {
            if (current.contentY !== undefined && current.contentHeight !== undefined && current !== flick)
                return true;
            current = current.parent;
        }
        return false;
    }
    function nestedScrollableTarget(flick, item) {
        let current = item;
        while (current && current !== flick && current !== flick.contentItem) {
            if (current.contentY !== undefined && current.contentHeight !== undefined && current !== flick)
                return current;
            current = current.parent;
        }
        return item;
    }
    function ensureActiveFocusVisible() {
        const flick = activeSettingsFlick();
        const window = dialog.Window.window;
        if (!flick || !window || !window.activeFocusItem)
            return;
        const target = itemIsInsideNestedScrollable(flick, window.activeFocusItem)
            ? nestedScrollableTarget(flick, window.activeFocusItem)
            : window.activeFocusItem;
        ensureItemVisibleInFlick(flick, target);
    }
    function scrollFlickKeepingItemVisible(flick, item, delta) {
        if (!flick || !item)
            return;
        const maxContentY = Math.max(0, flick.contentHeight - flick.height);
        if (maxContentY <= 0)
            return;
        flick.contentY = Math.max(0, Math.min(maxContentY, flick.contentY + delta));
    }
    function focusCurrentTabFirstItem() {
        let item = null;
        switch (bar.currentIndex) {
        case 0: item = disconnectAction; break;
        case 1: item = hwDecoderCombo; break;
        case 2: item = consoleSelection; break;
        case 3: item = audioOutDevice; break;
        case 4: item = registerNewButton; break;
        case 5: item = resetAllKeys; break;
        case 6: item = controllerMappingChange; break;
        case 7: item = firstRemoteFocusableItem(); break;
        case 8: item = profile; break;
        }
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason);
    }
    function activeSettingsFlick() {
        switch (bar.currentIndex) {
        case 0: return generalFlick;
        case 1: return videoFlick;
        case 2: return streamFlick;
        case 3: return audiowifiFlick;
        case 4: return consolesFlick;
        case 5: return keysFlick;
        case 6: return controllersFlick;
        case 7: return remoteFlick;
        case 8: return configFlick;
        default: return null;
        }
    }

    function firstRemoteFocusableItem() {
        if (openPsnLogin.visible)
            return openPsnLogin;
        if (resetPsnTokens.visible)
            return resetPsnTokens;
        if (holePunchGuessingCheckbox.visible)
            return holePunchGuessingCheckbox;
        if (portGuessCountSlider.visible)
            return portGuessCountSlider;
        if (portGuessSocketSlider.visible)
            return portGuessSocketSlider;
        return remoteFlick;
    }
    Keys.onPressed: (event) => {
        if (event.modifiers)
            return;
        switch (event.key) {
        case Qt.Key_PageUp:
            bar.decrementCurrentIndex();
            event.accepted = true;
            break;
        case Qt.Key_PageDown:
            bar.incrementCurrentIndex();
            event.accepted = true;
            break;
        case Qt.Key_Up:
        {
            const flick = activeSettingsFlick();
            if (!flick || flick.contentHeight <= flick.height || flick.contentY <= 0.001)
                return;
            flick.flick(0, 500);
            event.accepted = true;
            break;
        }
        case Qt.Key_Down:
        {
            const flick = activeSettingsFlick();
            if (!flick || flick.contentHeight <= flick.height || flick.contentY >= flick.contentHeight - flick.height - 0.001)
                return;
            flick.flick(0, -500);
            event.accepted = true;
            break;
        }
        }
    }

    Item {
        // The sections, down the left side; L1 / R1 on a controller go up and down
        Rectangle {
            id: sidebar
            anchors {
                top: parent.top
                left: parent.left
                bottom: parent.bottom
                margins: 14
                topMargin: 10
            }
            width: 210
            radius: 18
            color: Qt.rgba(16 / 255, 14 / 255, 32 / 255, 0.72)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.07)

            Column {
                anchors {
                    fill: parent
                    margins: 10
                }
                spacing: 4

                Repeater {
                    model: [
                        { label: qsTr("General"), detail: qsTr("Disconnect, sleep, menu") },
                        { label: qsTr("Video"), detail: qsTr("Decoder, renderer, window") },
                        { label: qsTr("Stream"), detail: qsTr("Resolution, frame rate") },
                        { label: qsTr("Audio & Wi-Fi"), detail: qsTr("Devices, buffers") },
                        { label: qsTr("Consoles"), detail: qsTr("Registered and hidden") },
                        { label: qsTr("Keyboard"), detail: qsTr("Keys for the controller") },
                        { label: qsTr("Controllers"), detail: qsTr("Mapping, haptics, motion") },
                        { label: qsTr("Remote"), detail: qsTr("PSN, outside your home") },
                        { label: qsTr("Profiles"), detail: qsTr("Saved setting sets") }
                    ]
                    delegate: Rectangle {
                        id: navItem
                        required property var modelData
                        required property int index
                        readonly property bool current: bar.currentIndex === index
                        width: parent.width
                        height: 52
                        radius: 12
                        color: current ? Qt.rgba(140 / 255, 82 / 255, 1, 0.28)
                             : navArea.containsMouse ? Qt.rgba(1, 1, 1, 0.06) : "transparent"
                        border.width: current ? 1 : 0
                        border.color: Qt.rgba(140 / 255, 82 / 255, 1, 0.7)

                        Rectangle {
                            visible: navItem.current
                            anchors {
                                left: parent.left
                                verticalCenter: parent.verticalCenter
                                leftMargin: 4
                            }
                            width: 3
                            height: 24
                            radius: 1.5
                            color: "#00f2ff"
                        }

                        Column {
                            anchors {
                                left: parent.left
                                right: parent.right
                                verticalCenter: parent.verticalCenter
                                leftMargin: 16
                                rightMargin: 8
                            }
                            spacing: 1

                            Label {
                                width: parent.width
                                elide: Text.ElideRight
                                font.pixelSize: 15
                                font.bold: true
                                text: navItem.modelData.label
                            }
                            Label {
                                width: parent.width
                                elide: Text.ElideRight
                                font.pixelSize: 11
                                opacity: 0.5
                                text: navItem.modelData.detail
                            }
                        }

                        MouseArea {
                            id: navArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: bar.currentIndex = navItem.index
                        }
                    }
                }

                Item { width: 1; height: 8 }

                Row {
                    visible: Chiaki.controllers.length > 0
                    spacing: 6
                    leftPadding: 8
                    Image {
                        width: 24
                        height: 24
                        sourceSize: Qt.size(width, height)
                        source: "qrc:/icons/l1.svg"
                    }
                    Image {
                        width: 24
                        height: 24
                        sourceSize: Qt.size(width, height)
                        source: "qrc:/icons/r1.svg"
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        opacity: 0.55
                        font.pixelSize: 12
                        text: qsTr("change section")
                    }
                }
            }
        }

        TabBar {
            id: bar
            // Keeps the current page; the sidebar above is what is shown
            visible: false
            height: 0
            anchors {
                top: parent.top
                left: sidebar.right
                right: parent.right
            }
            TabButton {
                id: general
                text: qsTr("General")
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        left: general.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 1
                }
            }

            TabButton {
                id: video
                text: qsTr("Video")
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: video.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 0
                }
                Image {
                    anchors {
                        left: video.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 2
                }
            }

            TabButton {
                id: stream
                text: qsTr("Stream")
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: stream.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 1
                }
                Image {
                    anchors {
                        left: stream.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 3
                }
            }

            TabButton {
                text: qsTr("Audio/Wifi")
                id: audio
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: audio.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 2
                }
                Image {
                    anchors {
                        left: audio.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 4
                }
            }

            TabButton {
                text: qsTr("Consoles")
                id: consoles
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: consoles.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 3
                }
                Image {
                    anchors {
                        left: consoles.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 5
                }
            }

            TabButton {
                text: qsTr("Keys")
                id: keys
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: keys.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 4
                }
                Image {
                    anchors {
                        left: keys.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 6
                }
            }

            TabButton {
                text: qsTr("Controllers")
                id: controllers
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: controllers.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 5
                }
                Image {
                    anchors {
                        left: controllers.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 7
                }
            }

            TabButton {
                text: qsTr("Remote")
                id: remote
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        right: remote.left
                        verticalCenter: parent.verticalCenter
                        rightMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 6
                }
                Image {
                    anchors {
                        left: remote.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/l1.svg"
                    visible: bar.currentIndex == 8
                }
            }

            TabButton {
                text: qsTr("Config")
                id: config
                focusPolicy: Qt.NoFocus
                Image {
                    anchors {
                        left: config.left
                        verticalCenter: parent.verticalCenter
                        leftMargin: -15
                    }
                    width: 28
                    height: 28
                    sourceSize: Qt.size(width, height)
                    source: "qrc:/icons/r1.svg"
                    visible: bar.currentIndex == 7
                }
            }
        }

        // The page of the selected section sits on a glass panel
        Rectangle {
            anchors {
                top: parent.top
                left: sidebar.right
                right: parent.right
                bottom: parent.bottom
                margins: 14
                topMargin: 10
            }
            radius: 18
            color: Qt.rgba(16 / 255, 16 / 255, 24 / 255, 0.5)
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.07)
        }

        StackLayout {
            anchors {
                top: parent.top
                left: sidebar.right
                right: parent.right
                bottom: parent.bottom
                topMargin: 10
                leftMargin: 14
                rightMargin: 14
            }
            currentIndex: bar.currentIndex
            onCurrentIndexChanged: {
                dialog.focusCurrentTabFirstItem()
                Qt.callLater(dialog.ensureActiveFocusVisible)
            }

            Item {
                // General
                Flickable {
                    id: generalFlick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: generalLayout.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: generalFlick.contentHeight > generalFlick.height
                    }
                    ColumnLayout {
                        id: generalLayout
                        x: Math.max(28, Math.round((generalFlick.width - width) / 2))
                        y: 24
                        width: Math.min(generalFlick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("When a session ends")

                            C.SettingRow {
                                title: qsTr("Closing the stream")
                                description: qsTr("What happens to the console when you leave.")
                                hint: qsTr("(Ask)")

                                C.ComboBox {
                                    id: disconnectAction
                                    Layout.fillWidth: true
                                    firstInFocusChain: true
                                    model: [qsTr("Do Nothing"), qsTr("Enter Sleep Mode"), qsTr("Ask")]
                                    currentIndex: Chiaki.settings.disconnectAction
                                    onActivated: index => Chiaki.settings.disconnectAction = index
                                }
                            }

                            C.SettingRow {
                                title: qsTr("When this computer sleeps")
                                description: qsTr("What happens to the console when this computer goes to sleep.")
                                hint: qsTr("(Do Nothing)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Do Nothing"), qsTr("Enter Sleep Mode")]
                                    currentIndex: Chiaki.settings.suspendAction
                                    onActivated: index => Chiaki.settings.suspendAction = index
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Reconnect by itself")
                                description: qsTr("Get a lost connection back without asking: to the console, or as a guest to the host.")
                                hint: qsTr("(On)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.coplayAutoReconnect
                                    onToggled: Chiaki.settings.coplayAutoReconnect = checked
                                }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("While streaming")

                            C.SettingRow {
                                title: qsTr("Picture and sound")
                                description: qsTr("Switch off what you do not need on this computer, for example to play on the TV and only send your controller. Friends who join your session still get both.")
                                hint: qsTr("(Audio and Video Enabled)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Audio and Video Enabled"), qsTr("Audio Disabled"), qsTr("Video Disabled"), qsTr("Audio and Video Disabled")]
                                    currentIndex: Chiaki.settings.audioVideoDisabled
                                    onActivated: index => Chiaki.settings.audioVideoDisabled = index
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Streamer mode")
                                description: qsTr("Hides addresses and console IDs, for when your screen is on a stream.")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    id: streamerMode
                                    checked: Chiaki.settings.streamerMode
                                    onToggled: Chiaki.settings.streamerMode = !Chiaki.settings.streamerMode
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Stream menu on the controller")
                                description: qsTr("Open the stream menu with a button combination. On the keyboard it is always Ctrl+O (⌘O).")
                                hint: qsTr("(On)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    id: streamMenu
                                    checked: Chiaki.settings.streamMenuEnabled
                                    onToggled: Chiaki.settings.streamMenuEnabled = !Chiaki.settings.streamMenuEnabled
                                    KeyNavigation.priority: KeyNavigation.BeforeItem
                                    KeyNavigation.up: streamerMode
                                    KeyNavigation.left: streamMenu
                                    KeyNavigation.right: streamMenu
                                    KeyNavigation.down: {
                                        if(streamMenuShortcut1.visible)
                                            streamMenuShortcut1
                                        else
                                            streamMenu
                                    }
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.streamMenuEnabled
                                title: qsTr("Button combination")
                                description: qsTr("Hold these together to open the stream menu.")
                                hint: qsTr("(L1+R1+L3+R3)")
                                controlWidth: 520

                                C.ComboBox {
                                    id: streamMenuShortcut1
                                    font.pixelSize: 14
                                    Layout.fillWidth: true
                                    firstInFocusChain: false
                                    model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                    currentIndex: Chiaki.settings.streamMenuShortcut1
                                    onActivated: index => Chiaki.settings.streamMenuShortcut1 = index
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                    KeyNavigation.up: streamMenu
                                    KeyNavigation.down: streamMenuShortcut1
                                    KeyNavigation.left: streamMenuShortcut1
                                    KeyNavigation.right: streamMenuShortcut2
                                }

                                C.ComboBox {
                                    id: streamMenuShortcut2
                                    font.pixelSize: 14
                                    Layout.fillWidth: true
                                    firstInFocusChain: false
                                    model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                    currentIndex: Chiaki.settings.streamMenuShortcut2
                                    onActivated: index => Chiaki.settings.streamMenuShortcut2 = index
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                    KeyNavigation.up: streamMenu
                                    KeyNavigation.down: streamMenuShortcut2
                                    KeyNavigation.left: streamMenuShortcut1
                                    KeyNavigation.right: streamMenuShortcut3
                                }

                                C.ComboBox {
                                    id: streamMenuShortcut3
                                    font.pixelSize: 14
                                    Layout.fillWidth: true
                                    firstInFocusChain: false
                                    model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                    currentIndex: Chiaki.settings.streamMenuShortcut3
                                    onActivated: index => Chiaki.settings.streamMenuShortcut3 = index
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                    KeyNavigation.up: streamMenu
                                    KeyNavigation.down: streamMenuShortcut3
                                    KeyNavigation.left: streamMenuShortcut2
                                    KeyNavigation.right: streamMenuShortcut4
                                }

                                C.ComboBox {
                                    id: streamMenuShortcut4
                                    font.pixelSize: 14
                                    Layout.fillWidth: true
                                    firstInFocusChain: false
                                    model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                    currentIndex: Chiaki.settings.streamMenuShortcut4
                                    onActivated: index => Chiaki.settings.streamMenuShortcut4 = index
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                    KeyNavigation.up: streamMenu
                                    KeyNavigation.down: streamMenuShortcut4
                                    KeyNavigation.left: streamMenuShortcut3
                                    KeyNavigation.right: streamMenuShortcut4
                                }
                            }
                        }

                        C.SettingsSection {
                            visible: typeof Chiaki.settings.steamDeckHaptics !== "undefined" || typeof Chiaki.settings.verticalDeck !== "undefined"
                            title: qsTr("Steam Deck")

                            C.SettingRow {
                                visible: typeof Chiaki.settings.steamDeckHaptics !== "undefined"
                                title: qsTr("True haptics")
                                description: qsTr("Better quality, but noisier.")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: {
                                        if(typeof Chiaki.settings.steamDeckHaptics !== "undefined")
                                            Chiaki.settings.steamDeckHaptics
                                        else
                                            false
                                    }
                                    onToggled: Chiaki.settings.steamDeckHaptics = checked
                                }
                            }

                            C.SettingRow {
                                visible: typeof Chiaki.settings.verticalDeck !== "undefined"
                                title: qsTr("Vertical orientation")
                                description: qsTr("Hold the Steam Deck upright (for motion controls).")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: {
                                        if(typeof Chiaki.settings.verticalDeck !== "undefined")
                                            Chiaki.settings.verticalDeck
                                        else
                                            false
                                    }
                                    onToggled: Chiaki.settings.verticalDeck = checked
                                }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("Logs")

                            C.SettingRow {
                                title: qsTr("Log folder")
                                description: Chiaki.settings.logDirectory

                                Item { Layout.fillWidth: true }
                                C.Button {
                                    id: openButton
                                    text: qsTr("Open folder")
                                    onClicked: Qt.openUrlExternally("file://" + Chiaki.settings.logDirectory);
                                    Material.roundedScale: Material.SmallScale
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Video
                Flickable {
                    id: videoFlick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: videoGrid.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: videoFlick.contentHeight > videoFlick.height
                    }
                    ColumnLayout {
                        id: videoGrid
                        x: Math.max(28, Math.round((videoFlick.width - width) / 2))
                        y: 24
                        width: Math.min(videoFlick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Decoding")

                            C.SettingRow {
                                title: qsTr("Hardware decoder")
                                description: qsTr("Zero-copy hands the decoded picture straight to the screen.")
                                hint: qsTr("(Auto)")

                                C.ComboBox {
                                    id: hwDecoderCombo
                                    Layout.fillWidth: true
                                    model: Chiaki.settings.availableDecoders
                                    currentIndex: Math.max(0, model.indexOf(Chiaki.settings.decoder))
                                    KeyNavigation.priority: KeyNavigation.BeforeItem
                                    KeyNavigation.up: hwDecoderCombo
                                    KeyNavigation.right: zeroCopyCheck
                                    KeyNavigation.down: windowTypeCombo
                                    onActivated: (index) => Chiaki.settings.decoder = index ? model[index] : ""
                                }

                                Label {
                                    text: qsTr("Zero-copy")
                                }

                                C.CheckBox {
                                    id: zeroCopyCheck
                                    KeyNavigation.priority: KeyNavigation.BeforeItem
                                    KeyNavigation.up: zeroCopyCheck
                                    KeyNavigation.left: hwDecoderCombo
                                    KeyNavigation.down: windowTypeCombo
                                    checked: Chiaki.settings.useZeroCopy
                                    onToggled: Chiaki.settings.useZeroCopy = checked
                                }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("Window")

                            C.SettingRow {
                                title: qsTr("Window size")
                                description: qsTr("How the stream window opens.")
                                hint: qsTr("(Fullscreen)")

                                C.ComboBox {
                                    id: windowTypeCombo
                                    Layout.fillWidth: true
                                    popup.width: 500
                                    model: [qsTr("Stream Resolution"), qsTr("Custom Resolution"), qsTr("Adjust Resolution Manually"), qsTr("Fullscreen"), qsTr("Zoom [adjust zoom using slider in stream menu]"), qsTr("Stretch")]
                                    currentIndex: Chiaki.settings.windowType
                                    onActivated: (index) => Chiaki.settings.windowType = index;
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.windowType == 1
                                title: qsTr("Custom size")
                                description: qsTr("Width × height in pixels.")
                                hint: qsTr("(1920 × 1080)")

                                C.TextField {
                                    id: customResolutionWidth
                                    Layout.fillWidth: true
                                    text: Chiaki.settings.customResolutionWidth
                                    Material.accent: text && !validate() ? Material.Red : undefined
                                    onEditingFinished: {
                                        if (validate()) {
                                            Chiaki.settings.customResolutionWidth = parseInt(text);
                                        } else {
                                            Chiaki.settings.customResolutionWidth = 0;
                                            text = "";
                                        }
                                    }
                                    function validate() {
                                        var num = parseInt(text);
                                        return num >= 0 && num <= 9999;
                                    }
                                }

                                Label {
                                    text: "×"
                                }

                                C.TextField {
                                    id: customResolutionHeight
                                    Layout.fillWidth: true
                                    text: Chiaki.settings.customResolutionHeight
                                    Material.accent: text && !validate() ? Material.Red : undefined
                                    onEditingFinished: {
                                        if (validate()) {
                                            Chiaki.settings.customResolutionHeight = parseInt(text);
                                        } else {
                                            Chiaki.settings.customResolutionHeight = 0;
                                            text = "";
                                        }
                                    }
                                    function validate() {
                                        var num = parseInt(text);
                                        return num >= 0 && num <= 9999;
                                    }
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Full screen on double-click")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.fullscreenDoubleClick
                                    onToggled: Chiaki.settings.fullscreenDoubleClick = checked
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Hide the mouse pointer while streaming")
                                hint: qsTr("(On)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.hideCursor
                                    onToggled: Chiaki.settings.hideCursor = !Chiaki.settings.hideCursor
                                }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("Rendering")
                            description: qsTr("Some of these restart the app.")

                            C.SettingRow {
                                title: qsTr("Quality preset")
                                description: qsTr("Upscaling and filters. Also in the stream menu.")
                                hint: qsTr("(High Quality)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Fast"), qsTr("Default"), qsTr("High Quality"), qsTr("High Quality + Spatial Upscaling"), qsTr("High Quality + Advanced Spatial Upscaling"), qsTr("Custom")]
                                    currentIndex: Chiaki.settings.videoPreset
                                    onActivated: (index) => {
                                        Chiaki.settings.videoPreset = index;
                                        switch (index) {
                                        case 0: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Fast; break;
                                        case 1: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Default; break;
                                        case 2: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQuality; break;
                                        case 3: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQualitySpatial; break;
                                        case 4: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.HighQualityAdvancedSpatial; break;
                                        case 5: Chiaki.window.videoPreset = ChiakiWindow.VideoPreset.Custom; break;
                                        }
                                    }
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.window.videoPreset == ChiakiWindow.VideoPreset.Custom
                                title: qsTr("Custom renderer settings")

                                Item { Layout.fillWidth: true }
                                C.Button {
                                    id: customRendererSettings
                                    text: qsTr("Open")
                                    onClicked: root.showPlaceboSettingsDialog()
                                    Material.roundedScale: Material.SmallScale
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Vertical sync")
                                description: qsTr("Avoids tearing; can add a little delay.")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.vSyncEnabled
                                    onClicked: {
                                        Chiaki.settings.vSyncEnabled = checked
                                        if (Chiaki.window.runtimeRendererBackend === 1 && Chiaki.settings.restartApplication())
                                            Qt.quit()
                                    }
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Frame delivery")
                                description: qsTr("Restarts the app.")
                                hint: qsTr("(Direct Mapping)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Direct Mapping"), qsTr("Frame Queue")]
                                    currentIndex: Chiaki.settings.directFrameMapping ? 0 : 1
                                    onActivated: (index) => {
                                        const direct = index === 0;
                                        if (direct === Chiaki.settings.directFrameMapping)
                                            return;
                                        Chiaki.settings.directFrameMapping = direct;
                                        if (Chiaki.settings.restartApplication())
                                            Qt.quit();
                                    }
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Frame mixer")
                                description: qsTr("Only with the frame queue.")
                                hint: qsTr("(None)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("None"), qsTr("Oversample"), qsTr("Hermite"), qsTr("Linear"), qsTr("Cubic")]
                                    enabled: !Chiaki.settings.directFrameMapping
                                    currentIndex: Chiaki.settings.directFrameMapping ? 0 : Chiaki.settings.placeboFrameMixer
                                    onActivated: index => Chiaki.settings.placeboFrameMixer = index
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Renderer")
                                description: qsTr("Restarts the app.")
                                hint: qsTr("(Vulkan)")

                                C.ComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTr("Vulkan"), qsTr("OpenGL")]
                                    currentIndex: Chiaki.settings.rendererBackend
                                    onActivated: (index) => {
                                        if (index === Chiaki.settings.rendererBackend)
                                            return;
                                        Chiaki.settings.rendererBackend = index;
                                        if (Chiaki.settings.restartApplication())
                                            Qt.quit();
                                    }
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.rendererBackend == 0
                                title: qsTr("Vulkan deferred swap")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.vulkanDeferredSwap
                                    onToggled: Chiaki.settings.vulkanDeferredSwap = checked
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Display settings")
                                description: qsTr("Brightness, colours and HDR.")

                                Item { Layout.fillWidth: true }
                                C.Button {
                                    id: displaySettings
                                    text: qsTr("Open")
                                    onClicked: root.showDisplaySettingsDialog()
                                    Material.roundedScale: Material.SmallScale
                                    lastInFocusChain: true
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Stream
                Flickable {
                    id: streamFlick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: streamGrid.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: streamFlick.contentHeight > streamFlick.height
                    }
                    ColumnLayout {
                        id: streamGrid
                        x: Math.max(28, Math.round((streamFlick.width - width) / 2))
                        y: 24
                        width: Math.min(streamFlick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Stream quality")
                            description: qsTr("Each console has its own values, for playing at home and over the internet.")

                            C.SettingRow {
                                title: qsTr("Console")
                                controlWidth: 520

                                C.ComboBox {
                                    id: consoleSelection
                                    Layout.fillWidth: true
                                    model: [qsTr("PS4"), qsTr("PS5")]
                                    currentIndex: selectedConsole
                                    onActivated: (index) => selectedConsole = index
                                    firstInFocusChain: true
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                    KeyNavigation.down: {
                                        if(selectedConsole == SettingsDialog.Console.PS4)
                                            resolutionLocalPS4
                                        else
                                            resolutionLocalPS5

                                    }
                                }
                            }

                            C.SettingRow {
                                controlWidth: 520

                                Label {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    font.pixelSize: 13
                                    font.bold: true
                                    opacity: 0.7
                                    text: qsTr("At home")
                                }
                                Label {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    font.pixelSize: 13
                                    font.bold: true
                                    opacity: 0.7
                                    text: qsTr("Over the internet")
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Resolution")
                                hint: selectedConsole == SettingsDialog.Console.PS5 ? qsTr("(1080p at home, 720p outside)") : qsTr("(720p)")
                                controlWidth: 520

                                C.ComboBox {
                                    id: resolutionLocalPS4
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    model: [qsTr("360p"), qsTr("540p"), qsTr("720p (Default)"), qsTr("1080p (PS5 and PS4 Pro)")]
                                    currentIndex: Chiaki.settings.resolutionLocalPS4 - 1
                                    onActivated: (index) => {
                                        Chiaki.settings.resolutionLocalPS4 = index + 1
                                        Chiaki.settings.bitrateLocalPS4 = 0
                                    }
                                    visible: selectedConsole == SettingsDialog.Console.PS4
                                    KeyNavigation.right: resolutionRemotePS4
                                    KeyNavigation.down: fpsLocalPS4
                                    KeyNavigation.up: consoleSelection
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                }

                                C.ComboBox {
                                    id: resolutionRemotePS4
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    model: [qsTr("360p"), qsTr("540p"), qsTr("720p (Default)"), qsTr("1080p (PS5 and PS4 Pro)")]
                                    currentIndex: Chiaki.settings.resolutionRemotePS4 - 1
                                    onActivated: (index) => {
                                        Chiaki.settings.resolutionRemotePS4 = index + 1
                                        Chiaki.settings.bitrateRemotePS4 = 0
                                    }
                                    visible: selectedConsole == SettingsDialog.Console.PS4
                                    KeyNavigation.left: resolutionLocalPS4
                                    KeyNavigation.down: fpsRemotePS4
                                    KeyNavigation.up: consoleSelection
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                }

                                C.ComboBox {
                                    id: resolutionLocalPS5
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    model: [qsTr("360p"), qsTr("540p"), qsTr("720p"), qsTr("1080p (Default)")]
                                    currentIndex: Chiaki.settings.resolutionLocalPS5 - 1
                                    onActivated: (index) => {
                                        Chiaki.settings.resolutionLocalPS5 = index + 1
                                        Chiaki.settings.bitrateLocalPS5 = 0
                                    }
                                    visible: selectedConsole == SettingsDialog.Console.PS5
                                    KeyNavigation.right: resolutionRemotePS5
                                    KeyNavigation.up: consoleSelection
                                    KeyNavigation.down: fpsLocalPS5
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                }

                                C.ComboBox {
                                    id: resolutionRemotePS5
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    model: [qsTr("360p"), qsTr("540p"), qsTr("720p (Default)"), qsTr("1080p")]
                                    currentIndex: Chiaki.settings.resolutionRemotePS5 - 1
                                    onActivated: (index) => {
                                        Chiaki.settings.resolutionRemotePS5 = index + 1
                                        Chiaki.settings.bitrateRemotePS5 = 0
                                    }
                                    visible: selectedConsole == SettingsDialog.Console.PS5
                                    KeyNavigation.left: resolutionLocalPS5
                                    KeyNavigation.up: consoleSelection
                                    KeyNavigation.down: fpsRemotePS5
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Frame rate")
                                hint: qsTr("(60 fps)")
                                controlWidth: 520

                                C.ComboBox {
                                    id: fpsLocalPS4
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    model: [qsTr("30 fps"), qsTr("60 fps (Default)")]
                                    currentIndex: (Chiaki.settings.fpsLocalPS4 / 30) - 1
                                    onActivated: (index) => Chiaki.settings.fpsLocalPS4 = (index + 1) * 30
                                    visible: selectedConsole == SettingsDialog.Console.PS4
                                    KeyNavigation.up: resolutionLocalPS4
                                    KeyNavigation.right: fpsRemotePS4
                                    KeyNavigation.down: bitrateLocalPS4
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                }

                                C.ComboBox {
                                    id: fpsRemotePS4
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    model: [qsTr("30 fps"), qsTr("60 fps (Default)")]
                                    currentIndex: (Chiaki.settings.fpsRemotePS4 / 30) - 1
                                    onActivated: (index) => Chiaki.settings.fpsRemotePS4 = (index + 1) * 30
                                    visible: selectedConsole == SettingsDialog.Console.PS4
                                    KeyNavigation.up: resolutionRemotePS4
                                    KeyNavigation.left: fpsLocalPS4
                                    KeyNavigation.down: bitrateRemotePS4
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                }

                                C.ComboBox {
                                    id: fpsLocalPS5
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    model: [qsTr("30 fps"), qsTr("60 fps (Default)")]
                                    currentIndex: (Chiaki.settings.fpsLocalPS5 / 30) - 1
                                    onActivated: (index) => Chiaki.settings.fpsLocalPS5 = (index + 1) * 30
                                    visible: selectedConsole == SettingsDialog.Console.PS5
                                    KeyNavigation.up: resolutionLocalPS5
                                    KeyNavigation.right: fpsRemotePS5
                                    KeyNavigation.down: bitrateLocalPS5
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                }

                                C.ComboBox {
                                    id: fpsRemotePS5
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    model: [qsTr("30 fps"), qsTr("60 fps (Default)")]
                                    currentIndex: (Chiaki.settings.fpsRemotePS5 / 30) - 1
                                    onActivated: (index) => Chiaki.settings.fpsRemotePS5 = (index + 1) * 30
                                    visible: selectedConsole == SettingsDialog.Console.PS5
                                    KeyNavigation.up: resolutionRemotePS5
                                    KeyNavigation.left: fpsLocalPS5
                                    KeyNavigation.down: bitrateRemotePS5
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Bitrate")
                                description: qsTr("Mbit/s. In brackets: what the resolution uses by itself.")
                                controlWidth: 520

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    visible: selectedConsole == SettingsDialog.Console.PS4
                                    spacing: 0
                                    C.Slider {
                                        id: bitrateLocalPS4
                                        Layout.fillWidth: true
                                        property var bitrate: {
                                            var rate = 0;
                                            switch (Chiaki.settings.resolutionLocalPS4) {
                                            case 1: rate = 2; break; // 360p
                                            case 2: rate = 6; break; // 540p
                                            case 3: rate = 10; break; // 720p
                                            case 4: rate = 15; break; // 1080p
                                            }
                                            return rate;
                                        }
                                        from: 2
                                        to: 100
                                        stepSize: 1
                                        value: Chiaki.settings.bitrateLocalPS4 / 1000 ? (Chiaki.settings.bitrateLocalPS4 / 1000) : bitrate
                                        onMoved: Chiaki.settings.bitrateLocalPS4 = value * 1000;
                                        KeyNavigation.up: fpsLocalPS4
                                        KeyNavigation.down: bitrateLocalPS4
                                        KeyNavigation.priority: KeyNavigation.BeforeItem
                                    }
                                    Label {
                                        opacity: 0.75
                                        font.pixelSize: 13
                                        text: bitrateLocalPS4.value + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(bitrateLocalPS4.bitrate.toFixed(0))
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    visible: selectedConsole == SettingsDialog.Console.PS4
                                    spacing: 0
                                    C.Slider {
                                        id: bitrateRemotePS4
                                        Layout.fillWidth: true
                                        property var bitrate: {
                                            var rate = 0;
                                            switch (Chiaki.settings.resolutionRemotePS4) {
                                            case 1: rate = 2; break; // 360p
                                            case 2: rate = 6; break; // 540p
                                            case 3: rate = 10; break; // 720p
                                            case 4: rate = 15; break; // 1080p
                                            }
                                            return rate;
                                        }
                                        from: 2
                                        to: 100
                                        stepSize: 1
                                        value: Chiaki.settings.bitrateRemotePS4 / 1000 ? (Chiaki.settings.bitrateRemotePS4 / 1000) : bitrate
                                        onMoved: Chiaki.settings.bitrateRemotePS4 = value * 1000;
                                        KeyNavigation.up: fpsRemotePS4
                                        KeyNavigation.down: bitrateRemotePS4
                                        KeyNavigation.priority: KeyNavigation.BeforeItem
                                        lastInFocusChain: true
                                    }
                                    Label {
                                        opacity: 0.75
                                        font.pixelSize: 13
                                        text: bitrateRemotePS4.value + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(bitrateRemotePS4.bitrate.toFixed(0))
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    visible: selectedConsole == SettingsDialog.Console.PS5
                                    spacing: 0
                                    C.Slider {
                                        id: bitrateLocalPS5
                                        Layout.fillWidth: true
                                        property var bitrate: {
                                            var rate = 0;
                                            switch (Chiaki.settings.resolutionLocalPS5) {
                                            case 1: rate = 2; break; // 360p
                                            case 2: rate = 6; break; // 540p
                                            case 3: rate = 10; break; // 720p
                                            case 4: rate = 15; break; // 1080p
                                            }
                                            return rate;
                                        }
                                        from: 2
                                        to: 100
                                        stepSize: 1
                                        value: Chiaki.settings.bitrateLocalPS5 / 1000 ? (Chiaki.settings.bitrateLocalPS5 / 1000) : bitrate
                                        onMoved: Chiaki.settings.bitrateLocalPS5 = value * 1000;
                                        KeyNavigation.up: fpsLocalPS5
                                        KeyNavigation.down: codecLocalPS5
                                        KeyNavigation.priority: KeyNavigation.BeforeItem
                                    }
                                    Label {
                                        opacity: 0.75
                                        font.pixelSize: 13
                                        text: bitrateLocalPS5.value + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(bitrateLocalPS5.bitrate.toFixed(0))
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    visible: selectedConsole == SettingsDialog.Console.PS5
                                    spacing: 0
                                    C.Slider {
                                        id: bitrateRemotePS5
                                        Layout.fillWidth: true
                                        property var bitrate: {
                                            var rate = 0;
                                            switch (Chiaki.settings.resolutionRemotePS5) {
                                            case 1: rate = 2; break; // 360p
                                            case 2: rate = 6; break; // 540p
                                            case 3: rate = 10; break; // 720p
                                            case 4: rate = 15; break; // 1080p
                                            }
                                            return rate;
                                        }
                                        from: 2
                                        to: 100
                                        stepSize: 1
                                        value: Chiaki.settings.bitrateRemotePS5 / 1000 ? (Chiaki.settings.bitrateRemotePS5 / 1000) : bitrate
                                        onMoved: Chiaki.settings.bitrateRemotePS5 = value * 1000;
                                        KeyNavigation.up: fpsRemotePS5
                                        KeyNavigation.down: codecRemotePS5
                                        KeyNavigation.priority: KeyNavigation.BeforeItem
                                        lastInFocusChain: true
                                    }
                                    Label {
                                        opacity: 0.75
                                        font.pixelSize: 13
                                        text: bitrateRemotePS5.value + qsTr(" Mbps") + qsTr(" (%1 Mbps)").arg(bitrateRemotePS5.bitrate.toFixed(0))
                                    }
                                }
                            }

                            C.SettingRow {
                                visible: selectedConsole == SettingsDialog.Console.PS5
                                title: qsTr("Video codec")
                                description: qsTr("HDR needs the Vulkan renderer.")
                                hint: qsTr("(H265)")
                                controlWidth: 520

                                C.ComboBox {
                                    id: codecLocalPS5
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    property bool openGlBackend: Chiaki.settings.rendererBackend === 1
                                    model: openGlBackend ? [qsTr("H264"), qsTr("H265 (Default)")] : [qsTr("H264"), qsTr("H265 (Default)"), qsTr("H265 HDR")]
                                    currentIndex: Chiaki.settings.codecLocalPS5
                                    onActivated: (index) => Chiaki.settings.codecLocalPS5 = index
                                    visible: selectedConsole == SettingsDialog.Console.PS5
                                    Keys.onReturnPressed: {
                                        if (popup.visible) {
                                            activated(highlightedIndex);
                                            popup.close();
                                        } else
                                            popup.open();
                                    }
                                    KeyNavigation.up: bitrateLocalPS5
                                    KeyNavigation.right: codecRemotePS5
                                    KeyNavigation.down: codecLocalPS5
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                }

                                C.ComboBox {
                                    id: codecRemotePS5
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    property bool openGlBackend: Chiaki.settings.rendererBackend === 1
                                    model: openGlBackend ? [qsTr("H264"), qsTr("H265 (Default)")] : [qsTr("H264"), qsTr("H265 (Default)"), qsTr("H265 HDR")]
                                    currentIndex: Chiaki.settings.codecRemotePS5
                                    onActivated: (index) => Chiaki.settings.codecRemotePS5 = index
                                    visible: selectedConsole == SettingsDialog.Console.PS5
                                    lastInFocusChain: true
                                    Keys.onReturnPressed: {
                                        if (popup.visible) {
                                            activated(highlightedIndex);
                                            popup.close();
                                        } else
                                            popup.open();
                                    }
                                    KeyNavigation.up: bitrateRemotePS5
                                    KeyNavigation.left: codecLocalPS5
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Audio and Wifi
                Flickable {
                    id: audiowifiFlick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: audiowifigrid.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        id: audiowifiScrollbar
                        visible: audiowifiFlick.contentHeight > audiowifiFlick.height
                    }
                    ColumnLayout {
                        id: audiowifigrid
                        x: Math.max(28, Math.round((audiowifiFlick.width - width) / 2))
                        y: 24
                        width: Math.min(audiowifiFlick.width - 56, 980)
                        spacing: 18
                        onVisibleChanged: if (visible) Chiaki.settings.refreshAudioDevices()

                        C.SettingsSection {
                            title: qsTr("Devices")
                            description: qsTr("Also used for the Play Together voice chat.")

                            C.SettingRow {
                                title: qsTr("Speakers")
                                hint: qsTr("(Auto)")

                                C.ComboBox {
                                    id: audioOutDevice
                                    Layout.fillWidth: true
                                    popup.x: (width - popup.width) / 2
                                    popup.width: 700
                                    popup.font.pixelSize: 16
                                    firstInFocusChain: true
                                    model: [qsTr("Auto")].concat(Chiaki.settings.availableAudioOutDevices)
                                    currentIndex: Math.max(0, model.indexOf(Chiaki.settings.audioOutDevice))
                                    onActivated: (index) => Chiaki.settings.audioOutDevice = index ? model[index] : ""
                                    KeyNavigation.down: audioInDevice
                                    KeyNavigation.priority: popup.visible ? KeyNavigation.AfterItem : KeyNavigation.BeforeItem
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Microphone")
                                hint: qsTr("(Auto)")

                                C.ComboBox {
                                    id: audioInDevice
                                    Layout.fillWidth: true
                                    popup.x: (width - popup.width) / 2
                                    popup.width: 700
                                    popup.font.pixelSize: 16
                                    model: [qsTr("Auto")].concat(Chiaki.settings.availableAudioInDevices)
                                    currentIndex: Math.max(0, model.indexOf(Chiaki.settings.audioInDevice))
                                    onActivated: (index) => Chiaki.settings.audioInDevice = index ? model[index] : ""
                                    KeyNavigation.up: audioOutDevice
                                    KeyNavigation.down: audioBufferSizeSlider
                                    KeyNavigation.priority: popup.visible ? KeyNavigation.AfterItem : KeyNavigation.BeforeItem
                                }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("Sound")

                            C.SettingRow {
                                title: qsTr("Audio buffer")
                                description: qsTr("Larger is smoother on a weak connection, smaller has less delay.")
                                hint: qsTr("(50 ms)")

                                C.Slider {
                                    id: audioBufferSizeSlider
                                    Layout.fillWidth: true
                                    from: 1
                                    to: 10
                                    stepSize: 1
                                    value: Chiaki.settings.audioBufferSize / 1920 ? (Chiaki.settings.audioBufferSize / 1920) : 5
                                    onMoved: Chiaki.settings.audioBufferSize = value * 1920;
                                    KeyNavigation.up: audioInDevice
                                }
                                Label {
                                    Layout.preferredWidth: 90
                                    horizontalAlignment: Text.AlignRight
                                    text: (audioBufferSizeSlider.value * 10).toFixed(0) + qsTr(" ms")
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Volume")
                                hint: qsTr("(100%)")

                                C.Slider {
                                    id: volumeSettingSlider
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 128
                                    stepSize: 1
                                    value: Chiaki.settings.audioVolume
                                    onMoved: Chiaki.settings.audioVolume = value
                                }
                                Label {
                                    Layout.preferredWidth: 90
                                    horizontalAlignment: Text.AlignRight
                                    text: ((volumeSettingSlider.value / 128.0) * 100).toFixed(0) + qsTr(" %")
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Console microphone on at the start")
                                description: qsTr("The microphone the console hears, not the voice chat.")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.startMicUnmuted
                                    onToggled: Chiaki.settings.startMicUnmuted = checked
                                }
                            }

                            C.SettingRow {
                                visible: typeof Chiaki.settings.speechProcessing !== "undefined"
                                title: qsTr("Speech processing")
                                description: qsTr("Noise suppression and echo cancellation.")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.speechProcessing
                                    onToggled: Chiaki.settings.speechProcessing = !Chiaki.settings.speechProcessing
                                }
                            }

                            C.SettingRow {
                                visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                                title: qsTr("Noise to suppress")
                                hint: qsTr("(6 dB)")

                                C.Slider {
                                    id: noiseSlider
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 60
                                    stepSize: 1
                                    value: Chiaki.settings.noiseSuppressLevel
                                    onMoved: Chiaki.settings.noiseSuppressLevel = value
                                }
                                Label {
                                    Layout.preferredWidth: 90
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr("%1 dB").arg(noiseSlider.value)
                                }
                            }

                            C.SettingRow {
                                visible: if (typeof Chiaki.settings.speechProcessing !== "undefined") {Chiaki.settings.speechProcessing} else {false}
                                title: qsTr("Echo to suppress")
                                hint: qsTr("(30 dB)")

                                C.Slider {
                                    id: echoSlider
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 60
                                    stepSize: 1
                                    value: Chiaki.settings.echoSuppressLevel
                                    onMoved: Chiaki.settings.echoSuppressLevel = value
                                }
                                Label {
                                    Layout.preferredWidth: 90
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr("%1 dB").arg(echoSlider.value)
                                }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("Connection")

                            C.SettingRow {
                                title: qsTr("Weak Wi-Fi warning")
                                description: qsTr("Warn when at least this share of packets is lost.")
                                hint: qsTr("(3%)")

                                C.Slider {
                                    id: wifiSlider
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 100
                                    stepSize: 1
                                    value: Chiaki.settings.wifiDroppedNotif
                                    onMoved: Chiaki.settings.wifiDroppedNotif = value
                                }
                                Label {
                                    Layout.preferredWidth: 90
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr("%1 %").arg(wifiSlider.value)
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Packet loss reported to the console")
                                description: qsTr("At most this much, so the console does not lower the quality too far.")
                                hint: qsTr("(5%)")

                                C.Slider {
                                    id: lossSlider
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 100
                                    stepSize: 1
                                    value: Chiaki.settings.packetLossReportedMax
                                    onMoved: Chiaki.settings.packetLossReportedMax = value
                                }
                                Label {
                                    Layout.preferredWidth: 90
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr("%1 %").arg(lossSlider.value)
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Ask for a full picture after a repair failed")
                                description: qsTr("Requests a key frame when error correction cannot fix the stream.")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.iDROnFECFailureEnabled
                                    onToggled: Chiaki.settings.iDROnFECFailureEnabled = !Chiaki.settings.iDROnFECFailureEnabled
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Stream statistics while playing")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    lastInFocusChain: true
                                    checked: Chiaki.settings.showStreamStats
                                    onToggled: Chiaki.settings.showStreamStats = !Chiaki.settings.showStreamStats
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Consoles
                Flickable {
                    id: consolesFlick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: consolesLayout.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: consolesFlick.contentHeight > consolesFlick.height
                    }
                    ColumnLayout {
                        id: consolesLayout
                        x: Math.max(28, Math.round((consolesFlick.width - width) / 2))
                        y: 24
                        width: Math.min(consolesFlick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Add a console")

                            C.SettingRow {
                                title: qsTr("Register a console")
                                description: qsTr("Pair this app with your PS5 or PS4 once, with the PIN shown in the console's Remote Play settings. Consoles on this network also show up on the start page by themselves.")

                                Item { Layout.fillWidth: true }
                                C.Button {
                                    id: registerNewButton
                                    firstInFocusChain: true
                                    text: qsTr("Register a console")
                                    onClicked: root.showRegistDialog("255.255.255.255", true)
                                    Material.roundedScale: Material.SmallScale
                                }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("Registered consoles")
                            description: qsTr("Auto-connect starts a session with that console as soon as the app opens.")

                            Label {
                                id: consolesLabel
                                Layout.topMargin: 10
                                font.pixelSize: 13
                                visible: consolesView.count === 0
                                opacity: 0.55
                                text: qsTr("No console registered yet.")
                            }

                            ListView {
                                id: consolesView
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                Layout.preferredHeight: count * 64
                                interactive: false
                                keyNavigationEnabled: false
                                clip: true
                                model: Chiaki.settings.registeredHosts
                                delegate: ItemDelegate {
                                text: "%1  ·  %2  ·  %3".arg(modelData.name).arg(modelData.ps5 ? "PS5" : "PS4").arg(Chiaki.settings.streamerMode ? qsTr("ID hidden") : modelData.mac)
                                font.pixelSize: 15
                                height: 64
                                width: parent ? parent.width : 0
                                leftPadding: autoConnectButton.width + 40

                                CheckBox {
                                    property bool firstInFocusChain: false
                                    property bool lastInFocusChain: false
                                    property bool lastDownInFocusChain: index > consolesView.count + hiddenConsolesView.count - 2

                                    id: autoConnectButton
                                    anchors {
                                        left: parent.left
                                        verticalCenter: parent.verticalCenter
                                        leftMargin: 20
                                    }
                                    text: qsTr("Auto-Connect")
                                    checked: Chiaki.settings.autoConnectMac == modelData.mac
                                    onToggled: Chiaki.settings.autoConnectMac = checked ? modelData.mac : "";

                                    Keys.onPressed: (event) => {
                                        switch (event.key) {
                                        case Qt.Key_Right:
                                            if (!lastInFocusChain) {
                                                let item = nextItemInFocusChain();
                                                if (item)
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                event.accepted = true;
                                            }
                                            break;
                                        case Qt.Key_Up:
                                            if (!firstInFocusChain) {
                                                let item = nextItemInFocusChain(false);
                                                if (item) {
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                    dialog.ensureItemVisibleInFlick(consolesView, item);
                                                }
                                                let count = index > 0 ? 2: 0;
                                                for(var i = 0; i < count; i++)
                                                {
                                                    let item2 = item.nextItemInFocusChain(false);
                                                    if (item)
                                                    {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        dialog.ensureItemVisibleInFlick(consolesView, item);
                                                        item = item2;
                                                    }
                                                }
                                                event.accepted = true;
                                            }
                                            break;
                                        case Qt.Key_Down:
                                            if (!lastDownInFocusChain) {
                                                let item = nextItemInFocusChain();
                                                if (item) {
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                    dialog.ensureItemVisibleInFlick(consolesView, item);
                                                }
                                                let count = 2;
                                                for(var i = 0; i < count; i++)
                                                {
                                                    let item2 = item.nextItemInFocusChain();
                                                    if (item)
                                                    {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        dialog.ensureItemVisibleInFlick(consolesView, item);
                                                        item = item2;
                                                    }
                                                }
                                                event.accepted = true;
                                            }
                                            break;
                                        case Qt.Key_Return:
                                            if (visualFocus) {
                                                toggle();
                                                toggled();
                                            }
                                            event.accepted = true;
                                            break;
                                        }
                                    }
                                }

                                Button {
                                    property bool firstInFocusChain: false
                                    property bool lastInFocusChain: index > consolesView.count + hiddenConsolesView.count - 2
                                    Material.background: visualFocus ? Material.accent : undefined

                                    Component.onDestruction: {
                                        if (visualFocus) {
                                            let item = nextItemInFocusChain();
                                            if (item)
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                        }
                                    }
                                    Keys.onPressed: (event) => {
                                        switch (event.key) {
                                            case Qt.Key_Left:
                                                if (!firstInFocusChain) {
                                                    let item = nextItemInFocusChain(false);
                                                    if (item)
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Up:
                                                if (!firstInFocusChain)
                                                {
                                                    let item = nextItemInFocusChain(false);
                                                    if (item) {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        dialog.ensureItemVisibleInFlick(consolesView, item);
                                                    }
                                                    let count = 2;
                                                    for(var i = 0; i < count; i++)
                                                    {
                                                        let item2 = item.nextItemInFocusChain(false);
                                                        if (item)
                                                        {
                                                            item.forceActiveFocus(Qt.TabFocusReason);
                                                            dialog.ensureItemVisibleInFlick(consolesView, item);
                                                            item = item2;
                                                        }
                                                    }
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Down:
                                                if (!lastInFocusChain) {
                                                    let item = nextItemInFocusChain();
                                                    if (item) {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        dialog.ensureItemVisibleInFlick(consolesView, item);
                                                    }
                                                    let count = index < consolesView.count - 1 ? 2: 0;
                                                    for(var i = 0; i < count; i++)
                                                    {
                                                        let item2 = item.nextItemInFocusChain();
                                                        if (item)
                                                        {
                                                            item.forceActiveFocus(Qt.TabFocusReason);
                                                            dialog.ensureItemVisibleInFlick(consolesView, item);
                                                            item = item2;
                                                        }
                                                    }
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Return:
                                                if (visualFocus) {
                                                    clicked();
                                                }
                                                event.accepted = true;
                                                break;
                                        }
                                    }
                                    anchors {
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                        rightMargin: 20
                                    }
                                    text: qsTr("Delete")
                                    onClicked: root.showConfirmDialog(qsTr("Delete Console"), qsTr("Are you sure you want to delete this console?"), () => Chiaki.settings.deleteRegisteredHost(index));
                                    Material.roundedScale: Material.SmallScale
                                    Material.accent: Material.Red
                                }
                            }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("Hidden consoles")
                            description: qsTr("Consoles you hid on the start page. Unhide one to see it there again.")

                            Label {
                                id: hiddenConsolesLabel
                                Layout.topMargin: 10
                                font.pixelSize: 13
                                visible: hiddenConsolesView.count === 0
                                opacity: 0.55
                                text: qsTr("None hidden.")
                            }

                            ListView {
                                id: hiddenConsolesView
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                Layout.preferredHeight: count * 64
                                interactive: false
                                keyNavigationEnabled: false
                                clip: true
                                model: Chiaki.hiddenHosts
                                delegate: ItemDelegate {
                                text: "%1  ·  %2".arg(modelData.name).arg(Chiaki.settings.streamerMode ? qsTr("ID hidden") : modelData.mac)
                                font.pixelSize: 15
                                height: 64
                                width: parent ? parent.width : 0

                                Button {
                                    property bool firstInFocusChain: false
                                    property bool lastInFocusChain: index > hiddenConsolesView.count - 2
                                    Material.background: visualFocus ? Material.accent : undefined

                                    Component.onDestruction: {
                                        if (visualFocus) {
                                            let item = nextItemInFocusChain();
                                            if (item)
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                        }
                                    }
                                    Keys.onPressed: (event) => {
                                        switch (event.key) {
                                            case Qt.Key_Up:
                                                if (!firstInFocusChain)
                                                {
                                                    let item = nextItemInFocusChain(false);
                                                    if (item) {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        dialog.ensureItemVisibleInFlick(hiddenConsolesView, item);
                                                    }
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Down:
                                                if (!lastInFocusChain) {
                                                    let item = nextItemInFocusChain();
                                                    if (item) {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        dialog.ensureItemVisibleInFlick(hiddenConsolesView, item);
                                                    }
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Return:
                                                if (visualFocus) {
                                                    clicked();
                                                }
                                                event.accepted = true;
                                                break;
                                        }
                                    }
                                    anchors {
                                        right: parent.right
                                        verticalCenter: parent.verticalCenter
                                        rightMargin: 20
                                    }
                                    text: qsTr("Unhide")
                                    onClicked: root.showConfirmDialog(qsTr("Unhide Console"), qsTr("Are you sure you want to unhide this console?"), () => Chiaki.unhideHost(modelData.mac));
                                    Material.roundedScale: Material.SmallScale
                                    Material.accent: Material.Red
                                }
                            }
                            }
                        }
                    }
                }
            }

            Item {
                // Keys
                id: controllerMapping
                Flickable {
                    id: keysFlick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: keysPage.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    function ensureItemVisible(item) {
                        if (!item)
                            return;
                        const top = item.mapToItem(keysFlick.contentItem, 0, 0).y;
                        const bottom = top + item.height;
                        if (top < contentY)
                            contentY = top;
                        else if (bottom > contentY + height)
                            contentY = bottom - height;
                    }
                    ScrollBar.vertical: ScrollBar {
                        visible: keysFlick.contentHeight > keysFlick.height
                    }
                    ColumnLayout {
                        id: keysPage
                        x: Math.max(28, Math.round((keysFlick.width - width) / 2))
                        y: 24
                        width: Math.min(keysFlick.width - 56, 980)
                        spacing: 18

                    C.SettingsSection {
                        title: qsTr("Keyboard and mouse")
                        description: qsTr("Play with the keyboard as a controller. Click a key below to change it.")

                    GridLayout {
                        id: keysGrid
                        Layout.fillWidth: true
                        Layout.topMargin: 12
                        Layout.bottomMargin: 4
                        columns: 3
                        rowSpacing: 6
                        columnSpacing: 18

                        Button {
                            id: resetAllKeys
                            text: qsTr("Reset all keys")
                            Layout.alignment: Qt.AlignLeft
                            property bool firstInFocusChain: true
                            property bool lastInFocusChain: false
                            onClicked: Chiaki.settings.clearKeyMapping()
                            Material.roundedScale: Material.SmallScale
                            Material.background: visualFocus ? Material.accent : undefined
                            onActiveFocusChanged: if (activeFocus) keysFlick.ensureItemVisible(this)

                            Component.onDestruction: {
                                if (visualFocus) {
                                    let item = nextItemInFocusChain();
                                    if (item)
                                        item.forceActiveFocus(Qt.TabFocusReason);
                                }
                            }
                            Keys.onPressed: (event) => {
                                switch (event.key) {
                                case Qt.Key_Down:
                                    if (!lastInFocusChain) {
                                        let item = nextItemInFocusChain();
                                        if (item) {
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                            keysFlick.ensureItemVisible(item);
                                        }
                                        for(var i = 0; i < 3; i++)
                                        {
                                            let item2 = item.nextItemInFocusChain();
                                            if (item)
                                            {
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                                keysFlick.ensureItemVisible(item);
                                                item = item2;
                                            }
                                        }
                                        event.accepted = true;
                                    }
                                    break;
                                case Qt.Key_Right:
                                    if (!lastInFocusChain) {
                                        let item = nextItemInFocusChain();
                                        if (item) {
                                            item.forceActiveFocus(Qt.TabFocusReason);
                                            keysFlick.ensureItemVisible(item);
                                        }
                                        event.accepted = true;
                                    }
                                    break;
                                case Qt.Key_Return:
                                    if (visualFocus) {
                                        clicked();
                                    }
                                    event.accepted = true;
                                    break;
                                }
                            }
                        }

                        CheckBox {
                            text: qsTr("Keyboard as controller")
                            checked: {
                                Chiaki.settings.keyboardEnabled
                            }
                            onToggled: Chiaki.settings.keyboardEnabled = checked
                            Layout.alignment: Qt.AlignLeft
                            property bool firstInFocusChain: false
                            property bool lastInFocusChain: false
                            Material.roundedScale: Material.SmallScale
                            Material.background: visualFocus ? Material.accent : undefined
                            onActiveFocusChanged: if (activeFocus) keysFlick.ensureItemVisible(this)

                            Component.onDestruction: {
                                if (visualFocus) {
                                    let item = nextItemInFocusChain();
                                    if (item)
                                        item.forceActiveFocus(Qt.TabFocusReason);
                                }
                            }
                            Keys.onPressed: (event) => {
                                switch (event.key) {
                                    case Qt.Key_Left:
                                        if (!firstInFocusChain) {
                                            let item = nextItemInFocusChain(false);
                                            if (item) {
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                                keysFlick.ensureItemVisible(item);
                                            }
                                            event.accepted = true;
                                        }
                                        break;
                                    case Qt.Key_Right:
                                        if  (!lastInFocusChain) {
                                            let item = nextItemInFocusChain();
                                            if (item) {
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                                keysFlick.ensureItemVisible(item);
                                            }
                                            event.accepted = true;
                                        }
                                        break;
                                    case Qt.Key_Down:
                                        if (!lastInFocusChain) {
                                            let item = nextItemInFocusChain();
                                            if (item) {
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                                keysFlick.ensureItemVisible(item);
                                            }
                                            for(var i = 0; i < 3; i++)
                                            {
                                                let item2 = item.nextItemInFocusChain();
                                                if (item)
                                                {
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                    keysFlick.ensureItemVisible(item);
                                                    item = item2;
                                                }
                                            }
                                            event.accepted = true;
                                        }
                                        break;
                                    case Qt.Key_Return:
                                        if (visualFocus) {
                                            toggle();
                                            toggled();
                                        }
                                        event.accepted = true;
                                        break;
                                }
                            }
                        }
                        CheckBox {
                            text: qsTr("Enable Mouse Touchpad")
                            checked: {
                                Chiaki.settings.mouseTouchEnabled
                            }
                            onToggled: Chiaki.settings.mouseTouchEnabled = checked
                            Layout.alignment: Qt.AlignLeft
                            property bool firstInFocusChain: false
                            property bool lastInFocusChain: false
                            Material.roundedScale: Material.SmallScale
                            Material.background: visualFocus ? Material.accent : undefined
                            onActiveFocusChanged: if (activeFocus) keysFlick.ensureItemVisible(this)

                            Component.onDestruction: {
                                if (visualFocus) {
                                    let item = nextItemInFocusChain();
                                    if (item)
                                        item.forceActiveFocus(Qt.TabFocusReason);
                                }
                            }
                            Keys.onPressed: (event) => {
                                switch (event.key) {
                                    case Qt.Key_Left:
                                        if (!firstInFocusChain) {
                                            let item = nextItemInFocusChain(false);
                                            if (item) {
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                                keysFlick.ensureItemVisible(item);
                                            }
                                            event.accepted = true;
                                        }
                                        break;
                                    case Qt.Key_Down:
                                        if (!lastInFocusChain) {
                                            let item = nextItemInFocusChain();
                                            if (item) {
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                                keysFlick.ensureItemVisible(item);
                                            }
                                            for(var i = 0; i < 3; i++)
                                            {
                                                let item2 = item.nextItemInFocusChain();
                                                if (item)
                                                {
                                                    item.forceActiveFocus(Qt.TabFocusReason);
                                                    keysFlick.ensureItemVisible(item);
                                                    item = item2;
                                                }
                                            }
                                            event.accepted = true;
                                        }
                                        break;
                                    case Qt.Key_Return:
                                        if (visualFocus) {
                                            toggle();
                                            toggled();
                                        }
                                        event.accepted = true;
                                        break;
                                }
                            }
                        }
                        Repeater {
                            id: chiakiKeys
                            model: Chiaki.settings.controllerMapping

                            RowLayout {
                                spacing: 20

                                Label {
                                    Layout.preferredWidth: 130
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    opacity: 0.8
                                    font.pixelSize: 13
                                    text: modelData.buttonName
                                }

                                Button {
                                    property bool firstInFocusChain: false
                                    property bool lastInFocusChain: index == (chiakiKeys.count - 1)
                                    Layout.preferredWidth: 120
                                    Layout.preferredHeight: 44
                                    font.pixelSize: 14
                                    text: modelData.keyName
                                    Material.roundedScale: Material.SmallScale
                                    Material.background: visualFocus ? Material.accent : undefined
                                    onActiveFocusChanged: if (activeFocus) keysFlick.ensureItemVisible(this)
                                    Component.onDestruction: {
                                        if (visualFocus) {
                                            let item = nextItemInFocusChain();
                                            if (item)
                                                item.forceActiveFocus(Qt.TabFocusReason);
                                        }
                                    }
                                    onClicked: {
                                        keyDialog.show({
                                            value: modelData.buttonValue,
                                            mappingIndex: index,
                                            callback: (name) => text = name,
                                        });
                                    }
                                    Keys.onPressed: (event) => {
                                        switch (event.key) {
                                            case Qt.Key_Left:
                                                if (!firstInFocusChain && ((index % 3) != 0)) {
                                                    let item = nextItemInFocusChain(false);
                                                    if (item) {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        keysFlick.ensureItemVisible(item);
                                                    }
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Right:
                                                if  (!lastInFocusChain && (index % 3) != 2) {
                                                    let item = nextItemInFocusChain();
                                                    if (item) {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        keysFlick.ensureItemVisible(item);
                                                    }
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Up:
                                                if (!firstInFocusChain)
                                                {
                                                    let item = nextItemInFocusChain(false);
                                                    if (item) {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        keysFlick.ensureItemVisible(item);
                                                    }
                                                    for(var i = 0; i < 3; i++)
                                                    {
                                                        let item2 = item.nextItemInFocusChain(false);
                                                        if (item)
                                                        {
                                                            item.forceActiveFocus(Qt.TabFocusReason);
                                                            keysFlick.ensureItemVisible(item);
                                                            item = item2;
                                                        }
                                                    }
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Down:
                                                if (!lastInFocusChain && index < (chiakiKeys.count - 3)) {
                                                    let item = nextItemInFocusChain();
                                                    if (item) {
                                                        item.forceActiveFocus(Qt.TabFocusReason);
                                                        keysFlick.ensureItemVisible(item);
                                                    }
                                                    for(var i = 0; i < 3; i++)
                                                    {
                                                        let item2 = item.nextItemInFocusChain();
                                                        if (item)
                                                        {
                                                            item.forceActiveFocus(Qt.TabFocusReason);
                                                            keysFlick.ensureItemVisible(item);
                                                            item = item2;
                                                        }
                                                    }
                                                    event.accepted = true;
                                                }
                                                break;
                                            case Qt.Key_Return:
                                                if (visualFocus) {
                                                    clicked();
                                                }
                                                break;
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

            Item {
                // Controllers
                Flickable {
                    id: controllersFlick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: controllersLayout.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        id: controllersScrollbar
                        visible: controllersFlick.contentHeight > controllersFlick.height
                    }
                    ColumnLayout {
                        id: controllersLayout
                        x: Math.max(28, Math.round((controllersFlick.width - width) / 2))
                        y: 24
                        width: Math.min(controllersFlick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Button mapping")

                            C.SettingRow {
                                title: qsTr("Map your controller")
                                description: qsTr("Teach the app which button is which, for controllers it does not know yet.")
                                controlWidth: 420

                                Item { Layout.fillWidth: true }
                                C.Button {
                                    id: controllerMappingChange
                                    firstInFocusChain: true
                                    text: qsTr("Change mapping")
                                    onClicked: controllerMappingDialog.show({
                                        reset: false
                                    });
                                }
                                C.Button {
                                    id: controllerMappingReset
                                    text: qsTr("Reset")
                                    onClicked: controllerMappingDialog.show({
                                        reset: true
                                    });
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Controller input in the background")
                                description: qsTr("Keep reading the controller while another window is in front.")
                                hint: qsTr("(On)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    id: backgroundController
                                    checked: {
                                        Chiaki.settings.allowJoystickBackgroundEvents
                                    }
                                    onToggled: Chiaki.settings.allowJoystickBackgroundEvents = checked
                                }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("Touchpad from the D-pad")
                            description: qsTr("For controllers without a touchpad: switch the D-pad to swiping on the touchpad.")

                            C.SettingRow {
                                title: qsTr("D-pad touchpad")
                                hint: qsTr("(On)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    id: dpadTouch
                                    checked: Chiaki.settings.dpadTouchEnabled
                                    onToggled: Chiaki.settings.dpadTouchEnabled = !Chiaki.settings.dpadTouchEnabled
                                    KeyNavigation.priority: KeyNavigation.BeforeItem
                                    KeyNavigation.up: backgroundController
                                    KeyNavigation.left: dpadTouch
                                    KeyNavigation.right: dpadTouch
                                    KeyNavigation.down: {
                                        if(touchIncrement.visible)
                                            touchIncrement
                                        else
                                            posButtons
                                    }
                                }
                            }

                            C.SettingRow {
                                id: touchIncrementLayout
                                visible: Chiaki.settings.dpadTouchEnabled
                                title: qsTr("Swipe step")
                                hint: qsTr("(0.3 mm)")

                                C.Slider {
                                    id: touchIncrement
                                    Layout.fillWidth: true
                                    from: 1
                                    to: 1079
                                    stepSize: 1
                                    value: Chiaki.settings.dpadTouchIncrement
                                    onMoved: Chiaki.settings.dpadTouchIncrement = value
                                    KeyNavigation.priority: KeyNavigation.BeforeItem
                                    KeyNavigation.up: dpadTouch
                                    KeyNavigation.down: {
                                        if(dpadShortcut1.visible)
                                            dpadShortcut1
                                        else
                                            posButtons;
                                    }
                                }
                                Label {
                                    Layout.preferredWidth: 90
                                    horizontalAlignment: Text.AlignRight
                                    text: qsTr("%1 mm").arg(touchIncrement.value / 100)
                                }
                            }

                            C.SettingRow {
                                visible: Chiaki.settings.dpadTouchEnabled
                                title: qsTr("Switch with")
                                description: qsTr("Hold these together to change between the D-pad and the touchpad.")
                                hint: qsTr("(L1+R1+Dpad Up)")
                                controlWidth: 620

                                C.ComboBox {
                                    id: dpadShortcut1
                                    font.pixelSize: 14
                                    Layout.fillWidth: true
                                    firstInFocusChain: false
                                    model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                    currentIndex: Chiaki.settings.dpadTouchShortcut1
                                    onActivated: index => Chiaki.settings.dpadTouchShortcut1 = index
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                    KeyNavigation.up: touchIncrement
                                    KeyNavigation.down: posButtons
                                    KeyNavigation.left: dpadShortcut1
                                    KeyNavigation.right: dpadShortcut2
                                }

                                C.ComboBox {
                                    id: dpadShortcut2
                                    font.pixelSize: 14
                                    Layout.fillWidth: true
                                    firstInFocusChain: false
                                    model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                    currentIndex: Chiaki.settings.dpadTouchShortcut2
                                    onActivated: index => Chiaki.settings.dpadTouchShortcut2 = index
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                    KeyNavigation.up: touchIncrement
                                    KeyNavigation.down: posButtons
                                    KeyNavigation.left: dpadShortcut1
                                    KeyNavigation.right: dpadShortcut3
                                }

                                C.ComboBox {
                                    id: dpadShortcut3
                                    font.pixelSize: 14
                                    Layout.fillWidth: true
                                    firstInFocusChain: false
                                    model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                    currentIndex: Chiaki.settings.dpadTouchShortcut3
                                    onActivated: index => Chiaki.settings.dpadTouchShortcut3 = index
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                    KeyNavigation.up: touchIncrement
                                    KeyNavigation.down: posButtons
                                    KeyNavigation.left: dpadShortcut2
                                    KeyNavigation.right: dpadShortcut4
                                }

                                C.ComboBox {
                                    id: dpadShortcut4
                                    font.pixelSize: 14
                                    Layout.fillWidth: true
                                    firstInFocusChain: false
                                    model: [qsTr("Not Used"), qsTr("Cross"), qsTr("Moon"), qsTr("Box"), qsTr("Pyramid"), qsTr("Dpad Left"), qsTr("Dpad Right"), qsTr("Dpad Up"), qsTr("Dpad Down"), qsTr("L1"), qsTr("R1"), qsTr("L3"), qsTr("R3"), qsTr("Options"), qsTr("Share"), qsTr("Touchpad"), qsTr("PS")]
                                    currentIndex: Chiaki.settings.dpadTouchShortcut4
                                    onActivated: index => Chiaki.settings.dpadTouchShortcut4 = index
                                    KeyNavigation.priority: {
                                        if(!popup.visible)
                                            KeyNavigation.BeforeItem
                                        else
                                            KeyNavigation.AfterItem
                                    }
                                    KeyNavigation.up: touchIncrement
                                    KeyNavigation.down: posButtons
                                    KeyNavigation.left: dpadShortcut3
                                    KeyNavigation.right: dpadShortcut4
                                }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("Buttons and haptics")

                            C.SettingRow {
                                title: qsTr("Buttons by position")
                                description: qsTr("Use buttons by where they are instead of by their label (for Nintendo-style layouts).")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    id: posButtons
                                    checked: Chiaki.settings.buttonsByPosition
                                    onToggled: Chiaki.settings.buttonsByPosition = checked
                                    KeyNavigation.priority: KeyNavigation.BeforeItem
                                    KeyNavigation.up: {
                                        if(dpadShortcut1.visible)
                                            dpadShortcut1
                                        else
                                            dpadTouch;
                                    }
                                    KeyNavigation.down: rumbleHaptics
                                    KeyNavigation.left: posButtons
                                    KeyNavigation.right: posButtons
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Rumble strength")
                                description: qsTr("For controllers without the DualSense's haptics.")
                                hint: qsTr("(Normal)")

                                C.ComboBox {
                                    id: rumbleHaptics
                                    Layout.fillWidth: true
                                    model: [qsTr("Off"), qsTr("Very Weak"), qsTr("Weak"), qsTr("Normal"), qsTr("Strong"), qsTr("Very Strong")]
                                    currentIndex: Chiaki.settings.rumbleHapticsIntensity
                                    onActivated: (index) => Chiaki.settings.rumbleHapticsIntensity = index;
                                }
                            }

                            C.SettingRow {
                                title: qsTr("DualSense haptics strength")
                                description: qsTr("Compared to what the console sets.")
                                hint: qsTr("(console setting)")

                                C.Slider {
                                    id: hapticOverride
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 2
                                    stepSize: 0.1
                                    value: Chiaki.settings.hapticOverride
                                    onMoved: Chiaki.settings.hapticOverride = value;
                                    lastInFocusChain: true
                                }
                                Label {
                                    Layout.preferredWidth: 90
                                    horizontalAlignment: Text.AlignRight
                                    text: {
                                        if(hapticOverride.value > 0.99 && hapticOverride.value < 1.01)
                                            qsTr("as set")
                                        else
                                            (hapticOverride.value * 100).toFixed(0) + qsTr(" %")
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Remote (PSN and Hole Punching)
                Flickable {
                    id: remoteFlick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: remoteGrid.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: remoteFlick.contentHeight > remoteFlick.height
                    }
                    ColumnLayout {
                        id: remoteGrid
                        x: Math.max(28, Math.round((remoteFlick.width - width) / 2))
                        y: 24
                        width: Math.min(remoteFlick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("PlayStation Network")
                            description: qsTr("With PSN, consoles register by themselves and you can play your own console from outside your home without opening ports.")

                            C.SettingRow {
                                readonly property bool loggedIn: Chiaki.settings.psnRefreshToken && Chiaki.settings.psnAuthToken && Chiaki.settings.psnAuthTokenExpiry && Chiaki.settings.psnAccountId
                                title: loggedIn ? qsTr("Logged in") : qsTr("Not logged in")
                                description: loggedIn ? qsTr("Your PSN consoles show up on the start page.") : qsTr("Log in once; the app keeps a token, not your password.")

                                Item { Layout.fillWidth: true }
                                C.Button {
                                    id: openPsnLogin
                                    firstInFocusChain: visible
                                    text: qsTr("Log in to PSN")
                                    onClicked: {
                                        root.showPSNTokenDialog(false)
                                    }
                                    Material.roundedScale: Material.SmallScale
                                    visible: !Chiaki.settings.psnRefreshToken || !Chiaki.settings.psnAuthToken || !Chiaki.settings.psnAuthTokenExpiry || !Chiaki.settings.psnAccountId
                                }

                                C.Button {
                                    id: resetPsnTokens
                                    text: qsTr("Log out")
                                    firstInFocusChain: !openPsnLogin.visible
                                    onClicked: {
                                        Chiaki.settings.psnRefreshToken = ""
                                        Chiaki.settings.psnAuthToken = ""
                                        Chiaki.settings.psnAuthTokenExpiry = ""
                                        Chiaki.settings.psnAccountId = ""
                                        openPsnLogin.forceActiveFocus(Qt.TabFocusReason);
                                    }
                                    Material.roundedScale: Material.SmallScale
                                    visible: Chiaki.settings.psnRefreshToken && Chiaki.settings.psnAuthToken && Chiaki.settings.psnAuthTokenExpiry && Chiaki.settings.psnAccountId
                                }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("Hole punching")
                            description: qsTr("How a PSN connection finds its way through routers. Only change this if connecting from outside fails.")

                            C.SettingRow {
                                title: qsTr("Force port guessing")
                                description: qsTr("Guess the router's port when STUN alone does not work.")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    id: holePunchGuessingCheckbox
                                    checked: Chiaki.settings.portGuessingEnabled
                                    onToggled: Chiaki.settings.portGuessingEnabled = checked
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Port guesses")
                                hint: qsTr("(75)")

                                C.Slider {
                                    id: portGuessCountSlider
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 75
                                    stepSize: 1
                                    value: Chiaki.settings.portGuessCount
                                    onMoved: Chiaki.settings.portGuessCount = value
                                }
                                Label {
                                    Layout.preferredWidth: 90
                                    horizontalAlignment: Text.AlignRight
                                    text: portGuessCountSlider.value
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Sockets for guessing")
                                hint: qsTr("(250)")

                                C.Slider {
                                    id: portGuessSocketSlider
                                    Layout.fillWidth: true
                                    from: 0
                                    to: 500
                                    stepSize: 1
                                    value: Chiaki.settings.portGuessSocketCount
                                    onMoved: Chiaki.settings.portGuessSocketCount = value
                                    lastInFocusChain: true
                                }
                                Label {
                                    Layout.preferredWidth: 90
                                    horizontalAlignment: Text.AlignRight
                                    text: portGuessSocketSlider.value
                                }
                            }
                        }
                    }
                }
            }

            Item {
                // Config (Options such as Import/Export and Logging)
                Flickable {
                    id: configFlick
                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: configGrid.implicitHeight + 48
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        visible: configFlick.contentHeight > configFlick.height
                    }
                    ColumnLayout {
                        id: configGrid
                        x: Math.max(28, Math.round((configFlick.width - width) / 2))
                        y: 24
                        width: Math.min(configFlick.width - 56, 980)
                        spacing: 18

                        C.SettingsSection {
                            title: qsTr("Profiles")
                            description: qsTr("Complete sets of settings, for example one per person or per screen.")

                            C.SettingRow {
                                title: qsTr("Current profile")
                                description: Chiaki.settings.currentProfile ? Chiaki.settings.currentProfile : qsTr("default")

                                Item { Layout.fillWidth: true }
                                C.Button {
                                    id: profile
                                    firstInFocusChain: true
                                    text: qsTr("Manage profiles")
                                    onClicked: {
                                        root.showProfileDialog()
                                    }
                                    Material.roundedScale: Material.SmallScale
                                }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("Back up")

                            C.SettingRow {
                                title: qsTr("Settings file")
                                description: qsTr("Take your settings to another computer.")

                                Item { Layout.fillWidth: true }
                                C.Button {
                                    id: exportButton
                                    text: qsTr("Export")
                                    onClicked: {
                                        Chiaki.settings.exportSettings();
                                    }
                                    Material.roundedScale: Material.SmallScale
                                }
                                C.Button {
                                    id: importButton
                                    text: qsTr("Import")
                                    onClicked: {
                                        Chiaki.settings.importSettings();
                                    }
                                    Material.roundedScale: Material.SmallScale
                                }
                            }
                        }

                        C.SettingsSection {
                            title: qsTr("Logs and about")

                            C.SettingRow {
                                title: qsTr("Hide personal details in logs")
                                description: qsTr("Addresses and account IDs are left out, so logs can be shared.")
                                hint: qsTr("(On)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.logSanitize
                                    onToggled: Chiaki.settings.logSanitize = checked
                                }
                            }

                            C.SettingRow {
                                title: qsTr("Verbose logging")
                                description: qsTr("Much more detail, for finding problems.")
                                hint: qsTr("(Off)")

                                Item { Layout.fillWidth: true }
                                C.CheckBox {
                                    checked: Chiaki.settings.logVerbose
                                    onToggled: Chiaki.settings.logVerbose = checked
                                }
                            }

                            C.SettingRow {
                                title: qsTr("About %1").arg(Qt.application.name)
                                description: qsTr("Version %1 · powered by GamerNova.net · built on chiaki-ng").arg(Qt.application.version)

                                Item { Layout.fillWidth: true }
                                C.Button {
                                    id: aboutButton
                                    text: qsTr("About")
                                    onClicked: aboutDialog.open()
                                    Material.roundedScale: Material.SmallScale
                                    lastInFocusChain: true
                                }
                            }
                        }
                    }
                }
            }
        }

        Connections {
            target: dialog.Window.window
            function onActiveFocusItemChanged() {
                dialog.ensureActiveFocusVisible()
            }
        }

        Item {
            Timer {
                id: openTimer
                interval: 100
                running: false
                onTriggered: {
                    if(controllerMappingDialog.resetMapping)
                    {
                        if(!Chiaki.controllerMappingDefaultMapping)
                        {
                            quitControllerMapping = false;
                            Chiaki.controllerMappingReset();
                        }
                        controllerMappingDialog.close();
                    }
                    else
                    {
                        controllerMappingChange.forceActiveFocus(Qt.TabFocusReason);
                        root.showControllerMappingDialog();
                        quitControllerMapping = false;
                        controllerMappingDialog.resetFocus = false;
                        controllerMappingDialog.close();
                    }
                }
            }
        }

        Dialog {
            id: aboutDialog
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("About %1").arg(Qt.application.name)
            modal: true
            standardButtons: Dialog.Ok
            Material.roundedScale: Material.MediumScale
            onAboutToHide: aboutButton.forceActiveFocus(Qt.TabFocusReason)

            RowLayout {
                spacing: 50
                onVisibleChanged: if (visible) forceActiveFocus(Qt.TabFocusReason)
                Keys.onReturnPressed: aboutDialog.close()
                Keys.onEscapePressed: aboutDialog.close()

                Image {
                    Layout.preferredWidth: 200
                    fillMode: Image.PreserveAspectFit
                    verticalAlignment: Image.AlignTop
                    source: "qrc:icons/chiaking-logo.svg"
                }

                Label {
                    Layout.preferredWidth: 400
                    verticalAlignment: Text.AlignTop
                    wrapMode: Text.Wrap
                    text: "<h1>PSRemotePlayTogether</h1> version %1, powered by GamerNova.net
                        <h2>Built on chiaki-ng</h2> by Street Pea, version %2
                        <h2>Fork of Chiaki</h2> by Florian Markl at version 2.1.1

                        <p>This program is free software: you can redistribute it and/or modify
                        it under the terms of the GNU Affero General Public License version 3
                        as published by the Free Software Foundation.</p>

                        <p>This program is distributed in the hope that it will be useful,
                        but WITHOUT ANY WARRANTY; without even the implied warranty of
                        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
                        GNU General Public License for more details.</p>".arg(Qt.application.version).arg(Chiaki.chiakiVersion)
                }
            }
        }

        Dialog {
            id: keyDialog
            focus: false
            property int buttonValue
            property var buttonCallback
            property var keysIndex
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Key Capture")
            modal: true
            standardButtons: Dialog.Close
            closePolicy: Popup.CloseOnPressOutside
            onOpened: keyLabel.forceActiveFocus(Qt.TabFocusReason)
            onClosed: {
                let item = chiakiKeys.itemAt(keysIndex)
                if(item)
                {
                    let item2 = item.children[1];
                    if(item2)
                        item2.forceActiveFocus(Qt.TabFocusReason);
                }
                keyLabel.focus = false;
                focus = false;
            }
            Material.roundedScale: Material.MediumScale

            function show(opts) {
                buttonValue = opts.value;
                buttonCallback = opts.callback;
                keysIndex = opts.mappingIndex;
                open();
            }

            Label {
                id: keyLabel
                focus: true
                text: qsTr("Press any key to configure button or click close")
                Keys.onReleased: (event) => {
                    var name = Chiaki.settings.changeControllerKey(keyDialog.buttonValue, event.key);
                    keyDialog.buttonCallback(name);
                    keyDialog.close();
                }
            }
        }

        Dialog {
            id: controllerMappingDialog
            property bool resetFocus: true
            property bool resetMapping: false
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Controller Capture")
            modal: true
            standardButtons: Dialog.Close
            closePolicy: Popup.CloseOnPressOutside
            onOpened: {
                controllerLabel.forceActiveFocus(Qt.TabFocusReason);
                Chiaki.beginControllerMapping(resetMapping);
            }
            onClosed: {
                if(resetFocus)
                {
                    if(resetMapping)
                        controllerMappingReset.forceActiveFocus(Qt.TabFocusReason);
                    else
                        controllerMappingChange.forceActiveFocus(Qt.TabFocusReason);
                    focus = false;
                }
                else
                {
                    resetFocus = true;
                    focus = false;
                }
                if(quitControllerMapping)
                    Chiaki.controllerMappingQuit();
                else
                    quitControllerMapping = true;
            }
            Material.roundedScale: Material.MediumScale

            function show(opts) {
                resetMapping = opts.reset;
                open();
            }
            Label {
                id: controllerLabel
                text: qsTr("Choose the controller by pressing any button on the controller")
            }
        }

        Dialog {
            id: steamControllerMappingDialog
            property bool resetMapping: false
            parent: Overlay.overlay
            x: Math.round((root.width - width) / 2)
            y: Math.round((root.height - height) / 2)
            title: qsTr("Controller Managed by Steam")
            modal: true
            standardButtons: Dialog.Close
            closePolicy: Popup.NoAutoClose
            onOpened: {
                steamLabel.forceActiveFocus(Qt.TabFocusReason);
            }
            onClosed: {
                if(resetMapping)
                    controllerMappingReset.forceActiveFocus(Qt.TabFocusReason);
                else
                    controllerMappingChange.forceActiveFocus(Qt.TabFocusReason);
                focus = false;
            }
            Material.roundedScale: Material.MediumScale

            Label {
                id: steamLabel
                wrapMode: TextEdit.Wrap
                text: qsTr("This controller is managed by Steam.\nPlease use Steam to map controller or disable Steam Input for the controller before mapping here.")
                Keys.onReturnPressed: steamControllerMappingDialog.close();
                Keys.onEscapePressed: steamControllerMappingDialog.close();
            }
        }

        Connections {
            target: Chiaki

            function onControllerMappingInProgressChanged()
            {
                if(Chiaki.controllerMappingInProgress)
                    openTimer.start();
            }

            function onControllerMappingSteamControllerSelected()
            {
                controllerMappingDialog.resetFocus = false;
                quitControllerMapping = false;
                steamControllerMappingDialog.resetMapping = controllerMappingDialog.resetMapping;
                controllerMappingDialog.close();
                steamControllerMappingDialog.open();
            }
        }
    }
}
