import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Shapes

import org.streetpea.chiaking

import "controls" as C

// The start page: two doors (Host and Join), your consoles, and what to press while
// playing. Everything works with the mouse, the keyboard and a controller: the arrows
// and the D-pad go from one thing on the page to the next (up and down go from console
// to console), Cross presses, Triangle wakes, Square removes, L1 sets the console PIN,
// R1 refreshes PSN, L3 makes a Steam shortcut, R3 adds a console, Options opens the
// settings, Circle asks to quit.
Pane {
    padding: 0
    id: consolePane
    background: Item {
        Rectangle {
            anchors.fill: parent
            color: C.Theme.ground
        }
        Glow {
            anchors.fill: parent
            tint: Qt.rgba(140 / 255, 82 / 255, 1, 0.20)
            centerX: width * 0.1
            centerY: -height * 0.1
            radius: 640
        }
        Glow {
            anchors.fill: parent
            tint: Qt.rgba(240 / 255, 70 / 255, 155 / 255, 0.10)
            centerX: width
            centerY: height * 1.1
            radius: 570
        }
    }

    readonly property color accent: C.Theme.brandFrom
    readonly property color pink: C.Theme.brandTo
    readonly property color cyan: C.Theme.focus
    readonly property color green: C.Theme.good
    readonly property color amber: C.Theme.wait
    readonly property color red: C.Theme.bad
    /** Controller hints only make sense with a controller. */
    readonly property bool padConnected: Chiaki.controllers.length > 0
    /** Room for two console cards side by side. */
    readonly property bool wide: width >= 1100
    /** A narrow window: shorter texts, the doors without their descriptions. */
    readonly property bool narrow: width < 760
    /** Very narrow: the two doors one above the other. */
    readonly property bool doorsStacked: width < 600
    readonly property int sideMargin: narrow ? 18 : 32
    readonly property int shownConsoles: {
        let n = 0;
        for (let i = 0; i < Chiaki.hosts.length; ++i) {
            if (Chiaki.hosts[i].display)
                n++;
        }
        return n;
    }

    /** What had the keys when another page opened on top, to give them back to it. */
    property Item restoreFocusItem: null
    /** The last thing on this page that had the keys. */
    property Item lastFocusItem: null
    /** The keys were on a console card (put there by keys, not by the mouse). */
    property bool focusWasInConsoles: false
    /** Which of the card's buttons had them: 0 the main one, then PIN, then ⋯. */
    property int consoleFocusColumn: 0

    onShownConsolesChanged: Qt.callLater(consolePane.ensureCurrentConsole)

    StackView.onDeactivating: {
        const w = Window.window;
        const item = w ? w.activeFocusItem : null;
        // Given back only to what was reached with the keys or a controller; a click
        // leaves the page as it was, where Cross plays the selected console
        restoreFocusItem = item && item !== consolePane && consolePane.inPage(item) && item.visualFocus ? item : null;
    }

    StackView.onActivated: {
        const back = restoreFocusItem;
        restoreFocusItem = null;
        if (back && back.visible && back.enabled) {
            back.forceActiveFocus(Qt.TabFocusReason);
        } else {
            // The page itself, not whatever had the keys last (a pane passes them on)
            if (lastFocusItem)
                lastFocusItem.focus = false;
            forceActiveFocus(Qt.TabFocusReason);
        }
        // On a new installation the intro film comes first (Main.qml); the questions follow it
        if(!Chiaki.autoConnect && !root.initialAsk && !Chiaki.window.directStream && Chiaki.settings.introState !== "pending")
        {
            root.initialAsk = true;
            if(Chiaki.settings.addSteamShortcutAsk && (typeof Chiaki.createSteamShortcut === "function"))
                root.showRemindDialog(qsTr("Official Steam artwork + controller layout"), qsTr("Would you like to either create a new non-Steam game for PSRemotePlayTogether\nor update an existing non-Steam game with the official artwork and controller layout?") + "\n\n" + qsTr("(Note: If you select no now and want to do this later, click the button or press L3 from the main menu.)"), false, () => root.showSteamShortcutDialog(true));
            else if(Chiaki.settings.remotePlayAsk)
            {
                if(!Chiaki.settings.psnRefreshToken || !Chiaki.settings.psnAuthToken || !Chiaki.settings.psnAuthTokenExpiry || !Chiaki.settings.psnAccountId)
                    root.showRemindDialog(qsTr("Remote Play via PSN"), qsTr("Would you like to connect to PSN?\nThis enables:\n- Automatic registration\n- Playing outside of your home network without port forwarding?") + "\n\n" + qsTr("(If you say no now and want it later: Settings › Away from home (PSN).)"), true, () => root.showPSNTokenDialog(false));
                else
                    Chiaki.settings.remotePlayAsk = false;
            }
        }
    }
    Keys.onUpPressed: navigate(0, -1)
    Keys.onDownPressed: navigate(0, 1)
    Keys.onLeftPressed: navigate(-1, 0)
    Keys.onRightPressed: navigate(1, 0)
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
            root.showHostDialog();
            event.accepted = true;
            break;
        }
    }

    // ------------------------------------------------------------ keys and controller

    /** An arrow or the D-pad: to the next thing on the page in that direction. */
    function navigate(dx, dy) {
        if (moveFocus(dx, dy))
            return;
        // Nothing on the page has the keys yet: up and down pick a console, as they always did
        if (dy !== 0 && hostsView.currentItem && hostsView.currentItem.shown) {
            if (dy < 0) {
                hostsView.decrementCurrentIndex();
                while (!hostsView.currentItem.shown)
                    hostsView.decrementCurrentIndex();
            } else {
                hostsView.incrementCurrentIndex();
                while (!hostsView.currentItem.shown)
                    hostsView.incrementCurrentIndex();
            }
        }
        // and from there on the keys go from one thing to the next
        if (hostsView.currentItem && hostsView.currentItem.shown)
            hostsView.currentItem.focusAction(0);
        else
            hostButton.forceActiveFocus(Qt.TabFocusReason);
    }

    /** The page in rows, top to bottom: each console card is a row of its own. */
    function focusRows() {
        let rows = [
            { items: [introButton, introIconButton, profileButton, settingsButton, moreButton, quitButton] }
        ];
        if (doorsStacked) {
            rows.push({ items: [hostButton] });
            rows.push({ items: [joinButton, roomsButton] });
        } else {
            rows.push({ items: [hostButton, joinButton, roomsButton] });
        }
        rows.push({ items: [discoveryButton, addConsoleButton] });
        rows.push({ items: [emptyAddButton, emptySearchButton] });
        for (let i = 0; i < hostsRepeater.count; ++i) {
            const card = hostsRepeater.itemAt(i);
            if (card && card.shown)
                rows.push({ items: card.actions(), card: true });
        }
        rows.push({ items: [novaLink, supportButton, updateButton] });

        let out = [];
        for (let i = 0; i < rows.length; ++i) {
            const items = rows[i].items.filter(it => it && it.visible && it.enabled);
            if (items.length > 0)
                out.push({ items: items, card: !!rows[i].card });
        }
        return out;
    }

    /** Moves the keys from what has them now; false when nothing on the page has them. */
    function moveFocus(dx, dy) {
        const w = Window.window;
        const item = w ? w.activeFocusItem : null;
        if (!item)
            return false;
        const rows = focusRows();
        let r = -1;
        let c = -1;
        for (let i = 0; i < rows.length && r < 0; ++i) {
            const k = rows[i].items.indexOf(item);
            if (k >= 0) {
                r = i;
                c = k;
            }
        }
        if (r < 0)
            return false;
        if (dx !== 0) {
            const n = c + dx;
            if (n >= 0 && n < rows[r].items.length)
                rows[r].items[n].forceActiveFocus(Qt.TabFocusReason);
            return true;
        }
        const next = r + dy;
        if (next < 0 || next >= rows.length)
            return true;
        const target = rows[next];
        let pick = 0;
        if (target.card) {
            // From card to card the same button; into the cards, the main one
            pick = rows[r].card ? Math.min(c, target.items.length - 1) : 0;
        } else {
            // Whatever is nearest above or below
            const x = item.mapToItem(consolePane, item.width / 2, 0).x;
            let best = Infinity;
            for (let i = 0; i < target.items.length; ++i) {
                const t = target.items[i];
                const d = Math.abs(t.mapToItem(consolePane, t.width / 2, 0).x - x);
                if (d < best) {
                    best = d;
                    pick = i;
                }
            }
        }
        target.items[pick].forceActiveFocus(Qt.TabFocusReason);
        return true;
    }

    function inPage(item) {
        for (let p = item; p; p = p.parent) {
            if (p === consolePane)
                return true;
        }
        return false;
    }

    function cardOf(item) {
        for (let p = item; p; p = p.parent) {
            if (p.consoleCard === true)
                return p;
        }
        return null;
    }

    function focusMoved() {
        const w = Window.window;
        const item = w ? w.activeFocusItem : null;
        if (!item)
            return;
        if (item === consolePane) {
            // What had the keys went away, as when the consoles are listed anew: back to the console
            if (focusWasInConsoles)
                Qt.callLater(consolePane.refocusConsole);
            return;
        }
        if (!inPage(item)) {
            focusWasInConsoles = false;
            return;
        }
        lastFocusItem = item;
        const card = cardOf(item);
        focusWasInConsoles = !!card && item.visualFocus === true;
        if (card) {
            // The console whose button has the keys is the one Triangle, Square and L1 act on
            hostsView.currentIndex = card.cardIndex;
            consoleFocusColumn = Math.max(0, card.actions().filter(it => it.visible && it.enabled).indexOf(item));
        }
        reveal(card || item);
    }

    function refocusConsole() {
        const w = Window.window;
        if (!w || w.activeFocusItem !== consolePane || !consolePane.visible)
            return;
        ensureCurrentConsole();
        if (hostsView.currentItem && hostsView.currentItem.shown)
            hostsView.currentItem.focusAction(consoleFocusColumn);
    }

    /** Scrolls the page so that what has the keys is on screen. */
    function reveal(item) {
        let inside = false;
        for (let p = item; p; p = p.parent) {
            if (p === page.contentItem) {
                inside = true;
                break;
            }
        }
        if (!inside)
            return;
        const top = item.mapToItem(page.contentItem, 0, 0).y - 24;
        const bottom = top + item.height + 48;
        const max = Math.max(0, page.contentHeight - page.height);
        let y = page.contentY;
        if (top < page.contentY)
            y = Math.max(0, top);
        else if (bottom > page.contentY + page.height)
            y = Math.min(max, bottom - page.height);
        if (y === page.contentY)
            return;
        revealAnimation.stop();
        revealAnimation.to = y;
        revealAnimation.start();
    }

    /** A menu opened with the keys gives them back to its button as keys, ring and all. */
    function menuClosed(button, byKeys) {
        if (!byKeys)
            return;
        Qt.callLater(() => {
            const w = Window.window;
            if (w && w.activeFocusItem === button)
                button.focusReason = Qt.TabFocusReason;
        });
    }

    /** What the list always did when the consoles changed: a visible one is selected. */
    function ensureCurrentConsole() {
        if (!hostsView.currentItem)
            hostsView.incrementCurrentIndex();
        if (!hostsView.currentItem)
            return;
        if (!hostsView.currentItem.shown) {
            for (let i = 0; i < hostsView.count; i++) {
                hostsView.incrementCurrentIndex();
                if (hostsView.currentItem.shown)
                    break;
            }
        }
    }

    Connections {
        target: consolePane.Window.window
        function onActiveFocusItemChanged() {
            consolePane.focusMoved();
        }
    }

    NumberAnimation {
        id: revealAnimation
        target: page
        property: "contentY"
        duration: 160
        easing.type: Easing.OutCubic
    }

    // The selected console: the one Cross plays and Triangle, Square and L1 act on. Up and
    // down step through the consoles and go round at either end.
    QtObject {
        id: hostsView
        property int currentIndex: -1
        /** Counts up whenever cards come or go, so that currentItem follows. */
        property int revision: 0
        readonly property int count: hostsRepeater.count
        readonly property Item currentItem: revision >= 0 && currentIndex >= 0 && currentIndex < hostsRepeater.count ? hostsRepeater.itemAt(currentIndex) : null

        function incrementCurrentIndex() {
            if (count > 0)
                currentIndex = (currentIndex + 1) % count;
        }

        function decrementCurrentIndex() {
            if (count > 0)
                currentIndex = currentIndex <= 0 ? count - 1 : currentIndex - 1;
        }
    }

    // ------------------------------------------------------------ building blocks

    // A soft round light, as on GamerNova: a colour that fades out from a point
    component Glow: Shape {
        id: glow
        property color tint: "white"
        property real centerX: width / 2
        property real centerY: height / 2
        property real radius: Math.min(width, height) / 2
        /** Where along the radius it has faded out. */
        property real reach: 0.7
        /** Rounded corners, to stay inside a card. */
        property real cornerRadius: 0

        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: RadialGradient {
                centerX: glow.centerX
                centerY: glow.centerY
                centerRadius: glow.radius
                focalX: glow.centerX
                focalY: glow.centerY
                GradientStop { position: 0.0; color: glow.tint }
                GradientStop { position: glow.reach; color: Qt.rgba(glow.tint.r, glow.tint.g, glow.tint.b, 0) }
            }
            PathRectangle {
                x: 0
                y: 0
                width: glow.width
                height: glow.height
                radius: glow.cornerRadius
            }
        }
    }

    // A pill of the new look that knows the page: up and down are the page's, and while a
    // controller is connected the controller button for it is drawn in front of the text
    component PageButton: C.PillButton {
        id: pb
        property string hint: ""
        readonly property bool showHint: consolePane.padConnected && hint.length > 0
        readonly property int hintSize: small ? 20 : 24
        firstInFocusChain: true
        lastInFocusChain: true
        leftPadding: showHint ? hintSize + 4 : 0
        implicitWidth: Math.max(implicitHeight, implicitContentWidth + leftPadding + (small ? 26 : 40))

        Image {
            visible: pb.showHint
            x: pb.small ? 9 : 13
            anchors.verticalCenter: parent.verticalCenter
            width: pb.hintSize
            height: pb.hintSize
            sourceSize: Qt.size(pb.hintSize * 2, pb.hintSize * 2)
            source: pb.showHint ? pb.hint : ""
        }
    }

    component PageIconButton: C.IconButton {
        firstInFocusChain: true
        lastInFocusChain: true
    }

    // A small coloured pill: whether the search is on, Ko-fi, a newer version
    component TintPill: AbstractButton {
        id: tp
        property color tint: C.Theme.textDim
        property color ink: C.Theme.text
        property real edge: 0.55
        property string iconName
        property bool dot: false
        property bool pulse: false
        property string tip
        property bool firstInFocusChain: true
        property bool lastInFocusChain: true

        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        implicitHeight: 32
        implicitWidth: implicitContentWidth + 28
        font.pixelSize: 13
        font.weight: Font.Bold
        Accessible.name: text
        ToolTip.visible: hovered && tip.length > 0
        ToolTip.delay: 400
        ToolTip.text: tip

        Keys.onPressed: (event) => C.FocusChain.handle(tp, event)

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        background: Item {
            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: Qt.rgba(tp.tint.r, tp.tint.g, tp.tint.b, tp.down ? 0.28 : tp.hovered ? 0.18 : 0.09)
                border.width: 1
                border.color: Qt.rgba(tp.tint.r, tp.tint.g, tp.tint.b, tp.edge)
                Behavior on color { ColorAnimation { duration: 120 } }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: height / 2
                color: "transparent"
                border.width: 2
                border.color: C.Theme.focus
                visible: tp.visualFocus
            }
        }

        contentItem: Item {
            implicitWidth: tpRow.implicitWidth
            implicitHeight: tpRow.implicitHeight

            Row {
                id: tpRow
                anchors.centerIn: parent
                spacing: 8

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: tp.dot
                    width: 8
                    height: 8
                    radius: 4
                    color: tp.tint

                    SequentialAnimation on opacity {
                        running: tp.pulse
                        loops: Animation.Infinite
                        alwaysRunToEnd: true
                        NumberAnimation { to: 0.3; duration: 900 }
                        NumberAnimation { to: 1.0; duration: 900 }
                    }
                }

                C.Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: tp.iconName.length > 0
                    iconName: tp.iconName
                    size: 14
                    color: tp.ink
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: text.length > 0
                    text: tp.text
                    font: tp.font
                    color: tp.ink
                }
            }
        }
    }

    // A key to press, drawn like one
    component Keycap: Rectangle {
        property alias text: capLabel.text
        implicitWidth: Math.max(26, capLabel.implicitWidth + 16)
        implicitHeight: capLabel.implicitHeight + 10
        radius: 7
        color: "#39325e"

        Rectangle {
            anchors {
                fill: parent
                margins: 1
                bottomMargin: 3
            }
            radius: 6
            color: "#221d3e"
        }

        Text {
            id: capLabel
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -1
            font.pixelSize: 12
            font.weight: Font.Bold
            color: C.Theme.text
        }
    }

    // ------------------------------------------------------------ app bar

    Item {
        id: header
        anchors {
            top: parent.top
            left: parent.left
            right: parent.right
        }
        height: 76

        RowLayout {
            anchors {
                fill: parent
                leftMargin: consolePane.sideMargin
                rightMargin: consolePane.sideMargin
            }
            spacing: 10

            // The app's own icon: two linked rings over the controller (qrc:/icons/ui/app-icon.svg)
            Image {
                Layout.preferredWidth: 40
                Layout.preferredHeight: 40
                Layout.rightMargin: 2
                source: C.Theme.iconBase + "app-icon.svg"
                sourceSize: Qt.size(80, 80)
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 1

                C.GradientText {
                    text: "PSRemotePlayTogether"
                    font.pixelSize: 19
                    font.weight: Font.ExtraBold
                    from: "#a77bff"
                    to: C.Theme.brandTo
                }

                Label {
                    Layout.fillWidth: true
                    visible: !consolePane.narrow
                    elide: Text.ElideRight
                    font.pixelSize: 13
                    color: C.Theme.textDim
                    text: qsTr("Experience a new way of playing together online.")
                }
            }

            // The intro film again: what the app is for and how it works. Never opened on this
            // installation (one from before the film): a small light on the button says it is new.
            PageButton {
                id: introButton
                visible: !consolePane.narrow
                iconName: "play"
                text: qsTr("How it works")
                Accessible.description: qsTr("Plays the short intro film")
                onClicked: root.showIntro()

                Rectangle {
                    visible: Chiaki.settings.introState === ""
                    x: parent.width - 13
                    y: 3
                    width: 10
                    height: 10
                    radius: 5
                    color: C.Theme.focus
                    border.width: 2
                    border.color: C.Theme.ground
                }
            }

            PageIconButton {
                id: introIconButton
                visible: consolePane.narrow
                iconName: "play"
                tip: qsTr("How it works")
                onClicked: root.showIntro()
            }

            // Who you are in Play Together: opens the profile and the Play Together settings
            AbstractButton {
                id: profileButton
                property bool firstInFocusChain: true
                property bool lastInFocusChain: true
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                implicitHeight: C.Theme.touch
                implicitWidth: implicitContentWidth + leftPadding + rightPadding
                leftPadding: 6
                rightPadding: consolePane.narrow ? 6 : 16
                Accessible.name: qsTr("Profile & settings…")
                ToolTip.visible: hovered
                ToolTip.delay: 600
                ToolTip.text: qsTr("Profile & settings…")
                Keys.onPressed: (event) => C.FocusChain.handle(profileButton, event)
                onClicked: root.showSettingsDialog("profile")

                background: Item {
                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: Qt.rgba(1, 1, 1, profileButton.down ? 0.12 : profileButton.hovered ? 0.09 : 0.05)
                        border.width: 1
                        border.color: Qt.rgba(1, 1, 1, 0.09)
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -4
                        radius: height / 2
                        color: "transparent"
                        border.width: 2
                        border.color: C.Theme.focus
                        visible: profileButton.visualFocus
                    }
                }

                contentItem: RowLayout {
                    spacing: 10

                    C.Avatar {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        source: Chiaki.settings.coplayAvatarUrl
                        name: Chiaki.settings.coplayName || "?"
                    }

                    ColumnLayout {
                        visible: !consolePane.narrow
                        spacing: 0

                        Label {
                            font.pixelSize: 11
                            color: C.Theme.textDim
                            text: Chiaki.settings.coplayName ? qsTr("You play as") : qsTr("Play Together")
                        }

                        Label {
                            Layout.maximumWidth: 200
                            elide: Text.ElideRight
                            font.pixelSize: 14
                            font.bold: true
                            color: C.Theme.text
                            text: Chiaki.settings.coplayName || qsTr("Set your name and picture")
                        }
                    }
                }
            }

            PageIconButton {
                id: settingsButton
                iconName: "sliders"
                tip: qsTr("Settings")
                onClicked: root.showSettingsDialog()
            }

            PageIconButton {
                id: moreButton
                iconName: "more"
                tip: qsTr("More")
                visible: Chiaki.settings.psnAuthToken || typeof Chiaki.createSteamShortcut === "function"
                onClicked: {
                    moreMenu.byKeys = moreButton.visualFocus;
                    moreMenu.popup(moreButton, 0, moreButton.height + 6);
                }

                C.Menu {
                    id: moreMenu
                    property bool byKeys: false
                    font.pixelSize: 15
                    onClosed: consolePane.menuClosed(moreButton, byKeys)

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

            PageIconButton {
                id: quitButton
                iconName: "close"
                tip: qsTr("Quit")
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
            color: Qt.rgba(1, 1, 1, 0.06)
        }
    }

    // ------------------------------------------------------------ the page

    Flickable {
        id: page
        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
            bottom: footer.top
        }
        contentWidth: width
        contentHeight: pageColumn.implicitHeight + pageColumn.y + 20
        clip: true
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: page.contentHeight > page.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        ColumnLayout {
            id: pageColumn
            width: Math.min(1336, page.width - 2 * consolePane.sideMargin)
            x: Math.round((page.width - width) / 2)
            y: consolePane.narrow ? 18 : 28
            spacing: consolePane.narrow ? 20 : 30

            // -------------------------------------------------------- the two doors

            // Two ways to play together, each with a page of its own: everything about
            // it is there, and nowhere else
            GridLayout {
                Layout.fillWidth: true
                columns: consolePane.doorsStacked ? 1 : 2
                columnSpacing: 20
                rowSpacing: 14

                // Host: the gradient door
                Rectangle {
                    id: hostDoor
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 400
                    Layout.minimumHeight: consolePane.narrow ? 0 : 196
                    implicitHeight: hostColumn.implicitHeight + (consolePane.narrow ? 36 : 52)
                    radius: 24
                    clip: true
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#6a32e8" }
                        GradientStop { position: 0.45; color: "#8a36d6" }
                        GradientStop { position: 1.0; color: "#c02f7d" }
                    }

                    // Two soft circles, placed so that they stay clear of the round corners
                    // (the door only clips square): the top right corner falls into the
                    // ring's hole, and on a door this big the rest stays away from the others
                    Rectangle {
                        visible: hostDoor.width >= 340 && hostDoor.height >= 196
                        x: hostDoor.width - 80 - width / 2
                        y: 60 - height / 2
                        width: 280
                        height: 280
                        radius: width / 2
                        color: "transparent"
                        border.width: 34
                        border.color: Qt.rgba(1, 1, 1, 0.07)
                    }

                    Rectangle {
                        visible: hostDoor.width >= 340 && hostDoor.height >= 196
                        x: hostDoor.width - 270
                        y: hostDoor.height - 90
                        width: 200
                        height: 200
                        radius: 100
                        color: Qt.rgba(1, 1, 1, 0.06)
                    }

                    ColumnLayout {
                        id: hostColumn
                        anchors {
                            fill: parent
                            leftMargin: consolePane.narrow ? 20 : 28
                            rightMargin: consolePane.narrow ? 20 : 28
                            topMargin: consolePane.narrow ? 18 : 26
                            bottomMargin: consolePane.narrow ? 18 : 26
                        }
                        spacing: 14

                        RowLayout {
                            spacing: 14

                            Rectangle {
                                Layout.preferredWidth: 48
                                Layout.preferredHeight: 48
                                radius: 14
                                color: Qt.rgba(1, 1, 1, 0.16)

                                C.Icon {
                                    anchors.centerIn: parent
                                    iconName: "broadcast"
                                    size: 26
                                    color: "#ffffff"
                                }
                            }

                            Label {
                                font.pixelSize: 30
                                font.weight: Font.ExtraBold
                                color: "#ffffff"
                                text: qsTr("Host")
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.maximumWidth: 420
                            visible: !consolePane.narrow
                            wrapMode: Text.WordWrap
                            lineHeight: 1.15
                            font.pixelSize: 15
                            color: Qt.rgba(1, 1, 1, 0.88)
                            text: qsTr("Share your console. Privately with friends who have your invite, or as a public room anyone can find.")
                        }

                        Item {
                            Layout.fillHeight: true
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 12

                            PageButton {
                                id: hostButton
                                kind: "light"
                                text: qsTr("Host…")
                                iconName: "forward"
                                iconAfter: true
                                onClicked: root.showHostDialog()
                            }

                            Label {
                                visible: !consolePane.narrow
                                height: hostButton.height
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 13
                                color: Qt.rgba(1, 1, 1, 0.86)
                                text: qsTr("Console, room and invites on one page")
                            }
                        }
                    }
                }

                // Join: the dark door
                Rectangle {
                    id: joinDoor
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 400
                    Layout.minimumHeight: consolePane.narrow ? 0 : 196
                    implicitHeight: joinColumn.implicitHeight + (consolePane.narrow ? 36 : 52)
                    radius: 24
                    color: "#17132e"

                    Glow {
                        anchors.fill: parent
                        cornerRadius: joinDoor.radius
                        tint: Qt.rgba(124 / 255, 134 / 255, 1, 0.22)
                        centerX: width - 50
                        centerY: 50
                        radius: 198
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: joinDoor.radius
                        color: "transparent"
                        border.width: 1
                        border.color: Qt.rgba(124 / 255, 134 / 255, 1, 0.30)
                    }

                    ColumnLayout {
                        id: joinColumn
                        anchors {
                            fill: parent
                            leftMargin: consolePane.narrow ? 20 : 28
                            rightMargin: consolePane.narrow ? 20 : 28
                            topMargin: consolePane.narrow ? 18 : 26
                            bottomMargin: consolePane.narrow ? 18 : 26
                        }
                        spacing: 14

                        RowLayout {
                            spacing: 14

                            Rectangle {
                                Layout.preferredWidth: 48
                                Layout.preferredHeight: 48
                                radius: 14
                                color: Qt.rgba(124 / 255, 134 / 255, 1, 0.16)

                                C.Icon {
                                    anchors.centerIn: parent
                                    iconName: "enter"
                                    size: 26
                                    color: C.Theme.headingSoft
                                }
                            }

                            Label {
                                font.pixelSize: 30
                                font.weight: Font.ExtraBold
                                color: C.Theme.text
                                text: qsTr("Join")
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.maximumWidth: 420
                            visible: !consolePane.narrow
                            wrapMode: Text.WordWrap
                            lineHeight: 1.15
                            font.pixelSize: 15
                            color: C.Theme.textSoft
                            text: qsTr("Got an invite from a friend? Paste it and you are in. No console of your own needed.")
                        }

                        Item {
                            Layout.fillHeight: true
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 12

                            PageButton {
                                id: joinButton
                                kind: "primary"
                                text: qsTr("Join…")
                                iconName: "forward"
                                iconAfter: true
                                onClicked: root.showJoinDialog(0)
                            }

                            // A look at the public rooms, for whoever wants to play with somebody right now
                            PageButton {
                                id: roomsButton
                                visible: !!Chiaki.settings.coplayRoomsOn
                                text: qsTr("Browse public rooms")
                                iconName: "globe"
                                onClicked: root.showJoinDialog(1)
                            }
                        }
                    }
                }
            }

            // -------------------------------------------------------- your consoles

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Label {
                        font.pixelSize: 20
                        font.weight: Font.ExtraBold
                        color: C.Theme.text
                        text: qsTr("Your consoles")
                    }

                    C.Badge {
                        visible: consolePane.shownConsoles > 0
                        text: consolePane.shownConsoles
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    // Looking for consoles on this network: on or off
                    TintPill {
                        id: discoveryButton
                        implicitHeight: 36
                        dot: true
                        pulse: Chiaki.discoveryEnabled
                        tint: Chiaki.discoveryEnabled ? C.Theme.good : C.Theme.textDim
                        edge: Chiaki.discoveryEnabled ? 0.45 : 0.3
                        ink: Chiaki.discoveryEnabled ? "#d7fbe9" : C.Theme.textSoft
                        text: Chiaki.discoveryEnabled ? qsTr("Searching this network") : qsTr("Search is off")
                        onClicked: Chiaki.discoveryEnabled = !Chiaki.discoveryEnabled
                    }

                    PageButton {
                        id: addConsoleButton
                        small: true
                        iconName: "plus"
                        hint: "qrc:/icons/r3.svg"
                        text: consolePane.narrow ? "" : qsTr("Add console")
                        Accessible.name: qsTr("Add console")
                        ToolTip.visible: hovered && text.length === 0
                        ToolTip.text: qsTr("Add console")
                        onClicked: root.showManualHostDialog()
                    }
                }

                // macOS (Local Network privacy) or a firewall lets the search through but not a packet
                // straight to the console, which waking and playing need (DiscoveryManager)
                Rectangle {
                    Layout.fillWidth: true
                    visible: Chiaki.localNetworkBlocked
                    implicitHeight: blockedRow.implicitHeight + 32
                    radius: 18
                    color: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.10)
                    border.width: 1
                    border.color: Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.45)

                    RowLayout {
                        id: blockedRow
                        anchors {
                            left: parent.left
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                            leftMargin: 18
                            rightMargin: 16
                        }
                        spacing: 14

                        C.Icon {
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 2
                            iconName: "shield"
                            size: 22
                            color: C.Theme.wait
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 15
                                font.weight: Font.ExtraBold
                                color: "#fde4a8"
                                text: qsTr("Your Mac keeps this app away from your console")
                            }

                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 13
                                lineHeight: 1.15
                                color: "#e3d6b4"
                                text: qsTr("Waking and playing need it. Switch on PSRemotePlayTogether under Local Network in the macOS settings (if it is on, switch it off and on: macOS asks again after every update), allow it in a firewall such as LuLu, then start the app again.")
                            }
                        }

                        PageButton {
                            Layout.alignment: Qt.AlignVCenter
                            small: true
                            iconName: "sliders"
                            text: qsTr("Open Local Network")
                            onClicked: Chiaki.openLocalNetworkSettings()
                        }
                    }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: consolePane.wide && consolePane.shownConsoles > 1 ? 2 : 1
                    columnSpacing: 20
                    rowSpacing: 16

                    Repeater {
                        id: hostsRepeater
                        model: Chiaki.hosts
                        onItemAdded: {
                            hostsView.revision++;
                            Qt.callLater(consolePane.ensureCurrentConsole);
                        }
                        onItemRemoved: {
                            hostsView.revision++;
                            Qt.callLater(consolePane.ensureCurrentConsole);
                        }

                        delegate: Item {
                            id: delegate
                            readonly property bool consoleCard: true
                            readonly property int cardIndex: index
                            readonly property bool shown: !!modelData.display
                            readonly property bool highlighted: index === hostsView.currentIndex
                            readonly property bool on: modelData.state === "ready"
                            readonly property bool resting: modelData.state === "standby"
                            readonly property bool psn: !!modelData.duid
                            readonly property bool canWake: !!modelData.registered && !psn && !modelData.discovered
                            readonly property bool canRemove: !!modelData.manual || (!!modelData.discovered && !modelData.registered)
                            readonly property bool compact: width < 520
                            visible: shown
                            Layout.fillWidth: true
                            Layout.preferredWidth: 400
                            implicitHeight: cardRow.implicitHeight + 36

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
                                    root.showConfirmDialog(qsTr("Hide Console"), qsTr("Are you sure you want to hide this console?") + "\n\n" + qsTr("You can show it again under Settings › Your consoles › Hidden from the start page."), () => Chiaki.hideHost(modelData.mac, modelData.name));
                            }

                            function setConsolePin() {
                                root.showConsolePinDialog(index);
                            }

                            /** The card's buttons, left to right, for the arrows and the D-pad. */
                            function actions() {
                                return [mainButton, pinButton, menuButton];
                            }

                            function focusAction(column) {
                                const list = actions().filter(it => it.visible && it.enabled);
                                if (list.length > 0)
                                    list[Math.max(0, Math.min(column, list.length - 1))].forceActiveFocus(Qt.TabFocusReason);
                            }

                            // The selected card glows
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -4
                                radius: 26
                                color: "transparent"
                                border.width: 4
                                border.color: Qt.rgba(140 / 255, 82 / 255, 1, 0.12)
                                visible: delegate.highlighted
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: 22
                                color: delegate.highlighted ? C.Theme.cardHigh : cardHover.hovered ? C.Theme.raised : C.Theme.card
                                border.width: delegate.highlighted ? 2 : 1
                                border.color: delegate.highlighted ? C.Theme.brandFrom : C.Theme.line
                                Behavior on color { ColorAnimation { duration: 150 } }
                            }

                            HoverHandler {
                                id: cardHover
                            }

                            // A click anywhere on the card plays, as before
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: delegate.connectToHost()
                            }

                            RowLayout {
                                id: cardRow
                                anchors {
                                    fill: parent
                                    leftMargin: 20
                                    rightMargin: 20
                                    topMargin: 18
                                    bottomMargin: 18
                                }
                                spacing: delegate.compact ? 14 : 18

                                // The console, on a soft light: purple when on, amber at rest
                                Item {
                                    Layout.preferredWidth: delegate.compact ? 80 : 104
                                    Layout.preferredHeight: delegate.compact ? 100 : 124
                                    Layout.alignment: Qt.AlignVCenter

                                    Glow {
                                        anchors.fill: parent
                                        centerY: height * 0.55
                                        radius: Math.sqrt(width * width / 4 + centerY * centerY)
                                        reach: 0.68
                                        tint: delegate.on ? Qt.rgba(140 / 255, 82 / 255, 1, 0.45)
                                            : delegate.resting ? Qt.rgba(245 / 255, 185 / 255, 66 / 255, 0.16)
                                            : Qt.rgba(1, 1, 1, 0.06)
                                    }

                                    Image {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        fillMode: Image.PreserveAspectFit
                                        source: "image://svg/console-ps" + (modelData.ps5 ? "5" : "4") + (modelData.state == "standby" ? "#light_standby" : "#light_on")
                                        sourceSize: Qt.size(width, height)
                                    }
                                }

                                // Name, state at a glance, and how it is reached
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    spacing: 8

                                    Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.pixelSize: 20
                                        font.weight: Font.ExtraBold
                                        color: C.Theme.text
                                        text: modelData.name || (modelData.ps5 ? qsTr("PlayStation 5") : qsTr("PlayStation 4"))
                                    }

                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 6

                                        // On, asleep, or reached through PSN
                                        C.Badge {
                                            visible: !delegate.psn
                                            dot: true
                                            tone: delegate.on ? "good" : (delegate.resting ? "wait" : "neutral")
                                            text: delegate.on ? qsTr("On") : (delegate.resting ? qsTr("Rest mode") : (modelData.state || qsTr("Unknown")))
                                        }

                                        C.Badge {
                                            visible: delegate.psn
                                            dot: true
                                            tone: "play"
                                            text: modelData.discovered ? qsTr("Automatic registration available") : qsTr("Through PSN")
                                        }

                                        // The game it is running
                                        C.Badge {
                                            visible: !delegate.psn && !!modelData.app
                                            text: modelData.app || ""
                                        }

                                        C.Badge {
                                            visible: !delegate.psn
                                            dot: true
                                            tone: modelData.registered ? "brand" : "bad"
                                            text: modelData.registered ? qsTr("Registered") : qsTr("Not registered yet")
                                        }
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.pixelSize: 13
                                        color: C.Theme.textDim
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

                                // What to do with it: one big main action, the rest small
                                ColumnLayout {
                                    Layout.alignment: Qt.AlignVCenter
                                    spacing: 8

                                    PageButton {
                                        id: mainButton
                                        Layout.alignment: Qt.AlignRight
                                        Layout.minimumWidth: delegate.compact ? 0 : 150
                                        hint: delegate.highlighted ? root.controllerButton("cross") : ""
                                        kind: {
                                            if (delegate.psn)
                                                return modelData.discovered ? "primary" : "light";
                                            if (!modelData.registered)
                                                return "primary";
                                            return delegate.resting ? "ghost" : "light";
                                        }
                                        iconName: {
                                            if (showHint)
                                                return "";
                                            if (delegate.psn)
                                                return modelData.discovered ? "" : "globe";
                                            if (!modelData.registered)
                                                return "";
                                            return delegate.resting ? "power" : "play";
                                        }
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
                                            id: pinButton
                                            visible: !!modelData.registered
                                            small: true
                                            hint: delegate.highlighted ? "qrc:/icons/l1.svg" : ""
                                            text: qsTr("PIN")
                                            onClicked: delegate.setConsolePin()
                                        }

                                        // Wake, and delete or hide
                                        PageIconButton {
                                            id: menuButton
                                            visible: delegate.canWake || delegate.canRemove
                                            small: true
                                            round: true
                                            iconName: "more"
                                            tip: qsTr("More")
                                            onClicked: {
                                                cardMenu.byKeys = menuButton.visualFocus;
                                                cardMenu.popup(menuButton, 0, menuButton.height + 6);
                                            }

                                            C.Menu {
                                                id: cardMenu
                                                property bool byKeys: false
                                                font.pixelSize: 15
                                                onClosed: consolePane.menuClosed(menuButton, byKeys)

                                                MenuItem {
                                                    visible: delegate.canWake
                                                    height: visible ? implicitHeight : 0
                                                    text: qsTr("Wake")
                                                    icon.source: consolePane.padConnected ? root.controllerButton("pyramid") : ""
                                                    icon.color: "transparent"
                                                    onTriggered: delegate.wakeUpHost()
                                                }
                                                MenuItem {
                                                    visible: delegate.canRemove
                                                    height: visible ? implicitHeight : 0
                                                    text: modelData.manual ? qsTr("Delete") : qsTr("Hide")
                                                    icon.source: consolePane.padConnected ? root.controllerButton("box") : ""
                                                    icon.color: "transparent"
                                                    onTriggered: delegate.deleteHost()
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Shown while there is no console in the list yet
                C.Card {
                    Layout.fillWidth: true
                    visible: consolePane.shownConsoles === 0
                    implicitHeight: emptyColumn.implicitHeight + 48

                    ColumnLayout {
                        id: emptyColumn
                        anchors {
                            left: parent.left
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                            leftMargin: 24
                            rightMargin: 24
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
                                font.weight: Font.ExtraBold
                                color: C.Theme.text
                                text: Chiaki.discoveryEnabled ? qsTr("Looking for your PlayStation…") : qsTr("No consoles yet")
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 14
                            color: C.Theme.textSoft
                            text: qsTr("Switch your PS5 or PS4 on (rest mode is fine) and make sure it is on the same network as this computer. It shows up here by itself. Somewhere else? Add it by its address.")
                        }

                        Flow {
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            spacing: 10

                            PageButton {
                                id: emptyAddButton
                                kind: "primary"
                                iconName: "plus"
                                text: qsTr("Add a console by address")
                                hint: "qrc:/icons/r3.svg"
                                onClicked: root.showManualHostDialog()
                            }

                            PageButton {
                                id: emptySearchButton
                                visible: !Chiaki.discoveryEnabled
                                iconName: "search"
                                text: qsTr("Search this network")
                                onClicked: Chiaki.discoveryEnabled = true
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                            wrapMode: Text.WordWrap
                            font.pixelSize: 13
                            color: C.Theme.textDim
                            text: qsTr("No console of your own? Join a friend's with Join, above.")
                        }
                    }
                }
            }

            // -------------------------------------------------------- while playing

            // The keys worth knowing during a game: the ones the stream window really has
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: playingFlow.implicitHeight + 24
                radius: 16
                color: Qt.rgba(1, 1, 1, 0.03)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.09)

                Flow {
                    id: playingFlow
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: 18
                        rightMargin: 18
                    }
                    spacing: 18

                    Label {
                        height: 30
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 13
                        font.weight: Font.ExtraBold
                        color: C.Theme.textSoft
                        text: qsTr("While playing")
                    }

                    Row {
                        height: 30
                        spacing: 8

                        Keycap {
                            anchors.verticalCenter: parent.verticalCenter
                            text: consolePane.padConnected ? Chiaki.settings.stringForStreamMenuShortcut() : (Qt.platform.os === "osx" ? "⌘O" : "Ctrl+O")
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 13
                            color: C.Theme.textSoft
                            text: qsTr("Menu")
                        }
                    }

                    Row {
                        height: 30
                        spacing: 8

                        Keycap {
                            anchors.verticalCenter: parent.verticalCenter
                            text: Qt.platform.os === "osx" ? "⌘T" : "Ctrl+T"
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 13
                            color: C.Theme.textSoft
                            text: qsTr("Chat")
                        }
                    }

                    Row {
                        height: 30
                        spacing: 8

                        Keycap {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "F11"
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: 13
                            color: C.Theme.textSoft
                            text: qsTr("Full screen")
                        }
                    }

                    // Only while double-clicking is switched on in the settings
                    Label {
                        visible: !!Chiaki.settings.fullscreenDoubleClick
                        height: 30
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 13
                        color: C.Theme.textDim
                        text: qsTr("or double-click the picture")
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
        height: 56

        RowLayout {
            anchors {
                fill: parent
                leftMargin: consolePane.sideMargin
                rightMargin: consolePane.sideMargin
                bottomMargin: 4
            }
            spacing: 16

            // Two sides of the same width, so that Ko-fi stays in the middle
            Item {
                Layout.fillWidth: true
                Layout.preferredWidth: Math.max(novaLink.implicitWidth, footerEnd.implicitWidth)
                Layout.fillHeight: true

                AbstractButton {
                    id: novaLink
                    property bool firstInFocusChain: true
                    property bool lastInFocusChain: true
                    anchors {
                        left: parent.left
                        verticalCenter: parent.verticalCenter
                    }
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    implicitWidth: implicitContentWidth + 8
                    implicitHeight: 32
                    leftPadding: 4
                    rightPadding: 4
                    Accessible.name: "GamerNova.net"
                    Keys.onPressed: (event) => C.FocusChain.handle(novaLink, event)
                    onClicked: Qt.openUrlExternally("https://gamernova.net/")

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    background: Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        radius: 10
                        color: "transparent"
                        border.width: 2
                        border.color: C.Theme.focus
                        visible: novaLink.visualFocus
                    }

                    contentItem: Label {
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.StyledText
                        font.pixelSize: 13
                        color: C.Theme.textDim
                        linkColor: novaLink.hovered ? "#ffffff" : C.Theme.headingSoft
                        text: qsTr("powered by %1").arg("<a href=\"https://gamernova.net/\">GamerNova.net</a>")
                    }
                }
            }

            // A coffee for the developer: the same Ko-fi page as in the README and the release notes
            TintPill {
                id: supportButton
                tint: "#ff5e5b"
                ink: "#ffb3b1"
                edge: 0.6
                iconName: "heart"
                // A very narrow window: only the heart, the words stay in the tooltip
                text: consolePane.width < 640 ? "" : qsTr("Support the project")
                Accessible.name: qsTr("Support the project")
                tip: qsTr("Buy me a coffee on Ko-fi (opens in your browser)")
                onClicked: Qt.openUrlExternally("https://ko-fi.com/crypto90")
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredWidth: Math.max(novaLink.implicitWidth, footerEnd.implicitWidth)
                Layout.fillHeight: true

                Row {
                    id: footerEnd
                    anchors {
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: 12

                    // A newer release exists: stays here for as long as that is so, and opens the download page
                    TintPill {
                        id: updateButton
                        anchors.verticalCenter: parent.verticalCenter
                        visible: Chiaki.updateVersion.length > 0
                        implicitHeight: 30
                        tint: C.Theme.focus
                        ink: "#8ef6ff"
                        text: qsTr("Update available: %1").arg(Chiaki.updateVersion)
                        tip: qsTr("Open the download page in your browser")
                        onClicked: Chiaki.openUpdatePage()
                    }

                    Label {
                        id: versionLabel
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !consolePane.narrow || !updateButton.visible
                        font.pixelSize: 13
                        color: C.Theme.textDim
                        text: qsTr("Version %1").arg(Qt.application.version)
                    }
                }
            }
        }
    }
}
