import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Shapes

import org.streetpea.chiaking

import "controls" as C

// Settings: one window for all of them, the app's and Play Together's. A sidebar on
// the left with your profile on top and the pages in four groups; the page on the
// right; a search field in the title bar that finds any setting on any page.
// Everything is saved as you go.
//
// Each page is a file of its own in settings/ (a C.SettingsPage). All pages are made
// when the window opens, so that the search can look through them; the ones not shown
// are see-through and switched off.
//
// Keys and controller: the sidebar is one stop in the focus chain. Up and Down there
// go through the pages, Right or Return goes into the page; Up from the top of a page
// comes back. Page Up / Page Down (L1 / R1) change the page from anywhere, ⌘F / Ctrl+F
// searches, Escape goes back.
DialogView {
    id: dialog
    title: qsTr("Settings")
    buttonVisible: false

    /** The page shown, by its key below. Main.qml's showSettingsDialog(page) sets it. */
    property string page: "general"
    /** A setting to show once the window is open: its row's title. */
    property string showRow: ""
    /** What is typed into the search field, trimmed. */
    property string search: ""

    readonly property var pages: [
        { key: "profile", group: "", title: qsTr("Your profile"), icon: "", file: "ProfilePage.qml" },
        { key: "general", group: qsTr("Playing"), title: qsTr("General"), icon: "sliders", file: "GeneralPage.qml" },
        { key: "picture", group: qsTr("Playing"), title: qsTr("Picture"), icon: "monitor", file: "PicturePage.qml" },
        { key: "quality", group: qsTr("Playing"), title: qsTr("Stream quality"), icon: "gauge", file: "QualityPage.qml" },
        { key: "sound", group: qsTr("Playing"), title: qsTr("Sound & Wi-Fi"), icon: "speaker", file: "SoundPage.qml" },
        { key: "controllers", group: qsTr("Playing"), title: qsTr("Controllers"), icon: "gamepad", file: "ControllersPage.qml" },
        { key: "keyboard", group: qsTr("Playing"), title: qsTr("Keyboard & mouse"), icon: "keyboard", file: "KeyboardPage.qml" },
        { key: "voice", group: qsTr("Play Together"), title: qsTr("Voice & webcam"), icon: "mic", file: "VoicePage.qml" },
        { key: "overlays", group: qsTr("Play Together"), title: qsTr("Overlays & OBS"), icon: "layers", file: "OverlaysPage.qml" },
        { key: "live", group: qsTr("Play Together"), title: qsTr("Live streaming"), icon: "broadcast", file: "StreamingPage.qml" },
        { key: "scenes", group: qsTr("Play Together"), title: qsTr("Stream scenes"), icon: "monitor", file: "ScenesPage.qml" },
        { key: "twitch", group: qsTr("Play Together"), title: qsTr("Twitch"), icon: "people", file: "TwitchPage.qml" },
        { key: "alerts", group: qsTr("Play Together"), title: qsTr("Alerts"), icon: "heart", file: "AlertsPage.qml" },
        { key: "vieweractions", group: qsTr("Play Together"), title: qsTr("Viewer actions"), icon: "gamepad", file: "ViewerActionsPage.qml" },
        { key: "twitch_elements", group: qsTr("Play Together"), title: qsTr("Twitch elements"), icon: "tv", file: "TwitchElementsPage.qml" },
        { key: "network", group: qsTr("Play Together"), title: qsTr("Network & invites"), icon: "globe", file: "NetworkPage.qml" },
        { key: "consoles", group: qsTr("Consoles"), title: qsTr("Your consoles"), icon: "console", file: "ConsolesPage.qml" },
        { key: "psn", group: qsTr("Consoles"), title: qsTr("Away from home (PSN)"), icon: "cloud", file: "PsnPage.qml" },
        { key: "sets", group: qsTr("App"), title: qsTr("Setting sets"), icon: "copies", file: "SetsPage.qml" },
        { key: "updates", group: qsTr("App"), title: qsTr("Updates & logs"), icon: "info", file: "UpdatesPage.qml" }
    ]
    readonly property int pageIndex: Math.max(0, pages.findIndex((p) => p.key === page))
    readonly property string playerName: (Chiaki.settings.coplayName || "").trim()

    function showPage(index) {
        if (index >= 0 && index < pages.length)
            page = pages[index].key;
    }

    /** For a link on one page to another: opens the page by its key and, when given,
     *  scrolls to and lights up the row with that title. `byKeys`: the focus goes onto
     *  that row's control (or into the page), as after Return on a link. */
    function openPage(key, rowTitle, byKeys) {
        const index = pages.findIndex((p) => p.key === key);
        if (index < 0)
            return;
        // Off the page that is about to be switched off first, or the focus is stranded there
        side.forceActiveFocus(byKeys ? Qt.TabFocusReason : Qt.MouseFocusReason);
        searchField.text = "";
        showPage(index);
        Qt.callLater(() => {
            const row = rowTitle ? dialog.rowByTitle(rowTitle) : null;
            if (row)
                dialog.revealRow(row, byKeys);
            else if (byKeys)
                dialog.enterPage();
        });
    }

    /** The scrolling area of the page shown. */
    function pageFlick() {
        // Worked out here: in onPageChanged, pageIndex may not have followed yet
        return pageRepeater.itemAt(Math.max(0, pages.findIndex((p) => p.key === page)));
    }

    function inside(item, ancestor) {
        for (let p = item; p; p = p.parent) {
            if (p === ancestor)
                return true;
        }
        return false;
    }

    /** Into the page shown: its first control gets the focus. */
    function enterPage() {
        const flick = pageFlick();
        if (!flick)
            return;
        const first = flick.contentItem.nextItemInFocusChain(true);
        if (first && inside(first, flick))
            first.forceActiveFocus(Qt.TabFocusReason);
    }

    /** The row (or the item) that should be on screen when `item` has the focus. */
    function rowOf(item, flick) {
        for (let p = item; p && p !== flick.contentItem; p = p.parent) {
            if (p.isSettingRow === true)
                return p;
        }
        return item;
    }

    function scrollTo(item, flick, margin) {
        const top = item.mapToItem(flick.contentItem, 0, 0).y;
        const bottom = top + item.height;
        const max = Math.max(0, flick.contentHeight - flick.height);
        if (top - margin < flick.contentY)
            flick.contentY = Math.max(0, Math.min(max, top - margin));
        else if (bottom + margin > flick.contentY + flick.height)
            flick.contentY = Math.max(0, Math.min(max, bottom + margin - flick.height));
    }

    /** Scrolls to a row of the page shown and lights it up; with `focusIt` its first control gets the focus. */
    function revealRow(row, focusIt) {
        const flick = pageFlick();
        if (!flick || !row || !inside(row, flick))
            return;
        revealing = row;
        placeReveal();
        // A page just shown may still be laying itself out: placed once more a moment later
        revealSettle.restart();
        if (focusIt) {
            const first = row.nextItemInFocusChain(true);
            if (first && inside(first, row))
                first.forceActiveFocus(Qt.TabFocusReason);
            else
                side.forceActiveFocus(Qt.TabFocusReason);
        }
    }

    /** The row revealRow() shows. */
    property Item revealing: null

    function placeReveal() {
        const flick = pageFlick();
        const row = revealing;
        if (!flick || !row || !inside(row, flick))
            return;
        const top = row.mapToItem(flick.contentItem, 0, 0).y;
        const max = Math.max(0, flick.contentHeight - flick.height);
        flick.contentY = Math.max(0, Math.min(max, top - Math.round(flick.height / 4)));
        flash.show(row, flick);
    }

    /** Every row of every page, for the search: what it is called and where it is. */
    function collectRows(item, out, info, group) {
        if (!item || item.visible === false)
            return;
        if (item.isSettingsGroup === true && item.title)
            group = item.title;
        if (item.isSettingRow === true && item.title) {
            out.push({
                row: item,
                pageIndex: info.index,
                pageTitle: info.title,
                group: group || "",
                title: item.title,
                subtitle: item.subtitle || "",
                words: [item.title, item.subtitle || "", item.keywords || "", group || "", info.title].join(" ").toLowerCase()
            });
        }
        const kids = item.children;
        for (let i = 0; i < kids.length; ++i)
            collectRows(kids[i], out, info, group);
    }

    function findRows(text) {
        const words = text.toLowerCase().split(/\s+/).filter((w) => w.length > 0);
        if (words.length === 0)
            return [];
        let all = [];
        for (let i = 0; i < pages.length; ++i) {
            const flick = pageRepeater.itemAt(i);
            const content = flick ? flick.page : null;
            if (content)
                collectRows(content, all, { index: i, title: pages[i].title }, "");
        }
        const first = words[0];
        return all.filter((r) => words.every((w) => r.words.indexOf(w) >= 0))
                  .sort((a, b) => (b.title.toLowerCase().indexOf(first) >= 0) - (a.title.toLowerCase().indexOf(first) >= 0))
                  .slice(0, 40);
    }

    function openResult(result, byKeys) {
        searchField.text = "";
        showPage(result.pageIndex);
        const row = result.row;
        Qt.callLater(() => dialog.revealRow(row, byKeys));
    }

    /** The row with this title on the page shown, for `showRow`. */
    function rowByTitle(title) {
        const flick = pageFlick();
        if (!flick || !flick.page)
            return null;
        let rows = [];
        collectRows(flick.page, rows, { index: pageIndex, title: "" }, "");
        const hit = rows.find((r) => r.title === title);
        return hit ? hit.row : null;
    }

    Keys.onPressed: (event) => {
        if (event.modifiers & ~Qt.KeypadModifier)
            return;
        if (event.key === Qt.Key_PageUp) {
            showPage(pageIndex - 1);
            event.accepted = true;
        } else if (event.key === Qt.Key_PageDown) {
            showPage(pageIndex + 1);
            event.accepted = true;
        } else if (event.key === Qt.Key_Left) {
            // Left that no control in the page took: back to the sidebar
            const flick = pageFlick();
            const item = dialog.Window.window ? dialog.Window.window.activeFocusItem : null;
            if (flick && item && inside(item, flick.contentItem)) {
                side.forceActiveFocus(Qt.TabFocusReason);
                event.accepted = true;
            }
        }
    }

    // The sidebar has the focus when the window opens
    restoreFocusItem: side

    onPageChanged: {
        const flick = pageFlick();
        if (flick)
            flick.contentY = 0;
    }

    Item {
        // DialogView's content; the window below covers DialogView's own bar as well

        Timer {
            id: revealSettle
            interval: 80
            onTriggered: dialog.placeReveal()
        }

        Shortcut {
            sequences: [StandardKey.Find]
            enabled: dialog.StackView.status === StackView.Active
            onActivated: {
                searchField.forceActiveFocus(Qt.ShortcutFocusReason);
                searchField.readOnly = false;
                searchField.selectAll();
            }
        }

        // Keeps the control with the focus on screen, with the name of its setting
        Connections {
            target: dialog.Window.window
            function onActiveFocusItemChanged() {
                const item = dialog.Window.window ? dialog.Window.window.activeFocusItem : null;
                const flick = dialog.pageFlick();
                if (!item || !flick || !dialog.inside(item, flick.contentItem))
                    return;
                dialog.scrollTo(dialog.rowOf(item, flick), flick, 12);
            }
        }


        Item {
            id: win
            parent: dialog
            anchors.fill: parent
            z: 10

            /** Below this the sidebar shows only icons. */
            readonly property bool compact: width < 860
            readonly property int gutter: width < 600 ? 16 : 32

            // Nothing under the window is pressed by mistake
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
                transform: Scale { yScale: 380 / 900 }

                ShapePath {
                    strokeWidth: 0
                    strokeColor: "transparent"
                    fillGradient: RadialGradient {
                        centerX: 0
                        centerY: -90
                        focalX: 0
                        focalY: -90
                        centerRadius: 900
                        GradientStop { position: 0.0; color: Qt.rgba(140 / 255, 82 / 255, 1, 0.14) }
                        GradientStop { position: 0.7; color: Qt.rgba(140 / 255, 82 / 255, 1, 0) }
                    }
                    startX: 0
                    startY: 0
                    PathLine { x: win.width; y: 0 }
                    PathLine { x: win.width; y: 900 }
                    PathLine { x: 0; y: 900 }
                    PathLine { x: 0; y: 0 }
                }
            }

            // ------------------------------------------------ title bar (first in the focus chain)

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
                        leftMargin: win.gutter
                        rightMargin: win.gutter
                    }
                    spacing: 14

                    // Escape does the same, so it stays out of the focus chain
                    C.IconButton {
                        iconName: "back"
                        tip: qsTr("Back")
                        focusPolicy: Qt.NoFocus
                        onClicked: {
                            dialog.rejected();
                            dialog.close();
                        }
                    }

                    Label {
                        color: C.Theme.text
                        font.pixelSize: 22
                        font.weight: Font.ExtraBold
                        text: dialog.title
                    }

                    Rectangle {
                        visible: win.width >= 720
                        implicitWidth: savedRow.implicitWidth + 20
                        implicitHeight: 26
                        radius: 13
                        color: Qt.rgba(52 / 255, 211 / 255, 153 / 255, 0.10)

                        Row {
                            id: savedRow
                            anchors.centerIn: parent
                            spacing: 6

                            C.Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                iconName: "check"
                                size: 13
                                color: "#9ff0cc"
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Saved as you go")
                                color: "#9ff0cc"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                            }
                        }
                    }

                    Item { Layout.fillWidth: true }

                    C.Field {
                        id: searchField
                        Layout.preferredWidth: Math.min(320, Math.max(150, win.width * 0.3))
                        iconName: "search"
                        hint: qsTr("Search all settings")
                        Accessible.name: qsTr("Search all settings")
                        firstInFocusChain: true
                        // Down is handled below: into what was found, or to the sidebar
                        lastInFocusChain: true
                        onTextChanged: dialog.search = text.trim()

                        Keys.onDownPressed: (event) => {
                            if (dialog.search !== "" && resultRepeater.count > 0)
                                resultRepeater.itemAt(0).forceActiveFocus(Qt.TabFocusReason);
                            else
                                side.forceActiveFocus(Qt.TabFocusReason);
                            event.accepted = true;
                        }
                        Keys.onReturnPressed: (event) => {
                            // With something found, Return opens the first of it
                            if (dialog.search !== "" && resultRepeater.count > 0) {
                                const first = resultRepeater.itemAt(0);
                                dialog.openResult(first.result, true);
                                event.accepted = true;
                            } else {
                                // To TextField.qml: Return starts or ends typing
                                event.accepted = false;
                            }
                        }
                        Keys.onEscapePressed: (event) => {
                            if (text.length > 0) {
                                text = "";
                                event.accepted = true;
                            } else {
                                event.accepted = false;
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------ sidebar

            Flickable {
                id: sideFlick
                anchors {
                    top: titleBar.bottom
                    left: parent.left
                    bottom: parent.bottom
                    leftMargin: win.gutter - (win.compact ? 6 : 0)
                }
                width: win.compact ? 56 : 264
                contentWidth: width
                contentHeight: side.implicitHeight + 18 + 24
                clip: true
                flickableDirection: Flickable.VerticalFlick
                boundsBehavior: Flickable.StopAtBounds

                Control {
                    id: side
                    y: 18
                    width: sideFlick.width
                    focusPolicy: Qt.StrongFocus
                    padding: 0
                    Accessible.role: Accessible.PageTabList
                    Accessible.name: qsTr("Settings pages")

                    Keys.onPressed: (event) => {
                        switch (event.key) {
                        case Qt.Key_Up:
                            if (dialog.pageIndex > 0)
                                dialog.showPage(dialog.pageIndex - 1);
                            else
                                searchField.forceActiveFocus(Qt.TabFocusReason);
                            event.accepted = true;
                            break;
                        case Qt.Key_Down:
                            dialog.showPage(dialog.pageIndex + 1);
                            event.accepted = true;
                            break;
                        case Qt.Key_Right:
                        case Qt.Key_Return:
                        case Qt.Key_Enter:
                        case Qt.Key_Space:
                            dialog.enterPage();
                            event.accepted = true;
                            break;
                        }
                    }

                    // The entry with the keys on it stays on screen
                    Connections {
                        target: dialog
                        function onPageIndexChanged() {
                            const entry = entryRepeater.itemAt(dialog.pageIndex);
                            if (entry)
                                dialog.scrollTo(entry, sideFlick, 8);
                        }
                    }

                    contentItem: Column {
                        spacing: 2

                        Repeater {
                            id: entryRepeater
                            model: dialog.pages

                            Item {
                                id: entry
                                required property var modelData
                                required property int index
                                readonly property bool current: index === dialog.pageIndex && dialog.search === ""
                                readonly property bool isProfile: modelData.key === "profile"
                                readonly property bool groupStart: !isProfile && (index === 0 || dialog.pages[index - 1].group !== modelData.group)

                                width: side.width
                                height: (groupStart ? groupLabel.height : 0) + button.height + (isProfile ? 6 : 0)

                                // The group's name above its first page; a thin line when there is no room for it
                                Item {
                                    id: groupLabel
                                    visible: entry.groupStart
                                    width: parent.width
                                    height: win.compact ? 13 : (entry.index <= 1 ? 26 : 32)

                                    Text {
                                        visible: !win.compact
                                        anchors {
                                            left: parent.left
                                            bottom: parent.bottom
                                            leftMargin: 10
                                            bottomMargin: 4
                                        }
                                        text: entry.modelData.group
                                        font.pixelSize: 11
                                        font.weight: Font.ExtraBold
                                        font.letterSpacing: 1.3
                                        font.capitalization: Font.AllUppercase
                                        color: C.Theme.textDim
                                    }

                                    Rectangle {
                                        visible: win.compact
                                        anchors {
                                            left: parent.left
                                            right: parent.right
                                            verticalCenter: parent.verticalCenter
                                            leftMargin: 10
                                            rightMargin: 10
                                        }
                                        height: 1
                                        color: C.Theme.line
                                    }
                                }

                                Rectangle {
                                    id: button
                                    y: entry.groupStart ? groupLabel.height : 0
                                    width: parent.width
                                    height: entry.isProfile ? (win.compact ? 48 : 58) : 36
                                    radius: entry.isProfile ? 14 : 10
                                    color: entry.isProfile ? (entry.current ? Qt.rgba(140 / 255, 82 / 255, 1, 0.16) : C.Theme.card)
                                         : entry.current ? Qt.rgba(140 / 255, 82 / 255, 1, 0.20)
                                         : entryMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.05) : "transparent"
                                    border.width: side.visualFocus && entry.current ? 2 : entry.isProfile ? (entry.current ? 2 : 1) : 0
                                    border.color: side.visualFocus && entry.current ? C.Theme.focus
                                                : entry.isProfile && entry.current ? C.Theme.brandFrom : C.Theme.line

                                    ToolTip.visible: win.compact && entryMouse.containsMouse
                                    ToolTip.delay: 400
                                    ToolTip.text: entry.isProfile ? qsTr("Your profile") : entry.modelData.title

                                    // Your profile: picture, name, and what is set there
                                    RowLayout {
                                        visible: entry.isProfile
                                        anchors {
                                            fill: parent
                                            leftMargin: win.compact ? 0 : 10
                                            rightMargin: win.compact ? 0 : 10
                                        }
                                        spacing: 10

                                        C.Avatar {
                                            Layout.alignment: win.compact ? Qt.AlignHCenter : Qt.AlignVCenter
                                            Layout.fillWidth: win.compact
                                            Layout.maximumWidth: 36
                                            Layout.preferredWidth: 36
                                            Layout.preferredHeight: 36
                                            source: Chiaki.settings.coplayAvatarUrl
                                            name: dialog.playerName || qsTr("Guest")
                                        }

                                        ColumnLayout {
                                            visible: !win.compact
                                            Layout.fillWidth: true
                                            spacing: 1

                                            Text {
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                                text: dialog.playerName || qsTr("Your name")
                                                color: dialog.playerName ? C.Theme.text : C.Theme.textSoft
                                                font.pixelSize: 14
                                                font.weight: Font.ExtraBold
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                elide: Text.ElideRight
                                                text: qsTr("Your name and picture")
                                                color: entry.current ? "#cdc8ea" : C.Theme.textDim
                                                font.pixelSize: 12
                                            }
                                        }

                                        C.Icon {
                                            visible: !win.compact
                                            iconName: "chevron-right"
                                            size: 16
                                            color: entry.current ? "#cdb6ff" : C.Theme.textDim
                                        }
                                    }

                                    // A page
                                    RowLayout {
                                        visible: !entry.isProfile
                                        anchors {
                                            fill: parent
                                            leftMargin: 10
                                            rightMargin: 10
                                        }
                                        spacing: 10

                                        C.Icon {
                                            Layout.alignment: win.compact ? Qt.AlignHCenter : Qt.AlignVCenter
                                            Layout.fillWidth: win.compact
                                            iconName: entry.modelData.icon
                                            size: 17
                                            color: entry.current ? "#cdb6ff" : "#a9a4c8"
                                        }

                                        Text {
                                            visible: !win.compact
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            text: entry.modelData.title
                                            color: entry.current ? "#ffffff" : "#d9d5f2"
                                            font.pixelSize: 14
                                            font.weight: entry.current ? Font.ExtraBold : Font.DemiBold
                                        }
                                    }

                                    MouseArea {
                                        id: entryMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            searchField.text = "";
                                            dialog.showPage(entry.index);
                                            side.forceActiveFocus(Qt.MouseFocusReason);
                                        }
                                    }
                                }
                            }
                        }

                        Item { width: 1; height: 14 }

                        // L1 / R1 change the page, once a controller is there
                        Row {
                            visible: !win.compact && Chiaki.controllers.length > 0
                            spacing: 6
                            leftPadding: 10

                            Image {
                                width: 22
                                height: 22
                                sourceSize: Qt.size(width, height)
                                source: "qrc:/icons/l1.svg"
                            }

                            Image {
                                width: 22
                                height: 22
                                sourceSize: Qt.size(width, height)
                                source: "qrc:/icons/r1.svg"
                            }

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("change the page")
                                color: C.Theme.textDim
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }

            // ------------------------------------------------ the pages

            Item {
                id: content
                anchors {
                    top: titleBar.bottom
                    left: sideFlick.right
                    right: parent.right
                    bottom: parent.bottom
                    leftMargin: win.compact ? 18 : 28
                }

                /** Width of a page: the room there is, up to a width that still reads well. */
                readonly property real pageWidth: Math.max(0, Math.min(width - win.gutter, 920))

                Repeater {
                    id: pageRepeater
                    model: dialog.pages

                    Flickable {
                        id: pageFlick
                        required property var modelData
                        required property int index
                        readonly property bool current: index === dialog.pageIndex && dialog.search === ""
                        readonly property Item page: loader.item

                        anchors.fill: parent
                        // Only the page shown can be seen, pressed and reached by the keys
                        opacity: current ? 1 : 0
                        enabled: current
                        z: current ? 1 : 0
                        contentWidth: width
                        contentHeight: loader.height + 18 + 32
                        clip: true
                        flickableDirection: Flickable.VerticalFlick
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {
                            policy: pageFlick.contentHeight > pageFlick.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                            width: 10
                        }

                        Loader {
                            id: loader
                            y: 18
                            width: content.pageWidth
                            height: item ? item.implicitHeight : 0
                            source: "settings/" + pageFlick.modelData.file
                            onLoaded: {
                                item.width = Qt.binding(() => loader.width);
                                if (pageFlick.current && dialog.showRow) {
                                    const title = dialog.showRow;
                                    dialog.showRow = "";
                                    Qt.callLater(() => dialog.revealRow(dialog.rowByTitle(title), false));
                                }
                            }
                        }
                    }
                }

                // A row found by the search lights up for a moment
                Rectangle {
                    id: flash
                    visible: opacity > 0
                    opacity: 0
                    radius: 14
                    color: Qt.rgba(140 / 255, 82 / 255, 1, 0.16)
                    border.width: 2
                    border.color: C.Theme.brandFrom
                    z: 100

                    function show(row, flick) {
                        parent = flick.contentItem;
                        const p = row.mapToItem(flick.contentItem, 0, 0);
                        x = p.x - 4;
                        y = p.y;
                        width = row.width + 8;
                        height = row.height;
                        fade.restart();
                    }

                    SequentialAnimation {
                        id: fade
                        NumberAnimation { target: flash; property: "opacity"; to: 1; duration: 120 }
                        PauseAnimation { duration: 900 }
                        NumberAnimation { target: flash; property: "opacity"; to: 0; duration: 600 }
                    }
                }

                // ------------------------------------------------ what the search found

                Flickable {
                    id: results
                    anchors.fill: parent
                    visible: dialog.search !== ""
                    z: 2
                    contentWidth: width
                    contentHeight: resultColumn.implicitHeight + 18 + 32
                    clip: true
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {
                        policy: results.contentHeight > results.height ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                        width: 10
                    }

                    readonly property var found: visible ? dialog.findRows(dialog.search) : []

                    ColumnLayout {
                        id: resultColumn
                        y: 18
                        width: content.pageWidth
                        spacing: 16

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: qsTr("Search: “%1”").arg(dialog.search)
                                color: C.Theme.text
                                font.pixelSize: 24
                                font.weight: Font.ExtraBold
                            }

                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: results.found.length === 0 ? qsTr("No setting goes by that. Try another word, or look through the pages on the left.")
                                    : results.found.length === 1 ? qsTr("One setting.")
                                    : qsTr("%1 settings.").arg(results.found.length)
                                color: C.Theme.textSoft
                                font.pixelSize: 14
                            }
                        }

                        Rectangle {
                            visible: results.found.length > 0
                            Layout.fillWidth: true
                            implicitHeight: resultList.implicitHeight
                            radius: 18
                            color: C.Theme.card
                            border.width: 1
                            border.color: C.Theme.line

                            Column {
                                id: resultList
                                width: parent.width

                                Repeater {
                                    id: resultRepeater
                                    model: results.found

                                    AbstractButton {
                                        id: hit
                                        required property var modelData
                                        required property int index
                                        readonly property var result: modelData
                                        property bool firstInFocusChain: false
                                        property bool lastInFocusChain: index === resultRepeater.count - 1

                                        width: resultList.width
                                        implicitHeight: hitTexts.implicitHeight + 22
                                        hoverEnabled: true
                                        focusPolicy: Qt.StrongFocus
                                        Accessible.name: modelData.title + ", " + modelData.pageTitle
                                        onClicked: dialog.openResult(modelData, visualFocus)

                                        Keys.onPressed: (event) => {
                                            if (event.key === Qt.Key_Up && hit.index === 0) {
                                                searchField.forceActiveFocus(Qt.TabFocusReason);
                                                event.accepted = true;
                                            } else {
                                                C.FocusChain.handle(hit, event);
                                            }
                                        }

                                        background: Rectangle {
                                            color: hit.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                                            radius: 18
                                            border.width: hit.visualFocus ? 2 : 0
                                            border.color: C.Theme.focus

                                            Rectangle {
                                                visible: hit.index > 0
                                                width: parent.width
                                                height: 1
                                                color: Qt.rgba(1, 1, 1, 0.06)
                                            }
                                        }

                                        contentItem: RowLayout {
                                            spacing: 14

                                            ColumnLayout {
                                                id: hitTexts
                                                Layout.fillWidth: true
                                                Layout.leftMargin: 18
                                                spacing: 2

                                                Text {
                                                    Layout.fillWidth: true
                                                    wrapMode: Text.WordWrap
                                                    text: hit.modelData.title
                                                    color: C.Theme.text
                                                    font.pixelSize: 15
                                                    font.weight: Font.ExtraBold
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    elide: Text.ElideRight
                                                    text: hit.modelData.group ? hit.modelData.pageTitle + "  ›  " + hit.modelData.group : hit.modelData.pageTitle
                                                    color: C.Theme.headingSoft
                                                    font.pixelSize: 12
                                                    font.weight: Font.Bold
                                                }

                                                Text {
                                                    Layout.fillWidth: true
                                                    visible: text.length > 0
                                                    wrapMode: Text.WordWrap
                                                    maximumLineCount: 2
                                                    elide: Text.ElideRight
                                                    textFormat: Text.PlainText
                                                    text: hit.modelData.subtitle
                                                    color: C.Theme.textDim
                                                    font.pixelSize: 13
                                                }
                                            }

                                            C.Icon {
                                                Layout.rightMargin: 18
                                                iconName: "chevron-right"
                                                size: 16
                                                color: C.Theme.textDim
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
    }
}
